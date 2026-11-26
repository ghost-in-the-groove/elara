// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Plain C++ facade over the system audio device. JUCE is an implementation
// detail of elara_audio_juce and must never appear in this header (ADR-0002).

#include <expected>
#include <memory>
#include <string>
#include <string_view>
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

/// The output device SystemAudio opens: a driver type and a device name, as
/// listOutputDevices() gives them. An empty name is the system's default
/// device; an empty type looks for the name among every type, in order.
struct OutputDeviceChoice {
    std::string type;
    std::string name;

    [[nodiscard]] bool isDefault() const noexcept { return name.empty(); }
    friend bool operator==(const OutputDeviceChoice&, const OutputDeviceChoice&) = default;
};

/// "<type> / <name>", "<name>" (any type) or "" (the default), as the CLI
/// takes a device and listOutputDevices() prints one.
[[nodiscard]] OutputDeviceChoice parseOutputDeviceChoice(std::string_view text);

/// Owns one output device, a test-tone voice and the source it plays (a
/// Player). Construct, setSource() and destroy on the main thread.
/// open() and outputDevices() may also run on one other thread at a time,
/// while the main thread leaves the object alone: opening a device can take a
/// while, and the interface need not wait for it (ADR-0122).
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

    /// Opens `device` in stereo, the system's default by default. Opening the
    /// device already open does nothing; another one closes it first, and the
    /// source goes on playing on the new one. A device that is not there, or
    /// does not open, leaves this closed and says why.
    [[nodiscard]] std::expected<void, std::string> open(const OutputDeviceChoice& device = {});

    /// The output devices of every driver type, as listOutputDevices() gives
    /// them, found by this object's own device manager.
    [[nodiscard]] std::vector<OutputDeviceInfo> outputDevices();

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
