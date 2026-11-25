// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/measure.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

class Staff;

/// The syllables of a verse typed as text (ADR-0084), the way Guitar Pro's
/// lyrics are written:
///
/// - a space or a hyphen ends a syllable; a hyphen also joins it to the next one;
/// - "+" keeps two words on one beat (it reads as a space);
/// - every further space or hyphen in a row leaves a beat blank (nullopt), and
///   so does each one before the first syllable;
/// - a line break separates syllables like a space, but leaves no beat blank;
/// - words in [brackets] are comments ("[Chorus]") and are not sung: with the
///   spaces around them they separate syllables like a line break.
[[nodiscard]] std::vector<std::optional<Syllable>> syllablesOf(std::string_view text);

/// Puts the syllables of `text` (syllablesOf) as verse `verse` (0 for the
/// first) on the beats of `staff`'s first voice from measure `first` on, one
/// per beat with notes: rests and beats whose notes are all tied from the beat
/// before take none. What the verse held from `first` on is replaced. Returns
/// how many syllables found no beat.
std::size_t placeVerse(Staff& staff, std::size_t verse, std::size_t first, std::string_view text);

} // namespace elara::core
