// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: MusicXML vocabulary shared by the reader (ADR-0007) and the writer (ADR-0008).
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/signature.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
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

// ---- Marks ----------------------------------------------------------------------
// One table per kind of mark pairs each MusicXML element with what it means in
// the model. The writer writes a table's marks in its order and the reader
// takes them back; aliases are other names for the same marks that only the
// reader accepts.

/// A MusicXML element and the model's value for it.
template <typename Value>
struct Named {
    std::string_view name;
    Value value;
};

/// The value `name` stands for in `table`; nullopt when it is not there.
template <typename Value, std::size_t N>
[[nodiscard]] constexpr std::optional<Value> valueNamed(const std::array<Named<Value>, N>& table,
                                                        std::string_view name) noexcept {
    const auto found = std::ranges::find(table, name, &Named<Value>::name);
    return found == table.end() ? std::nullopt : std::optional<Value>(found->value);
}

/// The element `table` names `value` with; empty when it is not there.
template <typename Value, std::size_t N>
[[nodiscard]] constexpr std::string_view nameOf(const std::array<Named<Value>, N>& table,
                                                Value value) noexcept {
    const auto found = std::ranges::find(table, value, &Named<Value>::value);
    return found == table.end() ? std::string_view() : found->name;
}

/// A mark in <articulations> and what it gives its note, or the beat it ends
/// for a breath mark or a caesura (ADR-0095). Each mark the writer writes sets
/// one thing; an alias may set more.
struct ArticulationMark {
    std::string_view name;
    bool staccato = false;
    bool staccatissimo = false; // ADR-0094
    bool tenuto = false;        // ADR-0088
    core::Accent accent = core::Accent::None;
    core::SlideIn slideIn = core::SlideIn::None;    // a slide into the note
    core::SlideOut slideOut = core::SlideOut::None; // a slide off it
    core::Breath breath = core::Breath::None;       // after the beat
};

inline constexpr std::array<ArticulationMark, 11> kArticulations{{
    {.name = "accent", .accent = core::Accent::Accent},
    {.name = "strong-accent", .accent = core::Accent::Marcato},
    {.name = "staccato", .staccato = true},
    {.name = "staccatissimo", .staccatissimo = true},
    {.name = "tenuto", .tenuto = true},
    {.name = "scoop", .slideIn = core::SlideIn::FromBelow},
    {.name = "plop", .slideIn = core::SlideIn::FromAbove},
    {.name = "doit", .slideOut = core::SlideOut::Upwards},
    {.name = "falloff", .slideOut = core::SlideOut::Downwards},
    {.name = "breath-mark", .breath = core::Breath::Comma},
    {.name = "caesura", .breath = core::Breath::Caesura},
}};

/// Read only: a spiccato is a staccato, a detached legato (portato) a staccato
/// with a tenuto (ADR-0088).
inline constexpr std::array<ArticulationMark, 2> kArticulationAliases{{
    {.name = "spiccato", .staccato = true},
    {.name = "detached-legato", .staccato = true, .tenuto = true},
}};

/// The articulation the reader takes `name` for, aliases included; nullptr for
/// one the model does not have.
[[nodiscard]] constexpr const ArticulationMark* articulationNamed(std::string_view name) noexcept {
    for (const ArticulationMark& mark : kArticulations) {
        if (mark.name == name) {
            return &mark;
        }
    }
    for (const ArticulationMark& mark : kArticulationAliases) {
        if (mark.name == name) {
            return &mark;
        }
    }
    return nullptr;
}

/// Whether a note, on a beat with `breath` after it, has `mark`.
[[nodiscard]] inline bool hasMark(const ArticulationMark& mark, const core::NoteEffects& fx,
                                  core::Breath breath) noexcept {
    return (!mark.staccato || fx.staccato) && (!mark.staccatissimo || fx.staccatissimo) &&
           (!mark.tenuto || fx.tenuto) &&
           (mark.accent == core::Accent::None || fx.accent == mark.accent) &&
           (mark.slideIn == core::SlideIn::None || fx.slideIn == mark.slideIn) &&
           (mark.slideOut == core::SlideOut::None || fx.slideOut == mark.slideOut) &&
           (mark.breath == core::Breath::None || breath == mark.breath);
}

