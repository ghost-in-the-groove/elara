// SPDX-License-Identifier: AGPL-3.0-or-later
// The output device playback plays through (ADR-0019, ADR-0122): the seam
// between the transport and the system's audio. SystemAudio (elara_audio_juce)
// is the device that plays through a sound card; tests play through one of
// their own.
#pragma once

#include <expected>
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

/// The output device to open: a driver type and a device name, as
/// OutputDevice::outputDevices() gives them. An empty name is the system's
/// default device; an empty type looks for the name among every type, in
/// order.
struct OutputDeviceChoice {
    std::string type;
    std::string name;

    [[nodiscard]] bool isDefault() const noexcept { return name.empty(); }
    friend bool operator==(const OutputDeviceChoice&, const OutputDeviceChoice&) = default;
};

/// "<type> / <name>", "<name>" (any type) or "" (the default), as the CLI
/// takes a device and lists one.
[[nodiscard]] OutputDeviceChoice parseOutputDeviceChoice(std::string_view text);

/// One output device, a test-tone voice and the source it plays (a Player).
///
/// Threads (ADR-0122): make, setSource() and destroy on the control thread.
/// open() and outputDevices() may also run on one other thread at a time,
/// while the control thread leaves the object alone, so that the interface
/// need not wait for a device that is slow to open. playTone(), stop() and
/// isPlaying() are safe from any thread and never block the audio callback.
class OutputDevice {
public:
    OutputDevice() = default;
    virtual ~OutputDevice() = default;
    OutputDevice(const OutputDevice&) = delete;
    OutputDevice& operator=(const OutputDevice&) = delete;
    OutputDevice(OutputDevice&&) = delete;
    OutputDevice& operator=(OutputDevice&&) = delete;

    /// Opens `device` in stereo. Opening the device already open does
    /// nothing; another one closes it first, and the source goes on playing on
    /// the new one. A device that is not there, or does not open, leaves this
    /// closed and says why.
    [[nodiscard]] virtual std::expected<void, std::string>
    open(const OutputDeviceChoice& device) = 0;

    /// The output devices of every driver type.
    [[nodiscard]] virtual std::vector<OutputDeviceInfo> outputDevices() = 0;

    /// Human-readable "<type> / <device> @ <rate> Hz, <buffer> samples"; empty
    /// while closed.
    [[nodiscard]] virtual std::string deviceDescription() const = 0;

    /// The device's sample rate in Hz; 0 while closed. It may change while
    /// open, when the system restarts the device at another rate.
    [[nodiscard]] virtual double sampleRate() const = 0;

    /// Plays `source` (nullptr for none) alongside the test tone. Once this
    /// returns, the previous source is no longer called and may be destroyed.
    virtual void setSource(AudioSource* source) = 0;

    /// Starts a sine tone with short fades; replaces any tone already playing.
    virtual void playTone(double frequencyHz, double seconds) noexcept = 0;

    /// Fades the current tone out quickly.
    virtual void stop() noexcept = 0;

    /// True while the test tone sounds.
    [[nodiscard]] virtual bool isPlaying() const noexcept = 0;
};

} // namespace elara::audio
