// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: MusicXML vocabulary shared by the reader (ADR-0007) and the writer (ADR-0008).
#pragma once

#include <elara/core/signature.hpp>

#include <array>
#include <string_view>

namespace elara::io::detail {

/// MusicXML <type> values the model has, index = core::NoteValue.
inline constexpr std::array<std::string_view, 9> kTypeNames{
    "breve", "whole", "half", "quarter", "eighth", "16th", "32nd", "64th", "128th"};

/// Semitones above C of each core::Step.
inline constexpr std::array<int, 7> kStepSemitones{0, 2, 4, 5, 7, 9, 11};

/// MIDI number of a spelled note; may fall outside 0..127 (written pitches).
[[nodiscard]] constexpr int midiOf(int step, int alter, int octave) noexcept {
    return (octave + 1) * 12 + kStepSemitones[static_cast<std::size_t>(step)] + alter;
}

/// The key a transposition by `diatonic` steps and `chromatic` semitones turns
/// `fifths` into: the tonic moves by that interval (a major second up adds two
/// sharps, a diminished fifth up six flats). A key beyond seven accidentals is
/// spelled enharmonically.
[[nodiscard]] constexpr int transposedFifths(int fifths, int chromatic, int diatonic) noexcept {
    int result = fifths + chromatic * 7 - diatonic * 12;
    while (result > core::KeySignature::kMaxFifths) {
        result -= 12;
    }
    while (result < -core::KeySignature::kMaxFifths) {
        result += 12;
    }
    return result;
}

/// A MusicXML <kind> of chord and the suffix a chord name writes for it
/// (ADR-0080): "minor-seventh" is the "m7" of "Am7".
struct ChordKind {
    std::string_view kind;
    std::string_view suffix;
};

inline constexpr std::array<ChordKind, 31> kChordKinds{{
    {"major", ""},
    {"minor", "m"},
    {"augmented", "+"},
    {"diminished", "dim"},
    {"dominant", "7"},
    {"major-seventh", "maj7"},
    {"minor-seventh", "m7"},
    {"diminished-seventh", "dim7"},
    {"augmented-seventh", "+7"},
    {"half-diminished", "m7b5"},
    {"major-minor", "m(maj7)"},
    {"major-sixth", "6"},
    {"minor-sixth", "m6"},
    {"dominant-ninth", "9"},
    {"major-ninth", "maj9"},
    {"minor-ninth", "m9"},
    {"dominant-11th", "11"},
    {"major-11th", "maj11"},
    {"minor-11th", "m11"},
    {"dominant-13th", "13"},
    {"major-13th", "maj13"},
    {"minor-13th", "m13"},
    {"suspended-second", "sus2"},
    {"suspended-fourth", "sus4"},
    {"power", "5"},
    {"pedal", "ped"},
    {"Neapolitan", "N6"},
    {"Italian", "It+6"},
    {"French", "Fr+6"},
    {"German", "Ger+6"},
    {"Tristan", "Tristan"},
}};

/// The MusicXML mime type of a compressed .mxl archive and of its score.
inline constexpr std::string_view kMxlMimeType = "application/vnd.recordare.musicxml";
inline constexpr std::string_view kMusicXmlMediaType = "application/vnd.recordare.musicxml+xml";

/// SMuFL glyph of a wide vibrato line (MusicXML 4.0 `wavy-line smufl`); a plain
/// one is "guitarVibratoStroke".
inline constexpr std::string_view kWideVibratoGlyph = "guitarWideVibratoStroke";
inline constexpr std::string_view kVibratoGlyph = "guitarVibratoStroke";

} // namespace elara::io::detail
