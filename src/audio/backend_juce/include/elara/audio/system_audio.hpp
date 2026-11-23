// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Plain C++ facade over the system audio device. JUCE is an implementation
// detail of elara_audio_juce and must never appear in this header (ADR-0002).

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace elara::audio {

class AudioSource;

struct OutputDeviceInfo {
    std::string type; // driver family, e.g. "Windows Audio", "CoreAudio", "ALSA"
    std::string name;
    bool isDefault = false;
};

/// Enumerates output devices of every available driver type.
/// Call from the main thread.
[[nodiscard]] std::vector<OutputDeviceInfo> listOutputDevices();

/// Owns the default output device, a test-tone voice and the source it plays
/// (a Player). Construct, open(), setSource() and destroy on the main thread;
/// playTone()/stop()/isPlaying() are safe from any thread and never block the
/// audio callback.
class SystemAudio {
public:
    SystemAudio();
    ~SystemAudio();
    SystemAudio(const SystemAudio&) = delete;
    SystemAudio& operator=(const SystemAudio&) = delete;
    SystemAudio(SystemAudio&&) = delete;
    SystemAudio& operator=(SystemAudio&&) = delete;

    /// Opens the default stereo output device. Idempotent.
    [[nodiscard]] std::expected<void, std::string> open();

    /// Human-readable "<type> / <device> @ <rate> Hz, <buffer> samples"; empty if closed.
    [[nodiscard]] std::string deviceDescription() const;

    /// The device's sample rate in Hz; 0 while closed.
    [[nodiscard]] double sampleRate() const;

    /// Plays `source` (nullptr for none) alongside the test tone. The audio
    /// callback is stopped while the source changes, so once this returns the
    /// previous source is no longer called and may be destroyed.
    void setSource(AudioSource* source);

    /// Starts a sine tone with short fades; replaces any tone already playing.
    void playTone(double frequencyHz, double seconds) noexcept;

    /// Fades the current tone out quickly.
    void stop() noexcept;

    [[nodiscard]] bool isPlaying() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace elara::audio
