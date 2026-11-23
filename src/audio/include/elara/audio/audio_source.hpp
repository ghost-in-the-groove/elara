// SPDX-License-Identifier: AGPL-3.0-or-later
// What an audio device plays (ADR-0019).
#pragma once

#include <span>

namespace elara::audio {

/// Something an audio device plays. render() is called on the audio thread
/// and must be real-time safe: no allocation, locks, waiting, I/O or throwing.
class AudioSource {
public:
    AudioSource() = default;
    virtual ~AudioSource() = default;
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&&) = delete;
    AudioSource& operator=(AudioSource&&) = delete;

    /// Writes the next frames to `left` and `right`, which have the same size.
    virtual void render(std::span<float> left, std::span<float> right) noexcept = 0;
};

} // namespace elara::audio
