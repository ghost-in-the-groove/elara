// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline rendering (ADR-0018), through a sound bank (ADR-0043).
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/audio/sound_bank.hpp>
#include <elara/audio/synth.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace elara::core {
class Song;
}

namespace elara::audio {

struct RenderOptions {
    double sampleRate = 48000.0;
    double tail = 2.0;      ///< seconds after the last measure, for releases to ring out
    unsigned threads = 0;   ///< 0: every core when there is enough work
    bool logEvents = false; ///< record every dispatched event in log()
    /// Where the tracks get their sound; the built-in synthesizer when null.
    /// The metronome always clicks on the built-in drums.
    std::shared_ptr<const SoundBank> bank{};
};

/// A note event as the renderer dispatched it: at a sample frame.
struct LoggedEvent {
    std::int64_t frame = 0;
    EventKind kind = EventKind::NoteOn;
    Touch touch = Touch::Normal;
    std::int16_t cents = 0; ///< Pitch events: the offset to reach
    float glide = 0.0f;     ///< Pitch events: seconds to reach it
    std::size_t track = 0;
    int key = 60;
    int velocity = 80;
    friend bool operator==(const LoggedEvent&, const LoggedEvent&) = default;
};

/// Plays a sequence through one sound per track (RenderOptions::bank) into
/// interleaved stereo, call by call, so a whole song never has to be in memory
/// at once.
/// Each track sits in the stereo mix as the song's track mix says (ADR-0020).
///
/// An event at t seconds is dispatched at frame round(t x rate). Tracks render
/// on separate threads and are mixed in track order, so the output does not
/// depend on the number of threads or on how the frames are split into calls.
class OfflineRenderer {
public:
    /// Takes what it needs from the song (instruments) and the sequence
    /// (events); neither has to outlive the renderer.
    OfflineRenderer(const core::Song& song, const Sequence& sequence, RenderOptions options = {});
    ~OfflineRenderer();
    OfflineRenderer(OfflineRenderer&&) noexcept;
    OfflineRenderer& operator=(OfflineRenderer&&) noexcept;
    OfflineRenderer(const OfflineRenderer&) = delete;
    OfflineRenderer& operator=(const OfflineRenderer&) = delete;

    [[nodiscard]] const RenderOptions& options() const noexcept { return options_; }

    /// Frames from the start to the end of the tail.
    [[nodiscard]] std::int64_t frames() const noexcept { return frames_; }

    /// Frames rendered so far.
    [[nodiscard]] std::int64_t position() const noexcept { return position_; }

    /// The built-in timbre of each track's instrument, in track order.
    [[nodiscard]] const std::vector<Timbre>& timbres() const noexcept { return timbres_; }

    /// The name of the sound each track plays, in track order
    /// (SoundBank::soundName()).
    [[nodiscard]] const std::vector<std::string>& sounds() const noexcept { return sounds_; }

    /// Renders the next frames as interleaved stereo (left, right) into `out`:
    /// out.size() / 2 frames, fewer at the end. Returns the frames written.
    std::size_t render(std::span<float> out);

    /// Renders every frame left, as interleaved stereo.
    [[nodiscard]] std::vector<float> renderRest();

    /// The events dispatched so far, when options().logEvents is set: by frame,
    /// then track, each track's in the order they were dispatched.
    [[nodiscard]] const std::vector<LoggedEvent>& log() const noexcept { return log_; }

    struct TrackPlayer;

private:
    RenderOptions options_;
    std::int64_t frames_ = 0;
    std::int64_t position_ = 0;
    std::vector<float> levels_; // each track's left and right level (ADR-0020)
    std::vector<Timbre> timbres_;
    std::vector<std::string> sounds_;
    std::vector<TrackPlayer> players_;
    std::vector<float> mix_;
    std::vector<LoggedEvent> log_;
};

} // namespace elara::audio
