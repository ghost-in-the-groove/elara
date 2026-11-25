// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/measure.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elara::core {

class Staff;

/// The verses a staff sings at most: what MusicXML numbers in practice, and
/// more than songbooks print (ADR-0084).
inline constexpr std::size_t kMaxVerses = 16;

/// The syllables of a verse typed as text (ADR-0084), the way Guitar Pro's
/// lyrics are written:
///
/// - a space or a hyphen ends a syllable; a hyphen also joins it to the next one;
/// - "+" keeps two words on one beat (it reads as a space);
/// - every further space or hyphen in a row leaves a beat blank (nullopt), and
///   so does each one before the first syllable;
/// - a line break separates syllables like a space, but leaves no beat blank;
/// - words in [brackets] are comments ("[Chorus]") and are not sung: with the
///   spaces around them they separate syllables like a line break;
/// - an underscore after a syllable holds it over the notes after it (Elara's
///   addition, for typing: "love_ me", "be_-low").
[[nodiscard]] std::vector<std::optional<Syllable>> syllablesOf(std::string_view text);

/// Puts the syllables of `text` (syllablesOf) as verse `verse` (0 for the
/// first) on the beats of `staff`'s first voice from measure `first` on, one
/// per beat with notes: rests and beats whose notes are all tied from the beat
/// before take none. What the verse held from `first` on is replaced. Returns
/// how many syllables found no beat.
std::size_t placeVerse(Staff& staff, std::size_t verse, std::size_t first, std::string_view text);

/// How many verses `staff` sings: one more than the last verse with a
/// syllable on any of its beats, 0 without lyrics.
[[nodiscard]] std::size_t verseCount(const Staff& staff);

/// A verse as typed: the measure it starts at and its text (syllablesOf).
struct VerseText {
    std::size_t first = 0;
    std::string text{};

    friend bool operator==(const VerseText&, const VerseText&) = default;
};

/// Verse `verse` of `staff`'s first voice as typed: from the measure of its
/// first syllable, the text placeVerse() puts back as it is, a space or a
/// hyphen after each syllable, one more space for each beat left blank, "+"
/// for a space inside a syllable and "_" after a held one. Measure 0 and no
/// text when the verse sings nothing there; syllables on rests and tied notes
/// are not part of it.
[[nodiscard]] VerseText verseText(const Staff& staff, std::size_t verse);

} // namespace elara::core
