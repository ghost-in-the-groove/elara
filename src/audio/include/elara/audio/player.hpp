// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback with the built-in synthesizer (ADR-0019).
#pragma once

#include <elara/audio/audio_source.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace elara::core {
class Song;
}

namespace elara::audio {

class Sequence;

/// Plays a sequence in real time through one built-in Synth per track.
///
/// Two threads use a player. The control thread (the main thread) constructs
/// it and calls load(), play(), pause(), seek() and tidy(); the audio thread
/// calls render(). render() never allocates, locks or waits, and the control
/// calls never wait for the audio thread: a loaded song is built on the control
/// thread and handed over through an atomic pointer, and the transport is a set
/// of atomics the audio thread reads at the start of each render().
class Player final : public AudioSource {
public:
    /// `maxBlockFrames` sizes the mixing buffers; render() splits longer blocks.
    explicit Player(double sampleRate, std::size_t maxBlockFrames = 4096);
    /// The audio thread must no longer call render().
    ~Player() override;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// Prepares a song for playback (synths, events in frames) and hands it to
    /// the audio thread, which takes it at its next render() and goes on from
    /// the same position. Notes of the previous song stop at once.
    void load(const core::Song& song, const Sequence& sequence);

    /// Starts or resumes playback; from the start when it stopped at the end.
    void play() noexcept;

    /// Holds the position; sounding notes are released and ring out.
    void pause() noexcept;

    /// Takes every track's mix (ADR-0020) from `song`, the song last loaded
    /// with its mix since changed, without loading it again: the audio thread
    /// moves to the new levels over its next block. Does nothing when `song`
    /// does not have the loaded song's number of tracks.
    void updateMix(const core::Song& song) noexcept;

    /// Moves to `seconds` (clamped to the song), silencing what sounds.
    /// Notes that started earlier are not struck again.
    void seek(double seconds) noexcept;

    /// True from play() until pause() or the end of the song.
    [[nodiscard]] bool isPlaying() const noexcept;

    /// Seconds from the start, as far as the audio thread has rendered (or the
    /// position asked for by seek(), until the audio thread has taken it).
    [[nodiscard]] double position() const noexcept;

    /// Seconds of the song last loaded.
    [[nodiscard]] double duration() const noexcept { return duration_; }

    /// Frees songs the audio thread no longer uses. load() calls it too.
    void tidy() noexcept;

    /// Audio thread: writes the next frames to `left` and `right`, which have
    /// the same size.
    void render(std::span<float> left, std::span<float> right) noexcept override;

    struct Loaded;

private:
    void renderBlock(std::span<float> left, std::span<float> right) noexcept;

    double sampleRate_;
    std::size_t maxBlockFrames_;
    double duration_ = 0.0; // control thread

    Loaded* latest_ = nullptr;              // control thread: the song last loaded
    std::atomic<Loaded*> pending_{nullptr}; // control -> audio
    std::atomic<Loaded*> retired_{nullptr}; // audio -> control, for tidy()
    std::atomic<bool> playing_{false};
    std::atomic<std::int64_t> seekTo_{-1}; // frame, -1 for none
    std::atomic<std::int64_t> position_{0};

    // Audio thread only.
    Loaded* current_ = nullptr;
    std::int64_t frame_ = 0;
    bool wasPlaying_ = false;
};

} // namespace elara::audio
