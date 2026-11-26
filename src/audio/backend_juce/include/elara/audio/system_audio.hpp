// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Plain C++ facade over the system audio device. JUCE is an implementation
// detail of elara_audio_juce and must never appear in this header (ADR-0002).

#include <elara/audio/output_device.hpp>

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace elara::audio {

/// Enumerates output devices of every available driver type.
/// Call from the main thread.
[[nodiscard]] std::vector<OutputDeviceInfo> listOutputDevices();

/// The output device that plays through the system's sound card, through
/// JUCE. Its threads are OutputDevice's: it sets up the JUCE runtime on the
/// thread that makes it, which must therefore be the main thread.
class SystemAudio final : public OutputDevice {
public:
    SystemAudio();
    ~SystemAudio() override;
    SystemAudio(const SystemAudio&) = delete;
    SystemAudio& operator=(const SystemAudio&) = delete;
    SystemAudio(SystemAudio&&) = delete;
    SystemAudio& operator=(SystemAudio&&) = delete;

    [[nodiscard]] std::expected<void, std::string> open(const OutputDeviceChoice& device) override;
    /// Found by this object's own device manager, as listOutputDevices() finds them.
    [[nodiscard]] std::vector<OutputDeviceInfo> outputDevices() override;
    [[nodiscard]] std::string deviceDescription() const override;
    [[nodiscard]] double sampleRate() const override;
    /// The audio callback is stopped while the source changes.
    void setSource(AudioSource* source) override;
    void playTone(double frequencyHz, double seconds) noexcept override;
    void stop() noexcept override;
    [[nodiscard]] bool isPlaying() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace elara::audio
