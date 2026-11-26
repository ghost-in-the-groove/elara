// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/audio_source.hpp>
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <objbase.h> // CoIncrementMTAUsage()
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::audio {

namespace {

constexpr float kToneAmplitude = 0.2f;
constexpr double kFadeSeconds = 0.005;

/// Audio-thread voice and the source it plays. Everything shared with other
/// threads is atomic, or (the source) changed only while the callback is
/// removed; the callback never allocates, locks or blocks.
class ToneCallback final : public juce::AudioIODeviceCallback {
public:
    void request(double frequencyHz, double seconds) noexcept {
        frequencyHz_.store(frequencyHz, std::memory_order_relaxed);
        seconds_.store(seconds, std::memory_order_relaxed);
        restart_.store(true, std::memory_order_release);
        playing_.store(true, std::memory_order_release);
    }

    void stop() noexcept { stop_.store(true, std::memory_order_release); }

    [[nodiscard]] bool isPlaying() const noexcept {
        return playing_.load(std::memory_order_acquire);
    }

    // Only while the callback is removed from the device.
    void setSource(AudioSource* source) noexcept { source_ = source; }

    void
    audioDeviceIOCallbackWithContext(const float* const* /*inputs*/, int /*numInputs*/,
                                     float* const* outputs, int numOutputs, int numSamples,
                                     const juce::AudioIODeviceCallbackContext& /*ctx*/) override {
        if (restart_.exchange(false, std::memory_order_acq_rel)) {
            generator_ = ToneGenerator(frequencyHz_.load(std::memory_order_relaxed), sampleRate_,
                                       kToneAmplitude);
            totalFrames_ = static_cast<std::int64_t>(
                std::llround(seconds_.load(std::memory_order_relaxed) * sampleRate_));
            position_ = 0;
        }
        if (stop_.exchange(false, std::memory_order_acq_rel)) {
            totalFrames_ = std::min(totalFrames_, position_ + fadeFrames_);
        }

        for (int start = 0; start < numSamples;) {
            const int count = std::min(numSamples - start, static_cast<int>(left_.size()));
            renderSource(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                float tone = 0.0f;
                if (position_ < totalFrames_) {
                    tone = generator_.next() * fadeGain(position_, totalFrames_, fadeFrames_);
                    ++position_;
                }
                const auto frame = static_cast<std::size_t>(i);
                const float left = left_[frame] + tone;
                const float right = right_[frame] + tone;
                for (int channel = 0; channel < numOutputs; ++channel) {
                    if (outputs[channel] != nullptr) {
                        outputs[channel][start + i] = numOutputs == 1    ? 0.5f * (left + right)
                                                      : channel % 2 == 0 ? left
                                                                         : right;
                    }
                }
            }
            start += count;
        }

        if (position_ >= totalFrames_ && !restart_.load(std::memory_order_acquire)) {
            playing_.store(false, std::memory_order_release);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        sampleRate_ = device->getCurrentSampleRate();
        fadeFrames_ = std::max<std::int64_t>(1, std::llround(kFadeSeconds * sampleRate_));
        // Before the callbacks start, so the callback itself never allocates.
        const auto frames =
            static_cast<std::size_t>(std::max(device->getCurrentBufferSizeSamples(), 256));
        left_.assign(frames, 0.0f);
        right_.assign(frames, 0.0f);
    }

    void audioDeviceStopped() override { playing_.store(false, std::memory_order_release); }

private:
    void renderSource(std::size_t frames) noexcept {
        const std::span<float> left(left_.data(), frames);
        const std::span<float> right(right_.data(), frames);
        if (source_ != nullptr) {
            source_->render(left, right);
        } else {
            std::ranges::fill(left, 0.0f);
            std::ranges::fill(right, 0.0f);
        }
    }

    std::atomic<double> frequencyHz_{440.0};
    std::atomic<double> seconds_{0.0};
    std::atomic<bool> restart_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};

    // Audio-thread state (written in audioDeviceAboutToStart before callbacks start).
    double sampleRate_ = 48000.0;
    std::int64_t fadeFrames_ = 240;
    ToneGenerator generator_;
    std::int64_t totalFrames_ = 0;
    std::int64_t position_ = 0;
    AudioSource* source_ = nullptr;
    std::vector<float> left_ = std::vector<float>(512, 0.0f);
    std::vector<float> right_ = std::vector<float>(512, 0.0f);
};

// JUCE opens Windows devices through COM, which it starts only on its message
// thread, the main thread. Keeping the process's multithreaded apartment alive
// lets any other thread use COM without joining an apartment itself, so that
// a device can open on a worker (ADR-0122). Elsewhere there is nothing to do.
void letOtherThreadsUseCom() {
#ifdef _WIN32
    static const bool kept = [] {
        CO_MTA_USAGE_COOKIE cookie{};
        return SUCCEEDED(CoIncrementMTAUsage(&cookie)); // never released: for the process's life
    }();
    static_cast<void>(kept);
#endif
}

// Every output device of every type `types` holds, scanned again so that a
// device plugged in since shows up.
std::vector<OutputDeviceInfo> outputsOf(const juce::OwnedArray<juce::AudioIODeviceType>& types) {
    std::vector<OutputDeviceInfo> devices;
    for (auto* type : types) {
        type->scanForDevices();
        const auto names = type->getDeviceNames(false);
        const auto defaultIndex = type->getDefaultDeviceIndex(false);
        for (int i = 0; i < names.size(); ++i) {
            devices.push_back({.type = type->getTypeName().toStdString(),
                               .name = names[i].toStdString(),
                               .isDefault = i == defaultIndex});
        }
    }
    return devices;
}

std::string describe(const OutputDeviceChoice& device) {
    return device.type.empty() ? device.name : std::format("{} / {}", device.type, device.name);
}

// The type and the name of the device `device` stands for among `types`: the
// first type's default device for the default.
std::optional<std::pair<juce::String, juce::String>>
findOutput(const juce::OwnedArray<juce::AudioIODeviceType>& types,
           const OutputDeviceChoice& device) {
    for (auto* type : types) {
        const auto names = type->getDeviceNames(false);
        if (device.isDefault()) {
            if (!names.isEmpty()) {
                return std::pair{type->getTypeName(),
                                 names[std::max(0, type->getDefaultDeviceIndex(false))]};
            }
        } else if ((device.type.empty() || type->getTypeName() == juce::String(device.type)) &&
                   names.contains(juce::String(device.name))) {
            return std::pair{type->getTypeName(), juce::String(device.name)};
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<OutputDeviceInfo> listOutputDevices() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes(types);
    return outputsOf(types);
}

struct SystemAudio::Impl {
    // Declaration order is destruction order in reverse: the JUCE runtime must
    // outlive the device manager, and the callback must outlive both.
    juce::ScopedJuceInitialiser_GUI juceInit;
    ToneCallback callback;
    juce::AudioDeviceManager deviceManager;
    OutputDeviceChoice device; // the one open
    bool isOpen = false;
    bool hasCallback = false; // added on the first open, kept across devices
};

SystemAudio::SystemAudio() : impl_(std::make_unique<Impl>()) {}

SystemAudio::~SystemAudio() {
    if (impl_->hasCallback) {
        impl_->deviceManager.removeAudioCallback(&impl_->callback);
    }
    impl_->deviceManager.closeAudioDevice();
}

std::expected<void, std::string> SystemAudio::open(const OutputDeviceChoice& device) {
    if (impl_->isOpen && impl_->device == device) {
        return {};
    }
    letOtherThreadsUseCom();
    auto& manager = impl_->deviceManager;
    impl_->isOpen = false;
    manager.closeAudioDevice();
    const auto& types = manager.getAvailableDeviceTypes();
    auto found = findOutput(types, device);
    if (!found) {
        outputsOf(types); // plugged in since the last scan?
        found = findOutput(types, device);
    }
    if (!found) {
        return std::unexpected(device.isDefault()
                                   ? std::string("no audio output device is available")
                                   : std::format("no output device \"{}\"", describe(device)));
    }
    // A saved setup names the type and the device; without falling back to
    // the default, a device that does not open leaves none open.
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", found->first);
    setup.setAttribute("audioOutputDeviceName", found->second);
    if (const auto error = manager.initialise(0, 2, &setup, false); error.isNotEmpty()) {
        return std::unexpected(error.toStdString());
    }
    if (manager.getCurrentAudioDevice() == nullptr) {
        return std::unexpected(std::format("the output device \"{} / {}\" did not open",
                                           found->first.toStdString(),
                                           found->second.toStdString()));
    }
    if (!impl_->hasCallback) {
        manager.addAudioCallback(&impl_->callback);
        impl_->hasCallback = true;
    }
    impl_->device = device;
    impl_->isOpen = true;
    return {};
}

std::vector<OutputDeviceInfo> SystemAudio::outputDevices() {
    letOtherThreadsUseCom();
    return outputsOf(impl_->deviceManager.getAvailableDeviceTypes());
}

std::string SystemAudio::deviceDescription() const {
    auto* device = impl_->deviceManager.getCurrentAudioDevice();
    if (device == nullptr) {
        return {};
    }
    return std::format("{} / {} @ {:.0f} Hz, {} samples", device->getTypeName().toStdString(),
                       device->getName().toStdString(), device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples());
}

double SystemAudio::sampleRate() const {
    auto* device = impl_->deviceManager.getCurrentAudioDevice();
    return device == nullptr ? 0.0 : device->getCurrentSampleRate();
}

void SystemAudio::setSource(AudioSource* source) {
    if (impl_->hasCallback) {
        // removeAudioCallback() waits for a callback in progress to finish.
        impl_->deviceManager.removeAudioCallback(&impl_->callback);
    }
    impl_->callback.setSource(source);
    if (impl_->hasCallback) {
        impl_->deviceManager.addAudioCallback(&impl_->callback);
    }
}

void SystemAudio::playTone(double frequencyHz, double seconds) noexcept {
    impl_->callback.request(frequencyHz, seconds);
}

void SystemAudio::stop() noexcept {
    impl_->callback.stop();
}

bool SystemAudio::isPlaying() const noexcept {
    return impl_->callback.isPlaying();
}

} // namespace elara::audio
