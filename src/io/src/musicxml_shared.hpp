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

/// The MusicXML mime type of a compressed .mxl archive and of its score.
inline constexpr std::string_view kMxlMimeType = "application/vnd.recordare.musicxml";
inline constexpr std::string_view kMusicXmlMediaType = "application/vnd.recordare.musicxml+xml";

/// SMuFL glyph of a wide vibrato line (MusicXML 4.0 `wavy-line smufl`); a plain
/// one is "guitarVibratoStroke".
inline constexpr std::string_view kWideVibratoGlyph = "guitarWideVibratoStroke";
inline constexpr std::string_view kVibratoGlyph = "guitarVibratoStroke";

} // namespace elara::io::detail
