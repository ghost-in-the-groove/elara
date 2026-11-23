// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer: a song as it is played, in seconds (ADR-0017).
#pragma once

#include <elara/audio/synth.hpp>

#include <cstddef>
#include <cstdint>
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
    friend bool operator==(const PlayedNote&, const PlayedNote&) = default;
};

enum class EventKind : std::uint8_t { NoteOff, NoteOn };

/// What a synthesizer is told, track by track: a key struck again while it
/// sounds is released and struck again, and released when the last of its
/// notes ends.
struct NoteEvent {
    double time = 0.0; ///< seconds from the start of the performance
    EventKind kind = EventKind::NoteOn;
    Touch touch = Touch::Normal; ///< how a NoteOn strikes its key
    std::size_t track = 0;
    int key = 60;
    int velocity = 80;
    friend bool operator==(const NoteEvent&, const NoteEvent&) = default;
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
    explicit Sequence(const core::Song& song);

    [[nodiscard]] const std::vector<PlayedMeasure>& measures() const noexcept { return measures_; }
    /// Ordered by start, then track, staff, voice and key.
    [[nodiscard]] const std::vector<PlayedNote>& notes() const noexcept { return notes_; }
    /// Ordered by time, releases before attacks, then by track and key.
    [[nodiscard]] const std::vector<NoteEvent>& events() const noexcept { return events_; }
    /// Seconds from the start to the end of the last played measure.
    [[nodiscard]] double duration() const noexcept { return duration_; }

    /// The index of the first event at or after `seconds` (events().size()
    /// when there is none): where playback starting there begins.
    [[nodiscard]] std::size_t eventAt(double seconds) const noexcept;

private:
    std::vector<PlayedMeasure> measures_;
    std::vector<PlayedNote> notes_;
    std::vector<NoteEvent> events_;
    double duration_ = 0.0;
};

} // namespace elara::audio
