// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/microphone_input.hpp>
#include <elara/audio/sample_ring.hpp>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <span>

namespace elara::audio {

namespace {

/// Audio-thread side: queues the samples of the loudest input channel, so
/// that a guitar plugged into one input of an interface is not mixed with the
/// noise of the others. Another channel takes over only when it is twice as
/// loud (6 dB), so the choice does not flicker between similar channels.
class InputCallback final : public juce::AudioIODeviceCallback {
public:
    explicit InputCallback(SampleRing& ring) : ring_(ring) {}

    float takePeak() noexcept { return peak_.exchange(0.0f, std::memory_order_relaxed); }

    void
    audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                     float* const* outputs, int numOutputs, int numSamples,
                                     const juce::AudioIODeviceCallbackContext& /*ctx*/) override {
        for (int channel = 0; channel < numOutputs; ++channel) {
            if (outputs[channel] != nullptr) {
                std::fill_n(outputs[channel], numSamples, 0.0f);
            }
        }
        int loudest = -1;
        double loudestEnergy = 0.0;
        double currentEnergy = -1.0;
        for (int channel = 0; channel < numInputs; ++channel) {
            if (inputs[channel] == nullptr) {
                continue;
            }
            double energy = 0.0;
            for (int i = 0; i < numSamples; ++i) {
                energy += static_cast<double>(inputs[channel][i]) * inputs[channel][i];
            }
            if (channel == channel_) {
                currentEnergy = energy;
            }
            if (loudest < 0 || energy > loudestEnergy) {
                loudest = channel;
                loudestEnergy = energy;
            }
        }
        if (loudest < 0) {
            return;
        }
        if (currentEnergy < 0.0 || loudestEnergy > 4.0 * currentEnergy) {
            channel_ = loudest;
        }
        const std::span<const float> samples(inputs[channel_], static_cast<std::size_t>(numSamples));
        float peak = 0.0f;
        for (const float sample : samples) {
            peak = std::max(peak, std::abs(sample));
        }
        ring_.write(samples);
        float seen = peak_.load(std::memory_order_relaxed);
        while (peak > seen && !peak_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* /*device*/) override { channel_ = 0; }

    void audioDeviceStopped() override {}

private:
    SampleRing& ring_;
    int channel_ = 0; // the input listened to; audio thread only
    std::atomic<float> peak_{0.0f};
};

} // namespace

std::vector<InputDeviceInfo> listInputDevices() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes(types);

    std::vector<InputDeviceInfo> devices;
    for (auto* type : types) {
        type->scanForDevices();
        const auto names = type->getDeviceNames(true);
        const auto defaultIndex = type->getDefaultDeviceIndex(true);
        for (int i = 0; i < names.size(); ++i) {
            devices.push_back({.type = type->getTypeName().toStdString(),
                               .name = names[i].toStdString(),
                               .isDefault = i == defaultIndex});
        }
    }
    return devices;
}

struct MicrophoneInput::Impl {
    // Destroyed in reverse order: the device manager stops the callback before
    // the callback and the ring go, and the JUCE runtime outlives them all.
    juce::ScopedJuceInitialiser_GUI juceInit;
    SampleRing ring{kQueueSamples};
    InputCallback callback{ring};
    juce::AudioDeviceManager deviceManager;
    bool isOpen = false;
};

MicrophoneInput::MicrophoneInput() : impl_(std::make_unique<Impl>()) {}

MicrophoneInput::~MicrophoneInput() {
    close();
}

std::expected<void, std::string> MicrophoneInput::open() {
    if (impl_->isOpen) {
        return {};
    }
    const auto error = impl_->deviceManager.initialiseWithDefaultDevices(2, 0);
    if (error.isNotEmpty()) {
        return std::unexpected(error.toStdString());
    }
    auto* device = impl_->deviceManager.getCurrentAudioDevice();
    if (device == nullptr || device->getActiveInputChannels().isZero()) {
        impl_->deviceManager.closeAudioDevice();
        return std::unexpected(std::string("no audio input device is available"));
    }
    impl_->ring.clear(); // what was heard before the last close()
    impl_->deviceManager.addAudioCallback(&impl_->callback);
    impl_->isOpen = true;
    return {};
}

void MicrophoneInput::close() {
    if (!impl_->isOpen) {
        return;
    }
    impl_->deviceManager.removeAudioCallback(&impl_->callback);
    impl_->deviceManager.closeAudioDevice();
    impl_->isOpen = false;
}

bool MicrophoneInput::isOpen() const noexcept {
    return impl_->isOpen;
}

std::string MicrophoneInput::deviceDescription() const {
    auto* device = impl_->deviceManager.getCurrentAudioDevice();
    if (!impl_->isOpen || device == nullptr) {
        return {};
    }
    return std::format("{} / {} @ {:.0f} Hz, {} samples", device->getTypeName().toStdString(),
                       device->getName().toStdString(), device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples());
}

double MicrophoneInput::sampleRate() const {
    auto* device = impl_->deviceManager.getCurrentAudioDevice();
    return !impl_->isOpen || device == nullptr ? 0.0 : device->getCurrentSampleRate();
}

std::size_t MicrophoneInput::read(std::span<float> out) noexcept {
    return impl_->ring.read(out);
}

float MicrophoneInput::takePeak() noexcept {
    return impl_->callback.takePeak();
}

std::size_t MicrophoneInput::dropped() const noexcept {
    return impl_->ring.dropped();
}

} // namespace elara::audio
