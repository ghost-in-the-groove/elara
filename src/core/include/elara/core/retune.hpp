// SPDX-License-Identifier: AGPL-3.0-or-later
// Retuning (ADR-0046): a fretted staff takes another tuning or capo, keeping
// either the pitches its notes sound or the frets its tablature shows.
#pragma once

#include <elara/core/commands.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace elara::core {

/// What a retuned staff keeps.
enum class Retune : std::uint8_t {
    Pitches, ///< the music sounds the same; its notes find strings and frets that play it
    Frets,   ///< the tablature stays; its notes sound what their frets play now
};

/// Staff `staff` of track `track` to take `tuning` with a capo at fret `capo`.
struct Retuning {
    std::size_t track = 0;
    std::size_t staff = 0;
    Tuning tuning = Tuning::standardGuitar();
    int capo = 0;
    Retune keep = Retune::Pitches;
};

/// One undo step, "Change tuning", that retunes as `what` says (ADR-0046), or
/// nullptr when the staff already has that tuning and capo.
///
/// Keeping the pitches, each note keeps its string when its fret there is on
/// the neck, else takes the nearest free string that plays it, as transposing
/// does (a note on a string the tuning lacks too). Keeping the frets, each
/// note sounds its fret on its string's new open pitch above the capo, spelled
/// in the key of its measure; notes on unchanged strings stay as they were.
///
/// Throws std::out_of_range for a missing track or staff, and
/// std::invalid_argument for a staff without strings, a capo outside
/// 0..Tuning::kMaxFret, or a note the new tuning cannot play: no free string
/// for its pitch, or (keeping the frets) its string missing, its fret past the
/// neck or its pitch above MIDI 127.
[[nodiscard]] std::unique_ptr<Command> makeRetune(const Song& song, const Retuning& what);

} // namespace elara::core
