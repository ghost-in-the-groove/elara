// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer: a song as it is played, in seconds (ADR-0017).
#pragma once

#include <elara/audio/synth.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elara::core {
class Song;
}

namespace elara::audio {

/// A written measure as it is played: its index in the song and when it
/// starts and ends, in seconds from the start of the performance.
struct PlayedMeasure {
    std::size_t measure = 0;
    double start = 0.0;
    double end = 0.0;
    friend bool operator==(const PlayedMeasure&, const PlayedMeasure&) = default;
};

/// A note as it is played, in seconds from the start of the performance,
/// with its articulations applied (ADR-0022): its key is the pitch it sounds
/// (a harmonic's partial), its velocity includes accents and ghosting, its end
/// staccato and let ring, and its touch says how the string is struck.
struct PlayedNote {
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t voice = 0;
    std::size_t pass = 0; ///< index in measures() of the played measure of its attack
    double start = 0.0;
    double end = 0.0;
    int key = 60; ///< sounding MIDI key; percussion: the General MIDI percussion key
    int velocity = 80;
    Touch touch = Touch::Normal;
    /// False when it continues the previous note on its string without a new
    /// attack: a hammer-on, a pull-off or the end of a legato slide (ADR-0023).
    bool struck = true;
    friend bool operator==(const PlayedNote&, const PlayedNote&) = default;
};

/// NoteOff and NoteOn release and strike a key; Pitch moves the pitch of the
/// voice struck as a key (ADR-0023).
enum class EventKind : std::uint8_t { NoteOff, NoteOn, Pitch };

/// What a synthesizer is told, track by track: a key struck again while it
/// sounds is released and struck again, and released when the last of its
/// notes ends. A Pitch event moves the voice struck as `key` to `cents` above
/// that key over `glide` seconds (Synth::glide()): bends, slides, vibrato and
/// the notes joined to it by hammer-ons and pull-offs.
struct NoteEvent {
    double time = 0.0; ///< seconds from the start of the performance
    EventKind kind = EventKind::NoteOn;
    Touch touch = Touch::Normal; ///< how a NoteOn strikes its key
    std::int16_t cents = 0;      ///< Pitch: the offset to reach
    float glide = 0.0f;          ///< Pitch: seconds to reach it; 0 at once
    std::size_t track = 0;
    int key = 60;
    int velocity = 80;
    friend bool operator==(const NoteEvent&, const NoteEvent&) = default;
};

/// The playback speed's range: a quarter of the written tempo to twice it.
inline constexpr double kMinSpeed = 0.25;
inline constexpr double kMaxSpeed = 2.0;

/// The metronome's keys (General MIDI percussion): a high wood block on the
/// first beat of a measure, a low one on the others.
inline constexpr int kMetronomeAccentKey = 76;
inline constexpr int kMetronomeKey = 77;

/// How a song is played for practice (ADR-0024).
struct SequenceOptions {
    /// Times every written tempo, from kMinSpeed to kMaxSpeed (clamped; 1
    /// when it is not a number). Effects timed in seconds, such as vibrato
    /// and slides, keep their pace.
    double speed = 1.0;
    /// A click on every beat, as events on Sequence::metronomeTrack().
    bool metronome = false;
};

/// The clicks before playback starts (ADR-0024): `clicks` of them,
/// `interval` seconds apart, the first one higher.
struct CountIn {
    int clicks = 0;
    double interval = 0.0;
    friend bool operator==(const CountIn&, const CountIn&) = default;
};

/// A song as it is played: its measures in play order (repeats unrolled,
/// core::Song::playOrder()), its notes and the note events for a
/// synthesizer, all in seconds. Every measure plays at the tempo written in
/// it, and a note lasts its length in whole notes over the measures played
/// after its attack.
///
/// Built once, away from the audio thread; afterwards it is only read, so the
/// audio thread can read it without locks.
class Sequence {
public:
    explicit Sequence(const core::Song& song, SequenceOptions options = {});

    /// The speed played at, as clamped from the options.
    [[nodiscard]] double speed() const noexcept { return speed_; }
    /// Whether the metronome's clicks are among the events.
    [[nodiscard]] bool metronome() const noexcept { return metronome_; }
    /// The track the metronome's events are on: one past the song's last.
    [[nodiscard]] std::size_t metronomeTrack() const noexcept { return metronomeTrack_; }

    [[nodiscard]] const std::vector<PlayedMeasure>& measures() const noexcept { return measures_; }
    /// Ordered by start, then track, staff, voice and key.
    [[nodiscard]] const std::vector<PlayedNote>& notes() const noexcept { return notes_; }
    /// Ordered by time, releases before attacks before pitch changes, then by
    /// track and key.
    [[nodiscard]] const std::vector<NoteEvent>& events() const noexcept { return events_; }
    /// Seconds from the start to the end of the last played measure.
    [[nodiscard]] double duration() const noexcept { return duration_; }

    /// The index of the first event at or after `seconds` (events().size()
    /// when there is none): where playback starting there begins.
    [[nodiscard]] std::size_t eventAt(double seconds) const noexcept;

    /// A count-in for playback from `seconds`: the beats of one measure of the
    /// measure played there (its time signature's pulse), at the tempo there.
    /// Before the start, the first measure's; after the end, the last one's.
    /// No clicks when there is no measure.
    [[nodiscard]] CountIn countInAt(double seconds) const noexcept;

    /// The index in measures() of the measure played at `seconds`: the first
    /// one for earlier times, the last one at its very end, and
    /// measures().size() after that or when there is none.
    [[nodiscard]] std::size_t playedMeasureAt(double seconds) const noexcept;

    /// The time `fraction` of the way through written measure `measure` (0 at
    /// its start, 1 at its end, clamped) the first time it is played; nothing
    /// when it never is.
    [[nodiscard]] std::optional<double> secondsAt(std::size_t measure,
                                                  double fraction) const noexcept;

    /// Equal when they play the same: a song whose mix or title changed plays
    /// as before, so playback can carry on (ADR-0039).
    friend bool operator==(const Sequence&, const Sequence&) = default;

private:
    // A tempo in played time: from `time` on, seconds per whole note.
    struct TempoPoint {
        double time = 0.0;
        double perWhole = 0.0;
        friend bool operator==(const TempoPoint&, const TempoPoint&) = default;
    };
    // The beats of a played measure: `count` of `length` whole notes.
    struct Pulse {
        double length = 0.0;
        int count = 0;
        friend bool operator==(const Pulse&, const Pulse&) = default;
    };

    std::vector<PlayedMeasure> measures_;
    std::vector<PlayedNote> notes_;
    std::vector<NoteEvent> events_;
    double duration_ = 0.0;
    double speed_ = 1.0;
    bool metronome_ = false;
    std::size_t metronomeTrack_ = 0;
    std::vector<TempoPoint> tempi_; // ordered by time
    std::vector<Pulse> pulses_;     // one per played measure
};

} // namespace elara::audio