/// Gives a note, and the breath after its beat, `mark`: a strong accent wins
/// over an accent and a caesura over a breath mark, in any order; a later
/// slide replaces an earlier one.
inline void addMark(const ArticulationMark& mark, core::NoteEffects& fx,
                    core::Breath& breath) noexcept {
    fx.staccato = fx.staccato || mark.staccato;
    fx.staccatissimo = fx.staccatissimo || mark.staccatissimo;
    fx.tenuto = fx.tenuto || mark.tenuto;
    fx.accent = std::max(fx.accent, mark.accent);
    if (mark.slideIn != core::SlideIn::None) {
        fx.slideIn = mark.slideIn;
    }
    if (mark.slideOut != core::SlideOut::None) {
        fx.slideOut = mark.slideOut;
    }
    breath = std::max(breath, mark.breath);
}

/// The marks in <ornaments> that are a note's ornament (ADR-0091), MusicXML's
/// inverted mordent being the upper one. Delayed and vertical turns are none
/// of them.
inline constexpr std::array<Named<core::Ornament>, 4> kOrnaments{{
    {"mordent", core::Ornament::Mordent},
    {"inverted-mordent", core::Ornament::UpperMordent},
    {"turn", core::Ornament::Turn},
    {"inverted-turn", core::Ornament::InvertedTurn},
}};

/// Pick strokes, the bowing marks of <technical> (ADR-0008).
inline constexpr std::array<Named<core::PickStroke>, 2> kPickStrokes{{
    {"down-bow", core::PickStroke::Down},
    {"up-bow", core::PickStroke::Up},
}};

/// Tremolo picking (ADR-0025) as the strokes of a single <tremolo> through the
/// stem: one for eighths, two for sixteenths, three for thirty-seconds
/// (index = strokes - 1).
inline constexpr std::array<core::NoteValue, 3> kTremoloSpeeds{
    core::NoteValue::Eighth, core::NoteValue::Sixteenth, core::NoteValue::ThirtySecond};

/// The speed `strokes` write, fewer than one counting as one and more than
/// three as three.
[[nodiscard]] constexpr core::NoteValue tremoloSpeed(int strokes) noexcept {
    const int count = std::clamp(strokes, 1, static_cast<int>(kTremoloSpeeds.size()));
    return kTremoloSpeeds[static_cast<std::size_t>(count - 1)];
}

/// The strokes of `speed`, or of the nearest speed strokes write when it is
/// slower than eighths or faster than thirty-seconds.
[[nodiscard]] constexpr int tremoloStrokes(core::NoteValue speed) noexcept {
    if (const auto found = std::ranges::find(kTremoloSpeeds, speed);
        found != kTremoloSpeeds.end()) {
        return static_cast<int>(found - kTremoloSpeeds.begin()) + 1;
    }
    return speed < kTremoloSpeeds.front() ? 1 : static_cast<int>(kTremoloSpeeds.size());
}

/// The marks in <dynamics> of the model's levels (ADR-0044), in core::Dynamic
/// order.
inline constexpr std::array<Named<core::Dynamic>, 8> kDynamics{{
    {"ppp", core::Dynamic::PPP},
    {"pp", core::Dynamic::PP},
    {"p", core::Dynamic::P},
    {"mp", core::Dynamic::MP},
    {"mf", core::Dynamic::MF},
    {"f", core::Dynamic::F},
    {"ff", core::Dynamic::FF},
    {"fff", core::Dynamic::FFF},
}};

/// The level a mark in <dynamics> sets. Read only, softer than ppp ("pppp"...)
/// is ppp and louder than fff fff. Nullopt for sf, sfz, fp...: accents rather
/// than levels.
[[nodiscard]] constexpr std::optional<core::Dynamic> dynamicNamed(std::string_view name) noexcept {
    if (name.starts_with("ppp")) {
        return core::Dynamic::PPP;
    }
    if (name.starts_with("fff")) {
        return core::Dynamic::FFF;
    }
    return valueNamed(kDynamics, name);
}

} // namespace elara::io::detail
