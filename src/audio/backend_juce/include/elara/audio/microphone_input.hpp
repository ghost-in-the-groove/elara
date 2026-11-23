// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Plain C++ facade over the system audio input, for the tuner (ADR-0029).
// JUCE is an implementation detail of elara_audio_juce and must never appear
// in this header (ADR-0002).

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace elara::audio {

struct InputDeviceInfo {
    std::string type; // driver family, e.g. "Windows Audio", "CoreAudio", "ALSA"
    std::string name;
    bool isDefault = false;
};

/// Enumerates input devices of every available driver type.
/// Call from the main thread.
[[nodiscard]] std::vector<InputDeviceInfo> listInputDevices();

/// Listens to the default input device. Its callback queues the samples of
/// the loudest input channel in a lock-free ring, so it never allocates, locks
/// or blocks; one other thread takes them with read().
///
/// Independent of SystemAudio: listening never interrupts playback. Construct,
/// open(), close() and destroy on the main thread.
class MicrophoneInput {
public:
    /// Samples queued at most: over 5 s at 48 kHz.
    static constexpr std::size_t kQueueSamples = std::size_t{1} << 18U;

    MicrophoneInput();
    ~MicrophoneInput();
    MicrophoneInput(const MicrophoneInput&) = delete;
    MicrophoneInput& operator=(const MicrophoneInput&) = delete;
    MicrophoneInput(MicrophoneInput&&) = delete;
    MicrophoneInput& operator=(MicrophoneInput&&) = delete;

    /// Opens the default input device and starts listening, without what
    /// was heard before the last close(). Idempotent.
    [[nodiscard]] std::expected<void, std::string> open();

    /// Stops listening and closes the device. Idempotent.
    void close();

    [[nodiscard]] bool isOpen() const noexcept;

    /// Human-readable "<type> / <device> @ <rate> Hz, <buffer> samples"; empty if closed.
    [[nodiscard]] std::string deviceDescription() const;

    /// The device's sample rate in Hz; 0 while closed.
    [[nodiscard]] double sampleRate() const;

    /// Takes up to `out.size()` of the samples heard since the last call,
    /// oldest first, and returns how many. Call from one thread at a time.
    std::size_t read(std::span<float> out) noexcept;

    /// The loudest sample heard since the last call, from 0 to 1.
    float takePeak() noexcept;

    /// Samples dropped because read() was not called often enough.
    [[nodiscard]] std::size_t dropped() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace elara::audio
