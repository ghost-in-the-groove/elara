// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>

namespace elara::audio {

namespace {

constexpr float kToneAmplitude = 0.2f;
constexpr double kFadeSeconds = 0.005;

/// Audio-thread voice. Everything shared with other threads is atomic; the
/// callback never allocates, locks or blocks.
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

        for (int i = 0; i < numSamples; ++i) {
            float sample = 0.0f;
            if (position_ < totalFrames_) {
                sample = generator_.next() * fadeGain(position_, totalFrames_, fadeFrames_);
                ++position_;
            }
            for (int channel = 0; channel < numOutputs; ++channel) {
                if (outputs[channel] != nullptr) {
                    outputs[channel][i] = sample;
                }
            }
        }

        if (position_ >= totalFrames_ && !restart_.load(std::memory_order_acquire)) {
            playing_.store(false, std::memory_order_release);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        sampleRate_ = device->getCurrentSampleRate();
        fadeFrames_ = std::max<std::int64_t>(1, std::llround(kFadeSeconds * sampleRate_));
    }

    void audioDeviceStopped() override { playing_.store(false, std::memory_order_release); }

private:
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
};

} // namespace

std::vector<OutputDeviceInfo> listOutputDevices() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes(types);

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

struct SystemAudio::Impl {
    // Declaration order is destruction order in reverse: the JUCE runtime must
    // outlive the device manager, and the callback must outlive both.
    juce::ScopedJuceInitialiser_GUI juceInit;
    ToneCallback callback;
    juce::AudioDeviceManager deviceManager;
    bool isOpen = false;
};

SystemAudio::SystemAudio() : impl_(std::make_unique<Impl>()) {}

SystemAudio::~SystemAudio() {
    if (impl_->isOpen) {
        impl_->deviceManager.removeAudioCallback(&impl_->callback);
        impl_->deviceManager.closeAudioDevice();
    }
}

std::expected<void, std::string> SystemAudio::open() {
    if (impl_->isOpen) {
        return {};
    }
    const auto error = impl_->deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) {
        return std::unexpected(error.toStdString());
    }
    if (impl_->deviceManager.getCurrentAudioDevice() == nullptr) {
        return std::unexpected(std::string("no audio output device is available"));
    }
    impl_->deviceManager.addAudioCallback(&impl_->callback);
    impl_->isOpen = true;
    return {};
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
