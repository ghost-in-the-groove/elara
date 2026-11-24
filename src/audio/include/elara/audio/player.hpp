// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback (ADR-0019), through a sound bank (ADR-0043).
#pragma once

#include <elara/audio/audio_source.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace elara::core {
class Song;
struct TrackMix;
} // namespace elara::core

namespace elara::audio {

class Sequence;
class SoundBank;
struct CountIn;

/// Plays a sequence in real time through one sound per track, from the
/// built-in synthesizer or another sound bank (setSoundBank()).
///
/// Two threads use a player. The control thread (the main thread) constructs
/// it and calls load(), play(), pause(), seek() and tidy(); the audio thread
/// calls render(). render() never allocates, locks or waits, and the control
/// calls never wait for the audio thread: a loaded song is built on the control
/// thread and handed over through an atomic pointer, and the transport is a set
/// of atomics the audio thread reads at the start of each render().
///
/// For practice (ADR-0024) it counts in before playback starts and loops a
/// passage.
class Player final : public AudioSource {
public:
    /// `maxBlockFrames` sizes the mixing buffers; render() splits longer blocks.
    explicit Player(double sampleRate, std::size_t maxBlockFrames = 4096);
    /// The audio thread must no longer call render().
    ~Player() override;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// Prepares a song for playback (sounds, events in frames) and hands it to
    /// the audio thread, which takes it at its next render() and goes on from
    /// the same position. Notes of the previous song stop at once. A track
    /// whose instrument (kind and program) has not changed since the last
    /// load keeps its sound, so reloading after an edit reads no samples.
    void load(const core::Song& song, const Sequence& sequence);

    /// Where the tracks of the next load() get their sound; the built-in
    /// synthesizer when null, as at first. The metronome and the count-in
    /// always click on the built-in drums.
    void setSoundBank(std::shared_ptr<const SoundBank> bank);

    /// Starts or resumes playback; from the start when it stopped at the end.
    void play() noexcept;

    /// The same, after the clicks of `countIn` (Sequence::countInAt()), on
    /// wood blocks like the metronome. The position holds still while they
    /// click. Ignored while playing; a pause ends the count-in.
    void play(const CountIn& countIn) noexcept;

    /// True from a play() with a count-in until its clicks are over.
    [[nodiscard]] bool isCountingIn() const noexcept;

    /// Loops from `from` to `to` seconds (clamped to the song): playback that
    /// reaches `to` goes on from `from`, silencing what sounds as a seek does.
    /// Playback that starts after `to` runs on to the end. `to <= from` ends
    /// the loop.
    void setLoop(double from, double to) noexcept;

    /// Holds the position; sounding notes are released and ring out.
    void pause() noexcept;

    /// Takes every track's mix (ADR-0020) from `song`, the song last loaded
    /// with its mix since changed, without loading it again: the audio thread
    /// moves to the new levels over its next block. Does nothing when `song`
    /// does not have the loaded song's number of tracks.
    void updateMix(const core::Song& song) noexcept;
    /// The same from the tracks' mixes alone, in track order: what a mixer
    /// passes while a fader moves, without copying the song for every step.
    void updateMix(std::span<const core::TrackMix> mixes) noexcept;

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
    struct KeptSound;

private:
    void renderBlock(std::span<float> left, std::span<float> right) noexcept;
    void start(int clicks, std::int64_t interval) noexcept;

    double sampleRate_;
    std::size_t maxBlockFrames_;
    double duration_ = 0.0;                 // control thread
    std::shared_ptr<const SoundBank> bank_; // control thread
    std::vector<KeptSound> kept_;           // control thread: each track's sound

    Loaded* latest_ = nullptr;              // control thread: the song last loaded
    std::atomic<Loaded*> pending_{nullptr}; // control -> audio
    std::atomic<Loaded*> retired_{nullptr}; // audio -> control, for tidy()
    std::atomic<bool> playing_{false};
    std::atomic<std::int64_t> seekTo_{-1}; // frame, -1 for none
    std::atomic<std::int64_t> position_{0};
    // The count-in of the next start (written before playing_), and whether
    // one clicks now.
    std::atomic<int> countInClicks_{0};
    std::atomic<std::int64_t> countInInterval_{0}; // frames
    std::atomic<bool> countingIn_{false};
    // The loop as two 32-bit frames, from in the high half; 0 for none.
    std::atomic<std::uint64_t> loop_{0};

    // Audio thread only.
    Loaded* current_ = nullptr;
    std::int64_t frame_ = 0;
    bool wasPlaying_ = false;
    int clicks_ = 0;              // of the count-in under way
    std::int64_t interval_ = 0;   // frames between its clicks
    std::int64_t countedIn_ = 0;  // frames of it played
    std::int64_t countInEnd_ = 0; // frames it lasts
};

} // namespace elara::audio
