// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/song.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

/// Keyboard entry (ADR-0021): a caret on a beat and a string, the moves of the
/// arrow keys, and the edits that typing frets, durations and deletes makes.
/// Edits come back as commands, so they go through the undo history (ADR-0005).
namespace elara::core {

/// Where keyboard entry happens: a beat of one voice and, on a fretted staff, a
/// string (1 is the highest); on a percussion staff `string` is a lane, one per
/// drumPieces() entry (1 is the top). `beat` may be one past the last beat of a voice
/// shorter than its measure (an empty voice included): the free slot, where
/// typing adds a beat.
struct Caret {
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t measure = 0;
    std::size_t voice = 0;
    std::size_t beat = 0;
    int string = 1;

    friend bool operator==(const Caret&, const Caret&) = default;
};

/// The nearest caret the song has: every index clamped (so a caret whose beat
/// or measure went away moves back), the beat to the free slot or else the last
/// beat, the string to the staff's strings or drum lanes (1 on any other staff).
/// Throws std::invalid_argument for a song without tracks or measures.
[[nodiscard]] Caret clampCaret(const Song& song, Caret caret);

/// True when the caret is on the free slot after the last beat of its voice.
[[nodiscard]] bool onFreeSlot(const Song& song, const Caret& caret);

enum class CaretMove : std::uint8_t {
    NextBeat,        ///< the next beat, then the free slot, then the next measure
    PreviousBeat,    ///< the reverse of NextBeat
    NextMeasure,     ///< the first beat of the next measure
    PreviousMeasure, ///< the first beat of the measure, then of the previous one
    StringUp,        ///< the next higher string
    StringDown,      ///< the next lower string
};

/// Where `move` takes a caret (clamped first). Moves stop at the ends of the
/// song and of the strings; moving between measures keeps the voice and string.
[[nodiscard]] Caret moveCaret(const Song& song, const Caret& caret, CaretMove move);

/// An edit to execute and where the caret goes after it.
struct Entry {
    std::unique_ptr<Command> command; ///< nullptr when nothing changes
    Caret caret;
};

/// Typing a fret: the note at `fret` (from the capo) on the caret's string,
/// spelled in the measure's key. On a beat it replaces the note on that string,
/// keeping its effects, or joins the chord (a rest becomes a note of the same
/// duration); on the free slot it adds a beat of `duration`. The caret stays.
/// Throws std::logic_error on a staff without a tuning and std::out_of_range for
/// a fret off the neck.
[[nodiscard]] Entry enterFret(const Song& song, const Caret& caret, int fret, Duration duration);

/// The caret's beat gets `duration`; nothing changes on the free slot.
[[nodiscard]] Entry setBeatDuration(const Song& song, const Caret& caret, Duration duration);

/// Delete: removes the note on the caret's string, leaving a rest when it was
/// the last one. On a rest it removes the beat, and the caret stays at its index
/// (now the next beat or the free slot). Nothing changes on the free slot or on
/// a string without a note.
[[nodiscard]] Entry deleteAtCaret(const Song& song, const Caret& caret);

/// Removes the caret's beat with all its notes; the caret stays at its index.
[[nodiscard]] Entry deleteBeat(const Song& song, const Caret& caret);

/// Insert: a rest of `duration` before the caret's beat (or on the free slot);
/// the caret goes to it.
[[nodiscard]] Entry insertRest(const Song& song, const Caret& caret, Duration duration);

/// Drum entry (ADR-0035): toggles the kit piece of the caret's lane on its beat.
/// Absent, it joins the beat's hits in lane order (a rest becomes a hit of the
/// same duration; the free slot gets a beat of `duration`); present, it goes,
/// leaving a rest when it was the last. The caret stays. Throws
/// std::logic_error on a staff that is not a percussion staff.
[[nodiscard]] Entry toggleDrum(const Song& song, const Caret& caret, Duration duration);

} // namespace elara::core
