// SPDX-License-Identifier: AGPL-3.0-or-later
// Placing the notes of a fretted staff on strings that play their pitches,
// for transposition (ADR-0026) and pasting (ADR-0027). Private to elara_core.
#pragma once

#include <elara/core/measure.hpp>
#include <elara/core/tuning.hpp>

#include <array>
#include <cstddef>

namespace elara::core::detail {

/// The string each string's last note went to, so a tie or a hammer-on stays
/// with its note; 0 before any note.
using Followed = std::array<int, Tuning::kMaxStrings + 1>;

/// One Followed per voice, carried from measure to measure.
using FollowedByVoice = std::array<Followed, Measure::kMaxVoices>;

/// Gives every note with a fret position in `content` a position on `tuning`
/// (frets counted from `capo`) that plays its pitch. A note keeps its string
/// when its fret there is on the neck. Otherwise it takes the nearest free
/// string in its beat, the higher of two as near; a tied or hammered note
/// first tries the string its note went to. A natural harmonic keeps its
/// node: the same fret on its own or another string, else it becomes an
/// artificial harmonic over a fretted note. Throws std::invalid_argument,
/// naming measure `measure` (0-based), when no free string can play a note.
void placeOnStrings(Measure& content, const Tuning& tuning, int capo, FollowedByVoice& followed,
                    std::size_t measure);

} // namespace elara::core::detail
