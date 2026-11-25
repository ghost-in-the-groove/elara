// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>

#include "drum_map.hpp"
#include "layout_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation::detail {

namespace {

using core::Beat;
using core::Clef;
using core::Fraction;
using core::KeySignature;
using core::NoteValue;
using core::Pitch;
using core::Step;
using core::TimeSignature;

// ---- Engraving constants, in staff spaces (ADR-0012) ------------------------

constexpr double kKeyAccidentalGap = 0.12; // between the accidentals of a key signature
constexpr double kAccidentalGap = 0.2;     // between an accidental and its notehead
constexpr double kAccidentalColumnGap = 0.12;
constexpr double kDotGap = 0.3; // between a notehead and its first dot
constexpr double kDotSeparation = 0.25;
constexpr double kStemLength = 3.5;
constexpr double kExtraFlagStem = 0.75; // per flag beyond the second
constexpr double kBeamExtraStem = 0.5;  // per beam beyond the first
// Between tremolo slashes and the notehead, tip, flag or beam (ADR-0101), and
// the share of a flag's height the slashes keep clear of.
constexpr double kTremoloGap = 0.4;
constexpr double kTremoloFlagShare = 0.9;
constexpr double kMaxBeamSlant = 1.0;
constexpr double kVoiceRestShift = 2.0;
constexpr double kMiddleLine = 2.0;
constexpr int kMiddleStep = 4;
constexpr int kAccidentalClearance = 6; // steps two accidentals in one column keep apart
constexpr double kStaffHeight = 4.0;
constexpr double kTieOffset = 0.35; // tie ends off the notehead's centre
constexpr double kTieGap = 0.12;    // between a tie end and its notehead
constexpr double kTieRise = 0.1;    // tie height per staff space of length
constexpr double kTieMinHeight = 0.35;
constexpr double kTieMaxHeight = 0.8;
constexpr double kSlurGap = 0.3;       // between a slur end and its notehead or stem
constexpr double kSlurClearance = 0.4; // between a slur and the notes under it
constexpr double kSlurRise = 0.08;     // slur height per staff space of length
constexpr double kSlurMinHeight = 0.5;
constexpr double kSlurNaturalHeight = 1.5; // the most it rises unless notes are in the way
constexpr double kSlurMaxHeight = 2.5;     // beyond it the whole slur moves away instead
constexpr double kSlurLineGap = 0.4;       // between a slur cut by a line and its edge
constexpr double kGraceSlurHeight = 0.35;  // a slur from a grace note to its chord
// Slides (ADR-0098): the gap between a line and its noteheads or accidental,
// how far a slide into or out of a note goes across and up or down, how far a
// line cut at a system break goes up or down, and their thickness.
constexpr double kSlideGap = 0.2;
constexpr double kSlideOff = 1.2;
constexpr double kSlideDrop = 0.75;
constexpr double kSlideBreak = 0.75;
constexpr double kSlideThickness = 0.12;
constexpr double kGraceSlideGap = 0.9;    // between a grace note sliding to its chord and the chord
constexpr double kParenthesisGap = 0.08;  // between a ghost note's notehead and its parentheses
constexpr double kClefChangeScale = 0.75; // a clef change against the line's clef (ADR-0087)
constexpr double kClefChangeGap = 0.5;    // between a clef change and the barline after it
constexpr double kArticulationGap = 0.3;  // between a notehead and its articulation
constexpr double kRunEndGap = 0.3;        // a technique run ends before the next column
constexpr double kOctaveGap = 0.5;        // between an octave line and the ink it clears (ADR-0089)
constexpr double kOctaveLineGap = 0.3;    // between an octave line's mark and its dashes
constexpr double kOctaveDash = 0.4;       // dashes and gaps of an octave line
constexpr double kOctaveMinLine = 1.0;    // the dashes of a line over a single short note
constexpr double kPedalGap = 0.5;         // between the pedal and the ink above it (ADR-0092)
constexpr double kPedalMinLine = 0.5;     // the line of a pedalling over a single short note
constexpr double kPedalHook = 1.0;        // the hook up at its end
constexpr double kPedalNotchHeight = 0.8;
constexpr double kTrillGap = 0.5;     // between a trill and the ink it clears (ADR-0090)
constexpr double kTrillWaveGap = 0.2; // between a trill's "tr" and its wavy line
constexpr double kTrillEndGap = 0.5;  // a wavy line ends before the next column

// A rolled chord's wavy line (ADR-0076): half waves kArpeggioWave long whose
// control points stand kArpeggioSwing off the line, so that it swings about
// 0.2 staff spaces either side, from half a space above the top notehead to
// half a space below the bottom one.
constexpr double kArpeggioGap = 0.25;   // between the line's room and the chord
constexpr double kArpeggioWidth = 0.6;  // room the line and its arrowhead take
constexpr double kArpeggioWave = 0.5;   // length of a half wave
constexpr double kArpeggioSwing = 0.27; // control points off the line
constexpr double kArpeggioThickness = 0.12;
constexpr double kArpeggioOverhang = 0.5;  // beyond the outer noteheads' centres
constexpr double kArpeggioArrow = 0.6;     // length of a downward roll's arrowhead
constexpr double kArpeggioMinLength = 2.0; // a single note's line, centred on it
constexpr double kGraceGap = 0.35;         // between grace notes, and before their chord
// An acciaccatura's slash, at full size: its centre down the stem from the
// tip and off the stem, and half its run and rise.
constexpr double kSlashDrop = 1.1;
constexpr double kSlashOff = 0.2;
constexpr double kSlashRun = 0.6;
constexpr double kSlashRise = 0.5;

// ---- Staff positions ----------------------------------------------------------

constexpr int kTrebleTopLine = 38; // F5 as octave * 7 + step

int diatonicIndex(Step step, int octave) {
    return octave * 7 + static_cast<int>(step);
}

int diatonicIndex(const Pitch& pitch) {
    return diatonicIndex(pitch.step(), pitch.octave());
}

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int topLineIndex(Clef clef) {
    switch (clef) {
    case Clef::Bass:
    case Clef::Bass8vb:
        return 26; // A3
    case Clef::Alto:
        return 32; // G4
    case Clef::Tenor:
        return 30; // E4
    case Clef::Treble:
    case Clef::Treble8vb:
    case Clef::Percussion:
        break;
    }
    return kTrebleTopLine;
}

double yOf(int step) {
    return step * 0.5;
}

// A clef's glyph and the y of the line it sits on.
struct ClefGlyph {
    Glyph glyph = Glyph::GClef;
    double y = 3.0;
};

ClefGlyph clefGlyph(Clef clef) {
    switch (clef) {
    case Clef::Treble:
        break;
    case Clef::Treble8vb:
        return {.glyph = Glyph::GClef8vb, .y = 3.0};
    case Clef::Bass:
        return {.glyph = Glyph::FClef, .y = 1.0};
    case Clef::Bass8vb:
        return {.glyph = Glyph::FClef8vb, .y = 1.0};
    case Clef::Alto:
        return {.glyph = Glyph::CClef, .y = 2.0};
    case Clef::Tenor:
        return {.glyph = Glyph::CClef, .y = 1.0};
    case Clef::Percussion:
        return {.glyph = Glyph::PercussionClef, .y = 2.0};
    }
    return {};
}

// The written pitch: the sounding pitch moved up by `offset` semitones, spelled
// with the interval those semitones usually make (a major second for 2).
Pitch writtenPitch(const Pitch& sounding, int offset) {
    if (offset == 0) {
        return sounding;
    }
    const int midi = sounding.midiNumber() + offset;
    if (midi < 0 || midi > 127) {
        return sounding;
    }
    const int octaves = floorDiv(offset, 12);
    const int chromatic = offset - 12 * octaves;
    const int steps = static_cast<int>(std::lround(chromatic * 7.0 / 12.0)) + 7 * octaves;
    const int target = diatonicIndex(sounding) + steps;
    const int octave = floorDiv(target, 7);
    const auto step = static_cast<Step>(target - 7 * octave);
    constexpr std::array<int, 7> kNatural{0, 2, 4, 5, 7, 9, 11};
    const int alter = midi - (kNatural[static_cast<std::size_t>(step)] + 12 * (octave + 1));
    if (alter < Pitch::kMinAlter || alter > Pitch::kMaxAlter) {
        return Pitch::fromMidi(midi);
    }
    return Pitch(step, alter, octave);
}

// The key the part is written in: the concert key moved by the same interval.
KeySignature writtenKey(const KeySignature& key, int offset) {
    const int chromatic = ((offset % 12) + 12) % 12;
    int delta = (7 * chromatic) % 12;
    if (delta > 6) {
        delta -= 12;
    }
    int fifths = key.fifths() + delta;
    if (fifths > KeySignature::kMaxFifths) {
        fifths -= 12;
    } else if (fifths < -KeySignature::kMaxFifths) {
        fifths += 12;
    }
    return KeySignature(fifths, key.mode());
}

// Steps of the key signature accidentals in the treble clef, in writing order.
constexpr std::array<int, 7> kSharpSteps{0, 3, -1, 2, 5, 1, 4}; // F C G D A E B
constexpr std::array<int, 7> kFlatSteps{4, 1, 5, 2, 6, 3, 7};   // B E A D G C F

// Where the key signature accidental `index` goes in `clef`.
int keyStep(Clef clef, bool sharp, std::size_t index) {
    int shift = ((topLineIndex(clef) - kTrebleTopLine) % 7 + 7) % 7;
    if (shift > 3) {
        shift -= 7;
    }
    int step = (sharp ? kSharpSteps : kFlatSteps)[index] + shift;
    if (step < -1) {
        step += 7;
    } else if (step > 8) {
        step -= 7;
    }
    return step;
}

// ---- Glyph choice ---------------------------------------------------------------

int valueIndex(NoteValue value) {
    return static_cast<int>(value);
}

bool hasStem(NoteValue value) {
    return valueIndex(value) >= valueIndex(NoteValue::Half);
}

Glyph noteheadFor(NoteValue value, detail::DrumHead head) {
    const int shape = value == NoteValue::DoubleWhole || value == NoteValue::Whole ? 0
                      : value == NoteValue::Half                                   ? 1
                                                                                   : 2;
    switch (head) {
    case detail::DrumHead::X:
        return offsetGlyph(Glyph::NoteheadXWhole, shape);
    case detail::DrumHead::Diamond:
        return offsetGlyph(Glyph::NoteheadDiamondWhole, shape);
    case detail::DrumHead::Normal:
        break;
    }
    return value == NoteValue::DoubleWhole ? Glyph::NoteheadDoubleWhole
                                           : offsetGlyph(Glyph::NoteheadWhole, shape);
}

Glyph accidentalFor(int alter) {
    switch (alter) {
    case -2:
        return Glyph::AccidentalDoubleFlat;
    case -1:
        return Glyph::AccidentalFlat;
    case 1:
        return Glyph::AccidentalSharp;
    case 2:
        return Glyph::AccidentalDoubleSharp;
    default:
        return Glyph::AccidentalNatural;
    }
}

Glyph restFor(NoteValue value) {
    return offsetGlyph(Glyph::RestDoubleWhole, valueIndex(value));
}

Glyph flagFor(NoteValue value, bool up) {
    return offsetGlyph(Glyph::Flag8thUp, 2 * (beamCount(value) - 1) + (up ? 0 : 1));
}

double stemLengthFor(NoteValue value) {
    return kStemLength + kExtraFlagStem * std::max(0, beamCount(value) - 2);
}

// ---- Measure contents -------------------------------------------------------------

struct Head {
    int step = 0; // half staff spaces below the top line
    int diatonic = 0;
    int alter = 0;
    bool tied = false;
    bool foot = false; // a drum played with a foot
    Glyph glyph = Glyph::NoteheadBlack;
    std::optional<Glyph> accidental;
    double accidentalX = 0.0; // accidental origin, from the chord's x
    double dx = 0.0;          // notehead origin, from the chord's x
    const core::Note* note = nullptr;
    bool ghost = false; // drawn between parentheses (ADR-0099)
    // The finger of a fretting hand, small left of the notehead (ADR-0093).
    std::optional<Glyph> fingering;
    double fingeringX = 0.0; // its origin, from the chord's x
};

struct Chord {
    const core::Beat* beat = nullptr;
    std::size_t voice = 0;
    Fraction start;
    Fraction length;
    core::Duration duration;
    bool rest = false;
    bool measureRest = false; // a rest filling its voice's measure, centred
    std::vector<Head> heads;  // top to bottom
    bool stemUp = true;
    int beamGroup = -1;
    double headLeft = 0.0;
    double headRight = 0.0;
    // Beside the noteheads, the room the parentheses of its ghost notes take
    // on each side, gap included; 0 without ghost notes (ADR-0099).
    double ghostPad = 0.0;
    double left = 0.0; // ink extents from the chord's x
    double right = 0.0;
    double x = 0.0;
    double stemX = 0.0; // centre of the stem
    double stemStart = 0.0;
    double inkTop = 0.0; // noteheads, accidentals, stem and flag, or the rest
    double inkBottom = 0.0;
    // The grace notes drawn small before it (ADR-0079), each shaped at full
    // size around its own x, which is from this chord's x.
    std::vector<Chord> graces;
    bool slash = false; // a grace note before the beat: its stem is slashed
    // The outer edge of its staccato and accents, on the notehead side.
    std::optional<double> marks;
    double breathX = 0.0; // a breath mark's or a caesura's left, from x (ADR-0095)
};

// What a slur needs of a beat under it (ADR-0086): where its end goes and
// what it has to clear.
struct SlurNote {
    double x = 0.0; // the middle of its noteheads or rest
    double halfWidth = 0.0;
    double stemX = 0.0;
    double headTop = 0.0; // its noteheads and the marks on their side
    double headBottom = 0.0;
    double inkTop = 0.0; // all its ink and marks
    double inkBottom = 0.0;
    bool stemUp = true;
    bool stem = false;
    bool rest = false;
    int voiceSide = 0; // -1: the upper voice of several, 1: a lower one, 0: alone
};

// Whether a note of `notes` is on the string of `note`; notes without a
// string, off a fretted staff, are on any.
[[nodiscard]] bool onStringOf(const core::Note& note, std::span<const core::Note> notes) {
    return std::ranges::any_of(notes, [&](const core::Note& other) {
        return !note.position || !other.position || other.position->string == note.position->string;
    });
}

// Whether a note of `from` goes on legato to a note of `to` on its string
// (ADR-0097): it slides legato into it, or that note is hammered on or
// pulled off from it.
[[nodiscard]] bool legatoBetween(std::span<const core::Note> from, std::span<const core::Note> to) {
    return std::ranges::any_of(from,
                               [&](const core::Note& note) {
                                   return note.effects.slideOut == core::SlideOut::Legato &&
                                          onStringOf(note, to);
                               }) ||
           std::ranges::any_of(to, [&](const core::Note& note) {
               return note.effects.legatoFromPrevious && onStringOf(note, from);
           });
}

// Whether `beat` is slurred to `next`, the beat after it in its voice: marked
// so (ADR-0086), or joined to it by a hammer-on, a pull-off or a legato slide
// (ADR-0097). A beat with grace notes is played legato from them instead, and
// its slur starts at them (graceSlur).
[[nodiscard]] bool slurredTo(const core::Beat& beat, const core::Beat* next) {
    return beat.effects.slur ||
           (next != nullptr && next->graces.empty() && legatoBetween(beat.notes, next->notes));
}

// Whether the notes of `beat` go on legato from its last grace note
// (ADR-0097).
[[nodiscard]] bool legatoFromGrace(const core::Beat& beat) {
    return !beat.graces.empty() && legatoBetween(beat.graces.back().notes, beat.notes);
}

// Whether `note` slides on to the next note on its string (ADR-0098).
[[nodiscard]] bool slidesOn(const core::Note& note) {
    return note.effects.slideOut == core::SlideOut::Shift ||
           note.effects.slideOut == core::SlideOut::Legato;
}

// Of `count` notes, `noteAt(i)` the i-th (null for none), the index of the
// one `from` slides to (ADR-0098): the one on its string, or where strings
// are not known the one nearest in pitch.
[[nodiscard]] std::optional<std::size_t> slideTarget(const core::Note& from, std::size_t count,
                                                     const auto& noteAt) {
    std::optional<std::size_t> nearest;
    int distance = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < count; ++i) {
        const core::Note* to = noteAt(i);
        if (to == nullptr) {
            continue;
        }
        if (from.position && to->position) {
            if (from.position->string == to->position->string) {
                return i;
            }
            continue;
        }
        const int apart = std::abs(to->pitch.midiNumber() - from.pitch.midiNumber());
        if (apart < distance) {
            distance = apart;
            nearest = i;
        }
    }
    return nearest;
}

// Up the page (-1), down (1) or level (0): the way a line from `from` to `to`
// goes.
[[nodiscard]] double slideDirection(const core::Note& from, const core::Note& to) {
    const int rise = to.pitch.midiNumber() - from.pitch.midiNumber();
    return rise > 0 ? -1.0 : rise < 0 ? 1.0 : 0.0;
}

// The outer edge of a beam group's primary beam: y = edge + slope * (x - x0).
struct BeamLine {
    double edge = 0.0;
    double slope = 0.0;
    double x0 = 0.0;
    bool up = true;

    [[nodiscard]] double at(double x) const { return edge + slope * (x - x0); }
};

struct Column {
    Fraction start;
    std::vector<std::size_t> chords;
    double left = 0.0;
    double right = 0.0;
    double x = 0.0;
    core::Strum roll = core::Strum::None; // a strum written as an arpeggio line
    double rollX = 0.0;                   // the line's centre, from the column's x
};

class StandardWriter final : public StaffWriter {
public:
    StandardWriter(const core::Song& song, const core::Track& track, const core::Staff& staff,
                   bool lowerStaff, const EngravingFont& font, bool techniques)
        : StaffWriter(font, kStaffHeight, 5), song_(song), staff_(staff), clef_(staff.clef()),
          fretting_(track.instrument().kind == core::InstrumentKind::Fretted),
          lowerStaff_(lowerStaff),
          percussion_(staff.clef() == Clef::Percussion ||
                      track.instrument().kind == core::InstrumentKind::Percussion),
          offset_(percussion_ ? 0 : track.instrument().writtenOffset), techniques_(techniques),
          rolls_(!percussion_ && track.instrument().kind != core::InstrumentKind::Fretted),
          lanes_(font), dynamics_(font), aboveRow_(font, staff.isFretted() && !percussion_),
          lyrics_(staff) {}

    double header(std::size_t first) override;
    double changes(std::size_t m, double x) override;
    double ending(std::size_t m, double x) override { return endingAt(m, x); }
    void collect(std::size_t m, std::vector<ColumnInk>& ink) override;
    [[nodiscard]] double emptyWidth() const override {
        return std::max(widthOf(Glyph::RestWhole), repeatWidth(repeat_));
    }
    void draw(std::span<const ColumnPosition> positions, double start, double barline) override;

private:
    void close(double width) override {
        tiesOut(width - defaults_.thinBarlineThickness - kTieGap);
        slidesOut(width - defaults_.thinBarlineThickness - kSlideGap);
        slursOut(width - defaults_.thinBarlineThickness - kSlurLineGap);
        octaveLinesOut(width);
        if (techniques_ && !lanes_.empty()) {
            lanes_.place(list_, inkTop());
        }
        if (!aboveRow_.empty()) {
            aboveRow_.place(list_, inkTop());
        }
        // Lyrics right under the notes, dynamics under them (ADR-0084).
        if (!lyrics_.empty()) {
            lyrics_.place(list_, inkBottom(), height(), width - defaults_.thinBarlineThickness);
        }
        if (!dynamics_.empty()) {
            dynamics_.place(list_, inkBottom(), height(), width);
        }
        pedalLinesOut(width); // under the dynamics (ADR-0092)
    }

    // ---- Signatures ----

    [[nodiscard]] KeySignature keyAt(std::size_t m) const {
        return writtenKey(song_.measureHeader(m).keySignature, offset_);
    }

    // The clef in force at measure m (ADR-0087). Measures mostly come in
    // order, so the clef of the one asked for last carries on to the next.
    Clef clefIn(std::size_t m) {
        if (percussion_) {
            return staff_.clef(); // drums keep theirs
        }
        if (clefMeasure_ != m) {
            clefCached_ = clefMeasure_ + 1 == m ? staff_.measure(m).clef.value_or(clefCached_)
                                                : staff_.clefAt(m);
            clefMeasure_ = m;
        }
        return clefCached_;
    }

    double headerAt(std::size_t first) {
        measure_ = first;
        clef_ = clefIn(first);
        const ClefGlyph clef = clefGlyph(clef_);
        double x = glyphFrom(clef.glyph, kClefX, clef.y, Role::Clef);
        x = keySignature(keyAt(first), x);
        if (first == 0 || song_.measureHeader(first - 1).timeSignature !=
                              song_.measureHeader(first).timeSignature) {
            x = timeSignature(song_.measureHeader(first).timeSignature, x + kHeaderGap,
                              kMiddleLine);
        }
        // Ties from the line before start after the header, and so do the
        // slurs its last beats were slurred into (ADR-0086).
        lineStart_ = x;
        lineFirst_ = first;
        incoming_.fill(first > 0);
        slideIncoming_.fill(first > 0);
        for (auto& starts : slideStarts_) {
            starts.clear();
        }
        for (std::size_t v = 0; v < core::Measure::kMaxVoices; ++v) {
            slurNotes_[v].clear();
            bool carried = false;
            if (first > 0) {
                const std::vector<core::Beat>& before = staff_.measure(first - 1).voices[v].beats;
                carried = !before.empty() &&
                          slurredTo(before.back(), beatAfter(first - 1, v, before.back()));
            }
            slurOpen_[v] = carried;
            slurIncoming_[v] = carried;
        }
        return x;
    }

    // Draws a key signature after `x`; returns the new x (unchanged when empty).
    double keySignature(const KeySignature& key, double x, bool cancel = false) {
        if (percussion_ || key.fifths() == 0) {
            return x;
        }
        const bool sharp = key.fifths() > 0;
        const Glyph accidental = cancel  ? Glyph::AccidentalNatural
                                 : sharp ? Glyph::AccidentalSharp
                                         : Glyph::AccidentalFlat;
        x += kHeaderGap;
        const auto count = static_cast<std::size_t>(std::abs(key.fifths()));
        for (std::size_t i = 0; i < count; ++i) {
            x = glyphFrom(accidental, x, yOf(keyStep(clef_, sharp, i)), Role::KeySignature);
            if (i + 1 < count) {
                x += kKeyAccidentalGap;
            }
        }
        return x;
    }

    // Signature changes at the start of measure m, drawn after the barline.
    double changesAt(std::size_t m, double x) {
        measure_ = m;
        clef_ = clefIn(m);
        const KeySignature before = keyAt(m - 1);
        const KeySignature now = keyAt(m);
        if (now.fifths() != before.fifths()) {
            x = now.fifths() == 0 ? keySignature(before, x, true) : keySignature(now, x);
        }
        if (song_.measureHeader(m).timeSignature != song_.measureHeader(m - 1).timeSignature) {
            x = timeSignature(song_.measureHeader(m).timeSignature, x + kHeaderGap, kMiddleLine);
        }
        return x;
    }

    // ---- One measure ----

    // ---- One measure ----

    // The ink of the columns read by read().
    void columnInk(std::vector<ColumnInk>& ink) const {
        ink.clear();
        for (const Column& column : columns_) {
            Fraction longest;
            for (const std::size_t c : column.chords) {
                longest = std::max(longest, chords_[c].length);
            }
            ink.push_back(ColumnInk{.start = column.start,
                                    .longest = longest,
                                    .left = column.left,
                                    .right = column.right});
        }
    }

    void drawMeasure(std::span<const ColumnPosition> positions, double start, double barline) {
        measure_ = measureIndex_;
        for (Column& column : columns_) {
            column.x = columnX(positions, column.start);
            for (const std::size_t c : column.chords) {
                chords_[c].x = column.x;
            }
        }
        for (Chord& chord : chords_) {
            if (chord.measureRest) {
                tieStarts_[chord.voice].clear();
                incoming_[chord.voice] = false;
                slideStarts_[chord.voice].clear();
                slideIncoming_[chord.voice] = false;
                continue;
            }
            draw(chord);
            drawGraces(chord);
            ties(chord);
            slides(chord);
            articulations(chord);
            breath(chord);
        }
        for (const Column& column : columns_) {
            if (column.roll != core::Strum::None) {
                arpeggio(column);
            }
        }
        if (voiceCount_ == 0) {
            for (auto& starts : tieStarts_) {
                starts.clear();
            }
            incoming_.fill(false);
            for (auto& starts : slideStarts_) {
                starts.clear();
            }
            slideIncoming_.fill(false);
        }
        beamLines_.clear();
        for (const std::vector<std::size_t>& group : beamGroups_) {
            beamLines_.push_back(beam(group));
        }
        for (const TupletSpan& group : tuplets_) {
            tuplet(group);
        }
        fingerings();
        slurs();
        centreMeasureRests(start, barline);
        measureRepeat(repeat_, start, barline);
        fermatas(start, barline);
        ornaments(positions, barline);
        if (techniques_) {
            markTechniques(positions, barline);
        }
    }

    // The fingerings over a chord's notes, or for a fretted instrument the
    // plucking fingers (ADR-0093): one over the other in the order of the
    // notes, centred on the noteheads, clear of the ink; under the notes on a
    // lower staff of the track and for the lower voice of two. Slurs go round
    // them.
    void fingerings() {
        std::vector<Glyph> marks;
        for (Chord& chord : chords_) {
            if (chord.beat == nullptr || chord.rest || percussion_) {
                continue;
            }
            marks.clear();
            for (const Head& head : chord.heads) { // top to bottom
                if (head.note == nullptr) {
                    continue;
                }
                const core::Finger finger =
                    fretting_ ? head.note->effects.pluck : head.note->effects.fingering;
                if (finger != core::Finger::None) {
                    marks.push_back(fretting_ ? pluckGlyph(finger) : fingerGlyph(finger));
                }
            }
            if (marks.empty()) {
                continue;
            }
            const bool below = voiceCount_ > 1 ? !voiceUp_[chord.voice] : lowerStaff_;
            const double middle = chord.x + (chord.headLeft + chord.headRight) / 2;
            double half = 0.0;
            for (const Glyph mark : marks) {
                half = std::max(half, metrics(mark).box.width() / 2);
            }
            const Extents ink = inkBetween(middle - half, middle + half);
            double edge = below ? ink.bottom + kFingeringGap : ink.top - kFingeringGap;
            // Outwards from the notes: under them the highest note's first,
            // over them the lowest note's.
            for (std::size_t k = 0; k < marks.size(); ++k) {
                const Glyph mark = below ? marks[k] : marks[marks.size() - 1 - k];
                const BoundingBox& box = metrics(mark).box;
                const double x = middle - (box.southWest.x + box.northEast.x) / 2;
                const double y = below ? edge + box.northEast.y : edge + box.southWest.y;
                glyph(mark, x, y, Role::Fingering);
                edge = below ? y - box.southWest.y + kFingeringStackGap
                             : y - box.northEast.y - kFingeringStackGap;
            }
            const double outer = below ? edge - kFingeringStackGap : edge + kFingeringStackGap;
            if (below) {
                chord.inkBottom = std::max(chord.inkBottom, outer);
            } else {
                chord.inkTop = std::min(chord.inkTop, outer);
            }
            if (below == chord.stemUp) { // on the notehead side
                chord.marks = outer;
            }
        }
    }

    // The mark over a chord: "tr" for a trill (ADR-0090), else the glyph of a
    // mordent or a turn (ADR-0091); nullopt without either.
    static std::optional<Glyph> ornamentOf(const Chord& chord) {
        std::optional<Glyph> found;
        for (const Head& head : chord.heads) {
            if (head.note == nullptr) {
                continue;
            }
            if (head.note->effects.trill.isTrill()) {
                return Glyph::OrnamentTrill;
            }
            if (!found && head.note->effects.ornament != core::Ornament::None) {
                found = ornamentGlyph(head.note->effects.ornament);
            }
        }
        return found;
    }

    // The trills, mordents and turns of the measure's beats (ADR-0090,
    // ADR-0091): the mark centred over the notehead, clear of the ink, under
    // the staff for the lower voice of two; after "tr", a wavy line on to the
    // next column when there is room for it.
    void ornaments(std::span<const ColumnPosition> positions, double barline) {
        const double centre = widthOf(Glyph::NoteheadBlack) / 2;
        const BoundingBox& wave = metrics(Glyph::WiggleTrill).box;
        const double advance = metrics(Glyph::WiggleTrill).advanceWidth > 0.0
                                   ? metrics(Glyph::WiggleTrill).advanceWidth
                                   : wave.width();
        for (const Chord& chord : chords_) {
            if (chord.beat == nullptr || chord.rest) {
                continue;
            }
            const std::optional<Glyph> mark = ornamentOf(chord);
            if (!mark) {
                continue;
            }
            const BoundingBox& box = metrics(*mark).box;
            const bool below = voiceCount_ > 1 && !voiceUp_[chord.voice];
            const double x = chord.x + centre - (box.southWest.x + box.northEast.x) / 2;
            const double from = x + box.northEast.x + kTrillWaveGap;
            const double to = nextColumnX(positions, chord.start, barline) - kTrillEndGap;
            // Each wave reaches past its advance into the next: n of them span
            // (n - 1) advances and one wave's width.
            const int waves =
                *mark != Glyph::OrnamentTrill
                    ? 0
                    : static_cast<int>(std::floor((to - from - wave.width()) / advance)) + 1;
            const double reach =
                waves >= 1 ? from + (waves - 1) * advance + wave.width() : x + box.northEast.x;
            const Extents ink = inkBetween(x + box.southWest.x, reach);
            const double y = below ? ink.bottom + kTrillGap + box.northEast.y
                                   : ink.top - kTrillGap + box.southWest.y;
            glyph(*mark, x, y, Role::Ornament);
            if (waves < 1) {
                continue;
            }
            // The wavy line through the middle of the letters.
            const double middle = y - (box.southWest.y + box.northEast.y) / 2;
            const double waveY = middle + (wave.southWest.y + wave.northEast.y) / 2;
            for (int i = 0; i < waves; ++i) {
                glyph(Glyph::WiggleTrill, from + i * advance - wave.southWest.x, waveY,
                      Role::Ornament);
            }
        }
    }

    // The fermatas of the measure's beats (ADR-0085), centred on the notehead
    // or the rest: above, or under the staff for the lower voice of two.
    void fermatas(double start, double barline) {
        const double centre = widthOf(Glyph::NoteheadBlack) / 2;
        for (const Chord& chord : chords_) {
            if (chord.beat == nullptr || chord.beat->effects.fermata == core::Fermata::None) {
                continue;
            }
            const bool below = voiceCount_ > 1 && !voiceUp_[chord.voice];
            fermata(chord.beat->effects.fermata,
                    chord.measureRest ? (start + barline) / 2 : chord.x + centre, below);
        }
    }

    void centreMeasureRests(double start, double barline) {
        if (repeat_ != core::MeasureRepeat::None) { // its sign instead (ADR-0102)
            return;
        }
        for (const Chord& chord : chords_) {
            if (chord.measureRest) {
                const BoundingBox& box = metrics(Glyph::RestWhole).box;
                const double middle = (start + barline) / 2;
                glyph(Glyph::RestWhole, middle - (box.southWest.x + box.northEast.x) / 2,
                      restY(chord, Glyph::RestWhole), Role::Rest);
            }
        }
    }

    // The clef measure m + 1 changes to, smaller, before the barline closing
    // m: engravers warn of a change before the measure, and at the end of a
    // line before the next (ADR-0087).
    double endingAt(std::size_t m, double x) {
        if (percussion_ || m + 1 >= staff_.measureCount()) {
            return x;
        }
        const std::optional<Clef>& next = staff_.measure(m + 1).clef;
        if (!next || *next == clefIn(m)) {
            return x;
        }
        measure_ = m;
        const ClefGlyph clef = clefGlyph(*next);
        const BoundingBox& box = metrics(clef.glyph).box;
        glyph(clef.glyph, x - box.southWest.x * kClefChangeScale, clef.y, Role::Clef,
              kClefChangeScale);
        return x + box.width() * kClefChangeScale + kClefChangeGap;
    }

    // Reads measure m into chords_, columns_ and beamGroups_.
    void read(std::size_t m) {
        measure_ = m;
        measureIndex_ = m;
        clef_ = clefIn(m);
        chords_.clear();
        columns_.clear();
        beamGroups_.clear();
        tuplets_.clear();
        const core::Measure& measure = staff_.measure(m);
        const TimeSignature time = song_.measureHeader(m).timeSignature;
        const Fraction length = time.measureLength();
        repeat_ = shownRepeat(song_, staff_, m);
        voiceCount_ = 0;
        for (const core::Voice& voice : measure.voices) {
            if (!voice.isEmpty()) {
                ++voiceCount_;
            }
        }
        if (voiceCount_ == 0) {
            Chord rest;
            rest.length = length;
            rest.rest = true;
            rest.measureRest = true;
            chords_.push_back(std::move(rest));
            return;
        }
        for (std::size_t v = 0; v < measure.voices.size(); ++v) {
            const auto& beats = measure.voices[v].beats;
            Fraction at;
            const std::size_t voiceStart = chords_.size();
            for (const Beat& beat : beats) {
                Chord chord;
                chord.beat = &beat;
                chord.voice = v;
                chord.start = at;
                chord.length = beat.duration.length();
                chord.duration = beat.duration;
                chord.rest = beat.isRest();
                chord.measureRest = chord.rest && beats.size() == 1 && chord.length >= length;
                for (const core::Note& note : beat.notes) {
                    chord.heads.push_back(headOf(note, beat.duration.value(), beat.effects.ottava));
                }
                std::ranges::sort(chord.heads, {}, &Head::step);
                for (const core::Grace& grace : beat.graces) {
                    if (grace.notes.empty()) {
                        continue;
                    }
                    Chord small;
                    small.voice = v;
                    small.duration = grace.duration;
                    small.slash = !grace.onBeat;
                    for (const core::Note& note : grace.notes) {
                        small.heads.push_back(
                            headOf(note, grace.duration.value(), beat.effects.ottava));
                    }
                    std::ranges::sort(small.heads, {}, &Head::step);
                    chord.graces.push_back(std::move(small));
                }
                at += chord.length;
                chords_.push_back(std::move(chord));
            }
            groupVoice(voiceStart, time);
        }
        accidentals(m);
        voiceDirections();
        for (std::size_t c = 0; c < chords_.size(); ++c) {
            if (chords_[c].beamGroup < 0) {
                chords_[c].stemUp = stemUpFor({c});
            }
        }
        for (const std::vector<std::size_t>& group : beamGroups_) {
            const bool up = stemUpFor(group);
            for (const std::size_t c : group) {
                chords_[c].stemUp = up;
            }
        }
        for (Chord& chord : chords_) {
            if (!chord.measureRest) {
                shape(chord);
                makeRoomForSlides(chord);
                placeGraces(chord);
            }
        }
        // Columns: chords starting together.
        std::vector<std::size_t> order;
        for (std::size_t c = 0; c < chords_.size(); ++c) {
            if (!chords_[c].measureRest) {
                order.push_back(c);
            }
        }
        std::ranges::stable_sort(order, {}, [this](std::size_t c) { return chords_[c].start; });
        for (const std::size_t c : order) {
            if (columns_.empty() || columns_.back().start != chords_[c].start) {
                Column column;
                column.start = chords_[c].start;
                column.left = std::numeric_limits<double>::max();
                column.right = std::numeric_limits<double>::lowest();
                columns_.push_back(std::move(column));
            }
            Column& column = columns_.back();
            column.chords.push_back(c);
            column.left = std::min(column.left, chords_[c].left);
            column.right = std::max(column.right, chords_[c].right);
        }
        if (rolls_) {
            for (Column& column : columns_) {
                rollRoom(column);
            }
        }
    }

    // A strum of a keyboard or another instrument without frets is a rolled
    // chord: the column keeps room for its arpeggio line left of the ink of
    // all its chords (ADR-0076).
    void rollRoom(Column& column) const {
        for (const std::size_t c : column.chords) {
            const Chord& chord = chords_[c];
            if (!chord.rest && chord.beat->effects.strum != core::Strum::None) {
                column.roll = chord.beat->effects.strum;
                break;
            }
        }
        if (column.roll == core::Strum::None) {
            return;
        }
        column.rollX = column.left - kArpeggioGap - kArpeggioWidth / 2;
        column.left -= kArpeggioGap + kArpeggioWidth;
    }

    // The arpeggio line of a rolled column, from above its top notehead to
    // below its bottom one: plain when the chord rolls upwards (the way a
    // downstroke plays it, ADR-0025), with an arrowhead at the bottom when it
    // rolls downwards.
    void arpeggio(const Column& column) {
        int top = std::numeric_limits<int>::max();
        int bottom = std::numeric_limits<int>::lowest();
        for (const std::size_t c : column.chords) {
            for (const Head& head : chords_[c].heads) {
                top = std::min(top, head.step);
                bottom = std::max(bottom, head.step);
            }
        }
        const double x = column.x + column.rollX;
        double from = yOf(top) - kArpeggioOverhang;
        double to = yOf(bottom) + kArpeggioOverhang;
        if (to - from < kArpeggioMinLength) {
            const double grow = (kArpeggioMinLength - (to - from)) / 2;
            from -= grow;
            to += grow;
        }
        const bool down = column.roll == core::Strum::Up;
        const double waveEnd = down ? to - kArpeggioArrow * 0.75 : to;
        const int halves =
            std::max(2, static_cast<int>(std::lround((waveEnd - from) / kArpeggioWave)));
        const double length = (waveEnd - from) / halves;
        for (int i = 0; i < halves; ++i) {
            const double y = from + length * i;
            const double swing = i % 2 == 0 ? kArpeggioSwing : -kArpeggioSwing;
            list_.curves.push_back(CurveItem{.from = {x, y},
                                             .control1 = {x + swing, y + length / 3},
                                             .control2 = {x + swing, y + length * 2 / 3},
                                             .to = {x, y + length},
                                             .thickness = kArpeggioThickness,
                                             .endThickness = kArpeggioThickness,
                                             .role = Role::Arpeggio,
                                             .measure = measure_});
        }
        if (down) {
            const double half = kArpeggioWidth / 2;
            polygon({{x - half, to - kArpeggioArrow}, {x + half, to - kArpeggioArrow}, {x, to}},
                    Role::Arpeggio);
        }
    }

    // Under an octave line the note is written one or two octaves nearer the
    // staff (ADR-0089).
    [[nodiscard]] Head headOf(const core::Note& note, NoteValue value, core::Ottava ottava) const {
        Head head;
        head.note = &note;
        head.tied = note.effects.tiedFromPrevious;
        head.ghost = note.effects.ghost;
        if (percussion_) {
            const detail::DrumNote drum = detail::drumNote(note.pitch.midiNumber());
            head.step = drum.step;
            head.foot = drum.foot;
            head.glyph = noteheadFor(value, drum.head);
            return head;
        }
        const Pitch written = writtenPitch(note.pitch, offset_ - 12 * core::ottavaOctaves(ottava));
        head.diatonic = diatonicIndex(written);
        head.step = topLineIndex(clef_) - head.diatonic;
        head.alter = written.alter();
        // A dead note is a cross; a natural harmonic, written where it is
        // touched, a diamond (ADR-0100).
        const detail::DrumHead shape = note.effects.deadNote ? detail::DrumHead::X
                                       : note.effects.harmonic.kind == core::HarmonicKind::Natural
                                           ? detail::DrumHead::Diamond
                                           : detail::DrumHead::Normal;
        head.glyph = noteheadFor(value, shape);
        return head;
    }

    // The beam groups and tuplets of the voice whose chords start at `from`.
    void groupVoice(std::size_t from, const TimeSignature& time) {
        rhythm_.clear();
        for (std::size_t c = from; c < chords_.size(); ++c) {
            rhythm_.push_back(RhythmItem{.start = chords_[c].start,
                                         .duration = chords_[c].duration,
                                         .rest = chords_[c].rest});
        }
        for (std::vector<std::size_t>& group : beamGroups(rhythm_, time)) {
            for (std::size_t& c : group) {
                c += from;
                chords_[c].beamGroup = static_cast<int>(beamGroups_.size());
            }
            beamGroups_.push_back(std::move(group));
        }
        for (const TupletSpan& span : tupletSpans(rhythm_)) {
            if (!chords_[from + span.first].measureRest) {
                tuplets_.push_back(TupletSpan{
                    .first = from + span.first, .last = from + span.last, .tuplet = span.tuplet});
            }
        }
    }

    // Marks the accidentals the key signature and earlier notes of the measure
    // do not already give, going through the voices in time order.
    void accidentals(std::size_t m) {
        if (percussion_) {
            return;
        }
        const KeySignature key = keyAt(m);
        constexpr std::int8_t kUnset = std::numeric_limits<std::int8_t>::max();
        std::array<std::int8_t, 128> state{};
        state.fill(kUnset);
        std::vector<std::size_t> order(chords_.size());
        for (std::size_t c = 0; c < order.size(); ++c) {
            order[c] = c;
        }
        std::ranges::stable_sort(order, {}, [this](std::size_t c) { return chords_[c].start; });
        const auto mark = [&](Head& head) {
            if (head.tied || head.diatonic < 0 || head.diatonic >= static_cast<int>(state.size())) {
                return;
            }
            std::int8_t& current = state[static_cast<std::size_t>(head.diatonic)];
            const int expected =
                current == kUnset ? key.alterFor(static_cast<Step>(head.diatonic % 7)) : current;
            if (head.alter != expected) {
                head.accidental = accidentalFor(head.alter);
            }
            current = static_cast<std::int8_t>(head.alter);
        };
        for (const std::size_t c : order) {
            // Grace notes come before their chord (ADR-0079).
            for (Chord& grace : chords_[c].graces) {
                std::ranges::for_each(grace.heads, mark);
            }
            std::ranges::for_each(chords_[c].heads, mark);
        }
    }

    // With several voices, the first and third point up and the others down.
    // On a drum staff the drum map decides instead when hands and feet have
    // voices of their own: hands up, feet down.
    void voiceDirections() {
        for (std::size_t v = 0; v < voiceUp_.size(); ++v) {
            voiceUp_[v] = v % 2 == 0;
        }
        if (!percussion_ || voiceCount_ < 2) {
            return;
        }
        std::array<bool, core::Measure::kMaxVoices> feet{};
        std::array<bool, core::Measure::kMaxVoices> hands{};
        for (const Chord& chord : chords_) {
            for (const Head& head : chord.heads) {
                (head.foot ? feet : hands)[chord.voice] = true;
            }
        }
        bool footVoice = false;
        bool handVoice = false;
        for (std::size_t v = 0; v < voiceUp_.size(); ++v) {
            footVoice = footVoice || (feet[v] && !hands[v]);
            handVoice = handVoice || hands[v];
        }
        if (footVoice && handVoice) {
            for (std::size_t v = 0; v < voiceUp_.size(); ++v) {
                voiceUp_[v] = !(feet[v] && !hands[v]);
            }
        }
    }

    // Stems of several voices follow their voice; otherwise they point away
    // from the middle line, judged by the note farthest from it.
    [[nodiscard]] bool stemUpFor(const std::vector<std::size_t>& group) const {
        const Chord& first = chords_[group.front()];
        if (voiceCount_ > 1) {
            return voiceUp_[first.voice];
        }
        if (percussion_) {
            return true;
        }
        int highest = std::numeric_limits<int>::max();
        int lowest = std::numeric_limits<int>::min();
        for (const std::size_t c : group) {
            for (const Head& head : chords_[c].heads) {
                highest = std::min(highest, head.step);
                lowest = std::max(lowest, head.step);
            }
        }
        if (chords_[group.front()].heads.empty()) {
            return true;
        }
        return lowest - kMiddleStep > kMiddleStep - highest;
    }

    [[nodiscard]] Point stemAnchor(const Head& head, bool up) const {
        const GlyphMetrics& m = metrics(head.glyph);
        if (up) {
            return m.anchor(Anchor::StemUpSE).value_or(Point{m.box.northEast.x, 0.0});
        }
        return m.anchor(Anchor::StemDownNW).value_or(Point{m.box.southWest.x, 0.0});
    }

    [[nodiscard]] double restY(const Chord& chord, Glyph rest) const {
        double y = rest == Glyph::RestWhole || rest == Glyph::RestDoubleWhole ? 1.0 : kMiddleLine;
        if (voiceCount_ > 1) {
            y += voiceUp_[chord.voice] ? -kVoiceRestShift : kVoiceRestShift;
        }
        return y;
    }

    // Places the noteheads, accidentals and dots of a chord around its x and
    // measures its ink.
    void shape(Chord& chord) {
        if (chord.rest) {
            const Glyph rest = restFor(chord.duration.value());
            const BoundingBox& box = metrics(rest).box;
            chord.headLeft = 0.0;
            chord.headRight = box.width();
            chord.left = 0.0;
            chord.right = box.width() + dotsWidth(chord.duration.dots());
            makeRoomForBreath(chord);
            return;
        }
        auto& heads = chord.heads;
        const bool stem = hasStem(chord.duration.value());
        const double stemWidth = defaults_.stemThickness;
        // Seconds: alternate notes move to the other side of the stem.
        const auto flipOffset = [&](const Head& head) {
            const double width = widthOf(head.glyph);
            if (chord.stemUp) {
                return stem ? stemAnchor(head, true).x - stemWidth : width;
            }
            return stem ? -(width - stemWidth) : -width;
        };
        if (chord.stemUp) {
            bool flipped = false;
            for (std::size_t i = heads.size(); i-- > 0;) {
                const bool second = i + 1 < heads.size() && heads[i + 1].step - heads[i].step <= 1;
                flipped = second && !flipped;
                heads[i].dx = flipped ? flipOffset(heads[i]) : 0.0;
            }
        } else {
            bool flipped = false;
            for (std::size_t i = 0; i < heads.size(); ++i) {
                const bool second = i > 0 && heads[i].step - heads[i - 1].step <= 1;
                flipped = second && !flipped;
                heads[i].dx = flipped ? flipOffset(heads[i]) : 0.0;
            }
        }
        chord.headLeft = std::numeric_limits<double>::max();
        chord.headRight = std::numeric_limits<double>::lowest();
        for (const Head& head : heads) {
            const BoundingBox& box = metrics(head.glyph).box;
            chord.headLeft = std::min(chord.headLeft, head.dx + box.southWest.x);
            chord.headRight = std::max(chord.headRight, head.dx + box.northEast.x);
        }
        chord.ghostPad =
            std::ranges::any_of(heads, &Head::ghost)
                ? kParenthesisGap + std::max(metrics(Glyph::NoteheadParenthesisLeft).box.width(),
                                             metrics(Glyph::NoteheadParenthesisRight).box.width())
                : 0.0;
        chord.left = chord.headLeft - chord.ghostPad - placeAccidentals(chord);
        if (fretting_ && !percussion_) {
            chord.left -= placeFingerings(chord, chord.left);
        }
        chord.right = chord.headRight + chord.ghostPad + dotsWidth(chord.duration.dots());
        if (stem && chord.beamGroup < 0 && beamCount(chord.duration.value()) > 0) {
            const Glyph flag = flagFor(chord.duration.value(), chord.stemUp);
            const Head& base = chord.stemUp ? heads.back() : heads.front();
            const double stemLeft =
                stemAnchor(base, chord.stemUp).x - (chord.stemUp ? stemWidth : 0.0);
            chord.right = std::max(chord.right, stemLeft + metrics(flag).box.northEast.x);
        }
        if (const auto slashes = tremoloOf(chord)) {
            const double half = metrics(*slashes).box.width() / 2;
            double centre = (chord.headLeft + chord.headRight) / 2;
            if (stem) {
                const Head& base = chord.stemUp ? heads.back() : heads.front();
                centre = stemAnchor(base, chord.stemUp).x +
                         (chord.stemUp ? -stemWidth / 2 : stemWidth / 2);
            }
            chord.left = std::min(chord.left, centre - half);
            chord.right = std::max(chord.right, centre + half);
        }
        makeRoomForBreath(chord);
    }

    // The slashes of a chord's tremolo picking (ADR-0101), if any.
    [[nodiscard]] static std::optional<Glyph> tremoloOf(const Chord& chord) {
        if (chord.rest || chord.beat == nullptr || !chord.beat->effects.tremoloPicking) {
            return std::nullopt;
        }
        return tremoloGlyph(tremoloStrokes(*chord.beat->effects.tremoloPicking));
    }

    // The edge of the notehead nearest the stem's tip, or where it would be.
    [[nodiscard]] double headEdge(const Chord& chord) const {
        if (chord.stemUp) {
            const Head& head = chord.heads.front();
            return yOf(head.step) - metrics(head.glyph).box.northEast.y;
        }
        const Head& head = chord.heads.back();
        return yOf(head.step) - metrics(head.glyph).box.southWest.y;
    }

    // The stem a chord needs from its notehead nearest the tip for its
    // tremolo slashes, their gaps and `zone`, the flag or beams at the tip;
    // 0 without tremolo picking.
    [[nodiscard]] double tremoloStem(const Chord& chord, double zone) const {
        const double room = tremoloRoom(chord);
        if (room == 0.0) {
            return 0.0;
        }
        const Head& head = chord.stemUp ? chord.heads.front() : chord.heads.back();
        return std::abs(headEdge(chord) - yOf(head.step)) + room + zone;
    }

    // The height of a chord's tremolo slashes and their gaps; 0 without them.
    [[nodiscard]] double tremoloRoom(const Chord& chord) const {
        const auto slashes = tremoloOf(chord);
        return slashes ? metrics(*slashes).box.height() + 2 * kTremoloGap : 0.0;
    }

    // A chord's tremolo slashes centred on `x`, midway between its notehead
    // and `inner`, where the stem's tip or the flag or beams at it begin.
    void tremolo(Chord& chord, double x, double inner) {
        const auto slashes = tremoloOf(chord);
        if (!slashes) {
            return;
        }
        const BoundingBox& box = metrics(*slashes).box;
        const double y = (headEdge(chord) + inner) / 2;
        glyph(*slashes, x - (box.southWest.x + box.northEast.x) / 2,
              y + (box.southWest.y + box.northEast.y) / 2, Role::Technique);
        chord.inkTop = std::min(chord.inkTop, y - box.height() / 2);
        chord.inkBottom = std::max(chord.inkBottom, y + box.height() / 2);
    }

    // The glyph of the break after a chord's beat, if any (ADR-0095).
    [[nodiscard]] static std::optional<Glyph> breathGlyph(const Chord& chord) {
        if (chord.beat == nullptr) {
            return std::nullopt;
        }
        switch (chord.beat->effects.breath) {
        case core::Breath::Comma:
            return Glyph::BreathMarkComma;
        case core::Breath::Caesura:
            return Glyph::Caesura;
        case core::Breath::None:
            break;
        }
        return std::nullopt;
    }

    // A breath mark or a caesura goes right of the chord's ink, which then
    // reaches over it so that the next beat makes room.
    void makeRoomForBreath(Chord& chord) {
        if (const std::optional<Glyph> mark = breathGlyph(chord)) {
            chord.breathX = chord.right + kBreathGap;
            chord.right = chord.breathX + metrics(*mark).box.width();
        }
    }

    // Draws it: a comma over the staff, clear of the chord's notes and marks;
    // a caesura through the top line, from the space under it.
    void breath(const Chord& chord) {
        const std::optional<Glyph> mark = breathGlyph(chord);
        if (!mark) {
            return;
        }
        const BoundingBox& box = metrics(*mark).box;
        const double x = chord.x + chord.breathX - box.southWest.x;
        if (*mark == Glyph::Caesura) {
            glyph(*mark, x, yOf(2) + box.southWest.y, Role::Articulation);
            return;
        }
        double bottom = std::min(yOf(0), chord.rest ? yOf(0) : chord.inkTop);
        if (chord.marks && *chord.marks < bottom) {
            bottom = *chord.marks;
        }
        glyph(*mark, x, bottom - kBreathRaise + box.southWest.y, Role::Articulation);
    }

    [[nodiscard]] double dotsWidth(int dots) const {
        if (dots == 0) {
            return 0.0;
        }
        const double dot = widthOf(Glyph::AugmentationDot);
        return kDotGap + dots * dot + (dots - 1) * kDotSeparation;
    }

    // Stacks the chord's accidentals in columns left of its noteheads, so that
    // two accidentals in one column keep three staff spaces apart; returns the
    // width they take, gaps included.
    double placeAccidentals(Chord& chord) {
        struct AccidentalColumn {
            std::vector<int> steps;
            double width = 0.0;
        };
        std::vector<AccidentalColumn> columns;
        std::vector<std::size_t> columnOf(chord.heads.size(), 0);
        for (std::size_t i = 0; i < chord.heads.size(); ++i) {
            const Head& head = chord.heads[i];
            if (!head.accidental) {
                continue;
            }
            std::size_t c = 0;
            while (
                c < columns.size() &&
                std::ranges::any_of(
                    columns[c].steps,
                    [&](int step) { return std::abs(step - head.step) < kAccidentalClearance; })) {
                ++c;
            }
            if (c == columns.size()) {
                columns.emplace_back();
            }
            columns[c].steps.push_back(head.step);
            columns[c].width = std::max(columns[c].width, widthOf(*head.accidental));
            columnOf[i] = c;
        }
        if (columns.empty()) {
            return 0.0;
        }
        std::vector<double> rightEdge(columns.size());
        double edge = chord.headLeft - chord.ghostPad - kAccidentalGap;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            rightEdge[c] = edge;
            edge -= columns[c].width + kAccidentalColumnGap;
        }
        for (std::size_t i = 0; i < chord.heads.size(); ++i) {
            Head& head = chord.heads[i];
            if (head.accidental) {
                head.accidentalX =
                    rightEdge[columnOf[i]] - metrics(*head.accidental).box.northEast.x;
            }
        }
        return chord.headLeft - chord.ghostPad - (edge + kAccidentalColumnGap);
    }

    // The glyph of a finger: 1 to 5 from the thumb, or for a fretting hand T
    // and 1 to 4 from the index (ADR-0093).
    [[nodiscard]] Glyph fingerGlyph(core::Finger finger) const {
        static constexpr std::array kFrom{Glyph::Fingering1, Glyph::Fingering1, Glyph::Fingering2,
                                          Glyph::Fingering3, Glyph::Fingering4, Glyph::Fingering5};
        static constexpr std::array kFretting{Glyph::FingeringTUpper, Glyph::FingeringTUpper,
                                              Glyph::Fingering1,      Glyph::Fingering2,
                                              Glyph::Fingering3,      Glyph::Fingering4};
        return (fretting_ ? kFretting : kFrom)[static_cast<std::size_t>(finger) % kFrom.size()];
    }

    // The letter of a plucking finger: p, i, m, a, c.
    [[nodiscard]] static Glyph pluckGlyph(core::Finger finger) {
        static constexpr std::array kLetters{Glyph::FingeringPLower, Glyph::FingeringPLower,
                                             Glyph::FingeringILower, Glyph::FingeringMLower,
                                             Glyph::FingeringALower, Glyph::FingeringCLower};
        return kLetters[static_cast<std::size_t>(finger) % kLetters.size()];
    }

    // The fingerings of a fretting hand (ADR-0093), small, each level with its
    // notehead in columns left of `left`, the accidentals' edge, so that two in
    // a column keep a staff space apart; returns the width they take, gaps
    // included.
    double placeFingerings(Chord& chord, double left) {
        constexpr int kClearance = 2; // steps: a staff space
        const auto fingered = [](const Head& head) {
            return head.note != nullptr && head.note->effects.fingering != core::Finger::None;
        };
        if (std::ranges::none_of(chord.heads, fingered)) {
            for (Head& head : chord.heads) {
                head.fingering.reset();
            }
            return 0.0;
        }
        struct FingerColumn {
            std::vector<int> steps;
            double width = 0.0;
        };
        std::vector<FingerColumn> columns;
        std::vector<std::size_t> columnOf(chord.heads.size(), 0);
        for (std::size_t i = 0; i < chord.heads.size(); ++i) {
            Head& head = chord.heads[i];
            head.fingering.reset();
            if (!fingered(head)) {
                continue;
            }
            head.fingering = fingerGlyph(head.note->effects.fingering);
            std::size_t c = 0;
            while (c < columns.size() &&
                   std::ranges::any_of(
                       columns[c].steps,
                       [&](int step) { return std::abs(step - head.step) < kClearance; })) {
                ++c;
            }
            if (c == columns.size()) {
                columns.emplace_back();
            }
            columns[c].steps.push_back(head.step);
            columns[c].width = std::max(columns[c].width,
                                        metrics(*head.fingering).box.width() * kFingeringLeftScale);
            columnOf[i] = c;
        }
        if (columns.empty()) {
            return 0.0;
        }
        // Clear of the ledger lines, which reach left of the noteheads.
        const bool ledgers = chord.heads.front().step <= -2 || chord.heads.back().step >= 10;
        const double from =
            ledgers ? std::min(left, chord.headLeft - defaults_.legerLineExtension) : left;
        std::vector<double> rightEdge(columns.size());
        double edge = from - kFingeringGap;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            rightEdge[c] = edge;
            edge -= columns[c].width + kFingeringGap;
        }
        for (std::size_t i = 0; i < chord.heads.size(); ++i) {
            Head& head = chord.heads[i];
            if (head.fingering) {
                head.fingeringX = rightEdge[columnOf[i]] -
                                  metrics(*head.fingering).box.northEast.x * kFingeringLeftScale;
            }
        }
        return left - (edge + kFingeringGap);
    }

    // The parentheses round the notehead of a ghost note (ADR-0099), its
    // chord at x, at `scale` of their size.
    void parentheses(const Head& head, double x, double scale) {
        const BoundingBox& box = metrics(head.glyph).box;
        const BoundingBox& opening = metrics(Glyph::NoteheadParenthesisLeft).box;
        const BoundingBox& closing = metrics(Glyph::NoteheadParenthesisRight).box;
        const double y = yOf(head.step);
        glyph(Glyph::NoteheadParenthesisLeft,
              x + (head.dx + box.southWest.x - kParenthesisGap - opening.northEast.x) * scale, y,
              Role::Parenthesis, scale);
        glyph(Glyph::NoteheadParenthesisRight,
              x + (head.dx + box.northEast.x + kParenthesisGap - closing.southWest.x) * scale, y,
              Role::Parenthesis, scale);
    }

    void draw(Chord& chord) {
        const double x = chord.x;
        if (chord.rest) {
            const Glyph rest = restFor(chord.duration.value());
            const double y = restY(chord, rest);
            glyph(rest, x - metrics(rest).box.southWest.x, y, Role::Rest);
            chord.inkTop = y - metrics(rest).box.northEast.y;
            chord.inkBottom = y - metrics(rest).box.southWest.y;
            dots(x + chord.headRight, {y - 0.5}, chord.duration.dots());
            return;
        }
        std::vector<double> dotYs;
        chord.inkTop = std::numeric_limits<double>::max();
        chord.inkBottom = std::numeric_limits<double>::lowest();
        const auto ink = [&chord](const BoundingBox& box, double y) {
            chord.inkTop = std::min(chord.inkTop, y - box.northEast.y);
            chord.inkBottom = std::max(chord.inkBottom, y - box.southWest.y);
        };
        for (const Head& head : chord.heads) {
            glyph(head.glyph, x + head.dx, yOf(head.step), Role::Notehead);
            ink(metrics(head.glyph).box, yOf(head.step));
            if (head.ghost) {
                parentheses(head, x, 1.0);
                ink(metrics(Glyph::NoteheadParenthesisLeft).box, yOf(head.step));
            }
            if (head.accidental) {
                glyph(*head.accidental, x + head.accidentalX, yOf(head.step), Role::Accidental);
                ink(metrics(*head.accidental).box, yOf(head.step));
            }
            if (head.fingering) { // level with the middle of its notehead
                const BoundingBox& box = metrics(*head.fingering).box;
                const double y =
                    yOf(head.step) + (box.southWest.y + box.northEast.y) * kFingeringLeftScale / 2;
                glyph(*head.fingering, x + head.fingeringX, y, Role::Fingering,
                      kFingeringLeftScale);
                chord.inkTop = std::min(chord.inkTop, y - box.northEast.y * kFingeringLeftScale);
                chord.inkBottom =
                    std::max(chord.inkBottom, y - box.southWest.y * kFingeringLeftScale);
            }
            const bool onLine = head.step % 2 == 0;
            const bool lower = voiceCount_ > 1 && !voiceUp_[chord.voice];
            const double dotY = yOf(head.step) + (onLine ? (lower ? 0.5 : -0.5) : 0.0);
            if (std::ranges::find(dotYs, dotY) == dotYs.end()) {
                dotYs.push_back(dotY);
            }
        }
        dots(x + chord.headRight + chord.ghostPad, dotYs, chord.duration.dots());
        ledgerLines(chord, x);
        if (!hasStem(chord.duration.value())) {
            // Tremolo slashes where its stem would be.
            const double room = tremoloRoom(chord);
            tremolo(chord, x + (chord.headLeft + chord.headRight) / 2,
                    headEdge(chord) + (chord.stemUp ? -room : room));
            return;
        }
        const double stemWidth = defaults_.stemThickness;
        const Head& base = chord.stemUp ? chord.heads.back() : chord.heads.front();
        const Point anchor = stemAnchor(base, chord.stemUp);
        chord.stemX = x + anchor.x + (chord.stemUp ? -stemWidth / 2 : stemWidth / 2);
        chord.stemStart = yOf(base.step) - anchor.y;
        if (chord.beamGroup >= 0) {
            return; // the beam decides where the stem ends
        }
        // Tremolo slashes lengthen the stem to stay clear of the flag.
        const NoteValue value = chord.duration.value();
        const double flagZone =
            beamCount(value) > 0
                ? metrics(flagFor(value, chord.stemUp)).box.height() * kTremoloFlagShare
                : 0.0;
        const double length = std::max(stemLengthFor(value), tremoloStem(chord, flagZone));
        const double tip = chord.stemUp
                               ? std::min(yOf(chord.heads.front().step) - length, kMiddleLine)
                               : std::max(yOf(chord.heads.back().step) + length, kMiddleLine);
        line({chord.stemX, chord.stemStart}, {chord.stemX, tip}, stemWidth, Role::Stem);
        chord.inkTop = std::min(chord.inkTop, tip);
        chord.inkBottom = std::max(chord.inkBottom, tip);
        tremolo(chord, chord.stemX, tip + (chord.stemUp ? flagZone : -flagZone));
        if (beamCount(chord.duration.value()) > 0) {
            const Glyph flag = flagFor(chord.duration.value(), chord.stemUp);
            const Point attach = metrics(flag)
                                     .anchor(chord.stemUp ? Anchor::StemUpNW : Anchor::StemDownSW)
                                     .value_or(Point{});
            glyph(flag, chord.stemX - stemWidth / 2 - attach.x, tip + attach.y, Role::Flag);
        }
    }

    void dots(double headRight, const std::vector<double>& ys, int count) {
        if (count == 0) {
            return;
        }
        const double width = widthOf(Glyph::AugmentationDot);
        for (const double y : ys) {
            double x = headRight + kDotGap;
            for (int i = 0; i < count; ++i) {
                glyphFrom(Glyph::AugmentationDot, x, y, Role::Dot);
                x += width + kDotSeparation;
            }
        }
    }

    // The ledger lines of a chord at x drawn at `scale` of its size.
    void ledgerLines(const Chord& chord, double x, double scale = 1.0) {
        const double extension = defaults_.legerLineExtension;
        const auto ledger = [&](int step, bool above) {
            double from = std::numeric_limits<double>::max();
            double to = std::numeric_limits<double>::lowest();
            for (const Head& head : chord.heads) {
                if (above ? head.step <= step : head.step >= step) {
                    const BoundingBox& box = metrics(head.glyph).box;
                    from = std::min(from, x + (head.dx + box.southWest.x - extension) * scale);
                    to = std::max(to, x + (head.dx + box.northEast.x + extension) * scale);
                }
            }
            line({from, yOf(step)}, {to, yOf(step)}, defaults_.legerLineThickness,
                 Role::LedgerLine);
        };
        for (int step = -2; step >= chord.heads.front().step; step -= 2) {
            ledger(step, true);
        }
        for (int step = 10; step <= chord.heads.back().step; step += 2) {
            ledger(step, false);
        }
    }

    // ---- Grace notes (ADR-0079) ----

    // Shapes the grace notes of a chord and puts them left of its ink, its
    // left then reaching over them. Their stems point up, or down in a voice
    // pointing down; two or more flagged grace notes share their beams.
    void placeGraces(Chord& chord) {
        if (chord.graces.empty()) {
            return;
        }
        const bool up = voiceCount_ < 2 || voiceUp_[chord.voice];
        const bool beamed =
            chord.graces.size() > 1 && std::ranges::all_of(chord.graces, [](const Chord& grace) {
                return beamCount(grace.duration.value()) > 0;
            });
        const bool sliding = std::ranges::any_of(chord.graces.back().heads, [](const Head& head) {
            return head.note != nullptr && slidesOn(*head.note);
        });
        double right = chord.left - (sliding ? kGraceSlideGap : kGraceGap);
        for (auto grace = chord.graces.rbegin(); grace != chord.graces.rend(); ++grace) {
            grace->stemUp = up;
            grace->beamGroup = beamed ? 0 : -1;
            shape(*grace);
            grace->x = right - grace->right * kGraceScale;
            right = grace->x + grace->left * kGraceScale - kGraceGap;
        }
        chord.left = right + kGraceGap;
    }

    // Draws the grace notes of a chord: noteheads, accidentals and ledger lines
    // at their size, then their stems with a flag each or beams across them,
    // and a slash through the first stem of grace notes before the beat.
    void drawGraces(Chord& chord) {
        if (chord.graces.empty()) {
            return;
        }
        constexpr double kScale = kGraceScale;
        const double stemWidth = defaults_.stemThickness * kScale;
        const bool beamed = chord.graces.front().beamGroup >= 0;
        const bool up = chord.graces.front().stemUp;
        int beams = 0;
        double beamTip =
            up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
        if (beamed) {
            beams = std::numeric_limits<int>::max();
            for (const Chord& grace : chord.graces) {
                beams = std::min(beams, beamCount(grace.duration.value()));
            }
            const double length = (kStemLength + kBeamExtraStem * (beams - 1)) * kScale;
            for (const Chord& grace : chord.graces) {
                beamTip = up ? std::min(beamTip, yOf(grace.heads.front().step) - length)
                             : std::max(beamTip, yOf(grace.heads.back().step) + length);
            }
        }
        for (Chord& grace : chord.graces) {
            const double x = chord.x + grace.x;
            for (const Head& head : grace.heads) {
                glyph(head.glyph, x + head.dx * kScale, yOf(head.step), Role::Notehead, kScale);
                if (head.ghost) {
                    parentheses(head, x, kScale);
                }
                if (head.accidental) {
                    glyph(*head.accidental, x + head.accidentalX * kScale, yOf(head.step),
                          Role::Accidental, kScale);
                }
            }
            ledgerLines(grace, x, kScale);
            if (!hasStem(grace.duration.value())) {
                continue;
            }
            const Head& base = up ? grace.heads.back() : grace.heads.front();
            const Point anchor = stemAnchor(base, up);
            grace.stemX = x + anchor.x * kScale + (up ? -stemWidth / 2 : stemWidth / 2);
            grace.stemStart = yOf(base.step) - anchor.y * kScale;
            const double length = stemLengthFor(grace.duration.value()) * kScale;
            const double tip = beamed ? beamTip
                               : up   ? yOf(grace.heads.front().step) - length
                                      : yOf(grace.heads.back().step) + length;
            line({grace.stemX, grace.stemStart}, {grace.stemX, tip}, stemWidth, Role::Stem);
            if (!beamed && beamCount(grace.duration.value()) > 0) {
                const Glyph flag = flagFor(grace.duration.value(), up);
                const Point attach = metrics(flag)
                                         .anchor(up ? Anchor::StemUpNW : Anchor::StemDownSW)
                                         .value_or(Point{});
                glyph(flag, grace.stemX - stemWidth / 2 - attach.x * kScale,
                      tip + attach.y * kScale, Role::Flag, kScale);
            }
            if (grace.slash && (!beamed || &grace == &chord.graces.front())) {
                const double cx = grace.stemX + (up ? kSlashOff : -kSlashOff) * kScale;
                const double cy = tip + (up ? kSlashDrop : -kSlashDrop) * kScale;
                line({cx - kSlashRun * kScale, cy + kSlashRise * kScale},
                     {cx + kSlashRun * kScale, cy - kSlashRise * kScale}, stemWidth, Role::Flag);
            }
        }
        if (beamed) {
            const double thickness = defaults_.beamThickness * kScale;
            const double step = (defaults_.beamThickness + defaults_.beamSpacing) * kScale;
            const double left = chord.graces.front().stemX - stemWidth / 2;
            const double right = chord.graces.back().stemX + stemWidth / 2;
            for (int level = 0; level < beams; ++level) {
                const double y = up ? beamTip + thickness / 2 + level * step
                                    : beamTip - thickness / 2 - level * step;
                list_.beams.push_back(BeamItem{.left = {left, y},
                                               .right = {right, y},
                                               .thickness = thickness,
                                               .measure = measure_});
            }
        }
        if (legatoFromGrace(*chord.beat)) {
            graceSlur(chord, up);
        }
        graceSlides(chord);
    }

    // ---- Slides (ADR-0098) ----

    // A slide into a note goes left of the chord's ink and one out of it right
    // of it, which then reach over them so that the beats around make room.
    void makeRoomForSlides(Chord& chord) const {
        bool in = false;
        bool out = false;
        for (const Head& head : chord.heads) {
            if (head.note != nullptr) {
                const core::NoteEffects& effects = head.note->effects;
                in = in || effects.slideIn != core::SlideIn::None;
                out = out || effects.slideOut == core::SlideOut::Downwards ||
                      effects.slideOut == core::SlideOut::Upwards;
            }
        }
        if (in) {
            chord.left -= kSlideOff + kSlideGap;
        }
        if (out) {
            chord.right += kSlideGap + kSlideOff;
        }
    }

    // The left of a notehead of the chord, or of its accidental.
    [[nodiscard]] double inkLeftOf(const Chord& chord, const Head& head) const {
        if (head.accidental) {
            return chord.x + head.accidentalX + metrics(*head.accidental).box.southWest.x;
        }
        return chord.x + head.dx + metrics(head.glyph).box.southWest.x -
               (head.ghost ? chord.ghostPad : 0.0);
    }

    // The right of a notehead of the chord, after its parentheses and dots.
    [[nodiscard]] double inkRightOf(const Chord& chord, const Head& head) const {
        return chord.x + head.dx + metrics(head.glyph).box.northEast.x + chord.ghostPad +
               dotsWidth(chord.duration.dots());
    }

    void slideLine(Point from, Point to) {
        if (to.x - from.x > kEpsilon) {
            line(from, to, kSlideThickness, Role::Slide);
        }
    }

    // Draws the slides of a chord: the lines from the notes of its voice's
    // chord before to the notes they slide to in it, from the start of the
    // line when that chord ended the line before, and its notes' slides in
    // and out; then keeps where its notes' slides to the next chord start.
    void slides(const Chord& chord) {
        std::vector<SlideStart>& starts = slideStarts_[chord.voice];
        const bool incoming = std::exchange(slideIncoming_[chord.voice], false);
        if (chord.rest || chord.heads.empty()) {
            starts.clear();
            return;
        }
        const std::vector<core::Beat>& beats =
            staff_.measure(measureIndex_).voices[chord.voice].beats;
        if (incoming && measureIndex_ == lineFirst_ && measureIndex_ > 0 &&
            chord.beat == &beats.front()) {
            const std::vector<core::Beat>& before =
                staff_.measure(measureIndex_ - 1).voices[chord.voice].beats;
            if (!before.empty()) {
                for (const core::Note& note : before.back().notes) {
                    if (slidesOn(note)) {
                        starts.push_back(SlideStart{.at = {lineStart_ + kSlideGap, 0.0},
                                                    .note = &note,
                                                    .measure = measureIndex_ - 1,
                                                    .cut = true});
                    }
                }
            }
        }
        for (const SlideStart& start : starts) {
            if (start.measure + 1 < measureIndex_) {
                continue; // the voice had no beats in between
            }
            const auto target = slideTarget(*start.note, chord.heads.size(),
                                            [&](std::size_t i) { return chord.heads[i].note; });
            if (!target) {
                continue;
            }
            const Head& head = chord.heads[*target];
            const Point to{inkLeftOf(chord, head) - kSlideGap, yOf(head.step)};
            Point from = start.at;
            if (start.cut) {
                from.y = to.y - slideDirection(*start.note, *head.note) * kSlideBreak;
            }
            slideLine(from, to);
        }
        starts.clear();
        for (const Head& head : chord.heads) {
            if (head.note == nullptr) {
                continue;
            }
            const core::NoteEffects& effects = head.note->effects;
            const double y = yOf(head.step);
            if (effects.slideIn != core::SlideIn::None) {
                const double end = inkLeftOf(chord, head) - kSlideGap;
                const double below = effects.slideIn == core::SlideIn::FromBelow ? 1.0 : -1.0;
                slideLine({end - kSlideOff, y + below * kSlideDrop}, {end, y});
            }
            const double begin = inkRightOf(chord, head) + kSlideGap;
            if (effects.slideOut == core::SlideOut::Downwards ||
                effects.slideOut == core::SlideOut::Upwards) {
                const double down = effects.slideOut == core::SlideOut::Downwards ? 1.0 : -1.0;
                slideLine({begin, y}, {begin + kSlideOff, y + down * kSlideDrop});
            } else if (slidesOn(*head.note)) {
                starts.push_back(
                    SlideStart{.at = {begin, y}, .note = head.note, .measure = measureIndex_});
            }
        }
    }

    // At the end of a line: the lines from the last notes of each voice to
    // the notes of the next measure they slide to, cut at `end`.
    void slidesOut(double end) {
        measure_ = measureIndex_;
        const std::size_t next = measureIndex_ + 1;
        for (std::size_t v = 0; v < slideStarts_.size(); ++v) {
            std::vector<SlideStart>& starts = slideStarts_[v];
            if (next < song_.measureCount() && !staff_.measure(next).voices[v].beats.empty()) {
                const std::vector<core::Note>& notes =
                    staff_.measure(next).voices[v].beats.front().notes;
                for (const SlideStart& start : starts) {
                    if (start.measure != measureIndex_) {
                        continue;
                    }
                    const auto target = slideTarget(*start.note, notes.size(),
                                                    [&](std::size_t i) { return &notes[i]; });
                    if (target) {
                        const double way = slideDirection(*start.note, notes[*target]);
                        slideLine(start.at, {end, start.at.y + way * kSlideBreak});
                    }
                }
            }
            starts.clear();
        }
    }

    // Lines from the last grace note of a chord to the notes it slides to.
    void graceSlides(const Chord& chord) {
        const Chord& grace = chord.graces.back();
        for (const Head& from : grace.heads) {
            if (from.note == nullptr || !slidesOn(*from.note)) {
                continue;
            }
            const auto target = slideTarget(*from.note, chord.heads.size(),
                                            [&](std::size_t i) { return chord.heads[i].note; });
            if (!target) {
                continue;
            }
            const Head& to = chord.heads[*target];
            const double x =
                chord.x + grace.x +
                (from.dx + metrics(from.glyph).box.northEast.x + kSlideGap) * kGraceScale;
            slideLine({x, yOf(from.step)}, {inkLeftOf(chord, to) - kSlideGap, yOf(to.step)});
        }
    }

    // Stems and beams of a beam group: the beam follows the melody, slanted by
    // half the interval between its outer notes (one staff space at most, flat
    // when an inner note reaches beyond both), and lies far enough from every
    // notehead.
    BeamLine beam(const std::vector<std::size_t>& group) {
        const bool up = chords_[group.front()].stemUp;
        const double stemWidth = defaults_.stemThickness;
        const double thickness = defaults_.beamThickness;
        int levels = 1;
        for (const std::size_t c : group) {
            levels = std::max(levels, beamCount(chords_[c].duration.value()));
        }
        const auto end = [&](const Chord& chord) {
            return yOf(up ? chord.heads.front().step : chord.heads.back().step);
        };
        const Chord& first = chords_[group.front()];
        const Chord& last = chords_[group.back()];
        double slant = std::clamp((end(last) - end(first)) / 2, -kMaxBeamSlant, kMaxBeamSlant);
        for (std::size_t i = 1; i + 1 < group.size(); ++i) {
            const double inner = end(chords_[group[i]]);
            if (up ? inner < std::min(end(first), end(last)) - kEpsilon
                   : inner > std::max(end(first), end(last)) + kEpsilon) {
                slant = 0.0;
            }
        }
        const double span = last.stemX - first.stemX;
        if (span <= kEpsilon) {
            slant = 0.0;
        }
        const double slope = slant == 0.0 ? 0.0 : slant / span;
        const double minimum = kStemLength + kBeamExtraStem * (levels - 1);
        // The beams over a chord's stem, from the outer edge of the primary one.
        const double step = thickness + defaults_.beamSpacing;
        const auto zone = [&](const Chord& chord) {
            return thickness + (beamCount(chord.duration.value()) - 1) * step;
        };
        // The outer edge of the primary beam: y = edge + slope * (x - first.stemX).
        double edge =
            up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
        for (const std::size_t c : group) {
            const Chord& chord = chords_[c];
            // Tremolo slashes lengthen the stem to stay clear of the beams.
            const double length = std::max(minimum, tremoloStem(chord, zone(chord)));
            const double needed =
                end(chord) + (up ? -length : length) - slope * (chord.stemX - first.stemX);
            edge = up ? std::min(edge, needed) : std::max(edge, needed);
        }
        // Notes far outside the staff: the beam still reaches the middle line.
        const double nearest = up ? std::max(edge, edge + slant) : std::min(edge, edge + slant);
        if (up && nearest > kMiddleLine) {
            edge -= nearest - kMiddleLine;
        } else if (!up && nearest < kMiddleLine) {
            edge += kMiddleLine - nearest;
        }
        const BeamLine beamLine{.edge = edge, .slope = slope, .x0 = first.stemX, .up = up};
        const auto edgeAt = [&](double x) {
            return beamLine.at(x);
        };
        for (const std::size_t c : group) {
            Chord& chord = chords_[c];
            const double tip = edgeAt(chord.stemX);
            line({chord.stemX, chord.stemStart}, {chord.stemX, tip}, stemWidth, Role::Stem);
            chord.inkTop = std::min(chord.inkTop, tip);
            chord.inkBottom = std::max(chord.inkBottom, tip);
            tremolo(chord, chord.stemX, tip + (up ? zone(chord) : -zone(chord)));
        }
        for (int level = 1; level <= levels; ++level) {
            const double offset = (up ? 1.0 : -1.0) * (thickness / 2 + (level - 1) * step);
            const auto add = [&](double left, double right) {
                list_.beams.push_back(BeamItem{.left = {left, edgeAt(left) + offset},
                                               .right = {right, edgeAt(right) + offset},
                                               .thickness = thickness,
                                               .measure = measure_});
            };
            std::size_t i = 0;
            while (i < group.size()) {
                if (beamCount(chords_[group[i]].duration.value()) < level) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j + 1 < group.size() &&
                       beamCount(chords_[group[j + 1]].duration.value()) >= level) {
                    ++j;
                }
                const double leftStem = chords_[group[i]].stemX;
                const double rightStem = chords_[group[j]].stemX;
                if (j > i) {
                    add(leftStem - stemWidth / 2, rightStem + stemWidth / 2);
                } else if (i == 0) {
                    add(leftStem - stemWidth / 2, leftStem - stemWidth / 2 + kBeamletLength);
                } else {
                    add(rightStem + stemWidth / 2 - kBeamletLength, rightStem + stemWidth / 2);
                }
                i = j + 1;
            }
        }
        return beamLine;
    }

    // ---- Tuplets ----

    // The number of a tuplet on the stem side: alone beyond the beam when one
    // beam carries the whole group and only tuplets, else in the gap of a
    // bracket beyond the notes from the first notehead to the last.
    void tuplet(const TupletSpan& group) {
        const Chord& first = chords_[group.first];
        const Chord& last = chords_[group.last];
        const int beamGroup = first.beamGroup;
        bool beamed = beamGroup >= 0;
        for (std::size_t c = group.first; beamed && c <= group.last; ++c) {
            beamed = chords_[c].beamGroup == beamGroup;
        }
        if (beamed) {
            beamed = std::ranges::none_of(
                beamGroups_[static_cast<std::size_t>(beamGroup)],
                [this](std::size_t c) { return chords_[c].duration.tuplet().isNone(); });
        }
        const TupletNumber number = tupletNumber(group.tuplet.actual());
        if (beamed) {
            const BeamLine& beam = beamLines_[static_cast<std::size_t>(beamGroup)];
            const double x = (first.stemX + last.stemX) / 2;
            const double edge = beam.at(x);
            drawTupletNumber(number, x,
                             beam.up ? edge - kTupletGap + number.bottom
                                     : edge + kTupletGap + number.top);
            return;
        }
        bool up = voiceCount_ > 1 ? voiceUp_[first.voice] : true;
        for (std::size_t c = group.first; c <= group.last; ++c) {
            if (!chords_[c].rest) {
                up = chords_[c].stemUp;
                break;
            }
        }
        double closest =
            up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
        for (std::size_t c = group.first; c <= group.last; ++c) {
            closest =
                up ? std::min(closest, chords_[c].inkTop) : std::max(closest, chords_[c].inkBottom);
        }
        const double reach = bracketReach(number);
        tupletBracket(number, first.x + first.headLeft, last.x + last.headRight,
                      up ? closest - reach : closest + reach, up);
    }

    // ---- Ties, articulations and techniques ----

    // Ties the tied noteheads of `chord` to the same notes of the previous chord
    // of its voice (or to the start of the line for its first chord), then
    // remembers its noteheads for the next chord.
    void ties(const Chord& chord) {
        std::vector<TieStart>& previous = tieStarts_[chord.voice];
        const bool incoming = std::exchange(incoming_[chord.voice], false);
        const std::size_t count = chord.heads.size();
        for (std::size_t i = 0; i < count; ++i) {
            const Head& head = chord.heads[i];
            if (!head.tied) {
                continue;
            }
            const auto from = std::ranges::find(previous, head.step, &TieStart::step);
            if (from == previous.end() && !incoming) {
                continue;
            }
            const double left = from == previous.end() ? lineStart_ : from->right;
            const double right = chord.x + head.dx + metrics(head.glyph).box.southWest.x -
                                 (head.ghost ? chord.ghostPad : 0.0);
            tie(head.step, tieAbove(chord, i), left + kTieGap, right - kTieGap);
        }
        previous.clear();
        if (chord.rest) {
            return;
        }
        const double dots = dotsWidth(chord.duration.dots());
        for (std::size_t i = 0; i < count; ++i) {
            const Head& head = chord.heads[i];
            previous.push_back(TieStart{.step = head.step,
                                        .right = chord.x + head.dx +
                                                 metrics(head.glyph).box.northEast.x +
                                                 chord.ghostPad + dots,
                                        .above = tieAbove(chord, i)});
        }
    }

    // The outer notes of a chord tie away from it; a single note (and the
    // middle one) away from its stem.
    [[nodiscard]] static bool tieAbove(const Chord& chord, std::size_t head) {
        const std::size_t count = chord.heads.size();
        if (2 * head + 1 != count) {
            return 2 * head + 1 < count;
        }
        return !chord.stemUp;
    }

    void tie(int step, bool above, double left, double right) {
        if (right - left < kEpsilon) {
            return;
        }
        const double direction = above ? -1.0 : 1.0;
        const double y = yOf(step) + direction * kTieOffset;
        const double height = std::clamp((right - left) * kTieRise, kTieMinHeight, kTieMaxHeight);
        curve({left, y}, {right, y}, direction * height, defaults_.tieMidpointThickness,
              defaults_.tieEndpointThickness, Role::Tie);
    }

    // At the end of a line: the first half of the ties the first notes of the
    // next measure continue, up to `end`.
    void tiesOut(double end) {
        const std::size_t next = measureIndex_ + 1;
        if (next >= song_.measureCount()) {
            return;
        }
        measure_ = measureIndex_;
        const core::Measure& measure = staff_.measure(next);
        for (std::size_t v = 0; v < measure.voices.size(); ++v) {
            const std::vector<TieStart>& starts = tieStarts_[v];
            const std::vector<Beat>& beats = measure.voices[v].beats;
            if (starts.empty() || beats.empty()) {
                continue;
            }
            for (const core::Note& note : beats.front().notes) {
                if (!note.effects.tiedFromPrevious) {
                    continue;
                }
                const int step =
                    headOf(note, beats.front().duration.value(), beats.front().effects.ottava).step;
                const auto from = std::ranges::find(starts, step, &TieStart::step);
                if (from != starts.end()) {
                    tie(step, from->above, from->right + kTieGap, end);
                }
            }
        }
    }

    // Staccato, tenuto and accents on the notehead side of the chord: the
    // staccato dot nearest, or the staccatissimo wedge in its place (ADR-0094),
    // the tenuto line beyond it (both: portato, ADR-0088), each in a space
    // when inside the staff, the accent outside.
    void articulations(Chord& chord) {
        if (chord.rest || chord.heads.empty()) {
            return;
        }
        bool staccato = false;
        bool staccatissimo = false;
        bool tenuto = false;
        core::Accent accent = core::Accent::None;
        for (const Head& head : chord.heads) {
            staccato = staccato || head.note->effects.staccato;
            staccatissimo = staccatissimo || head.note->effects.staccatissimo;
            tenuto = tenuto || head.note->effects.tenuto;
            accent = std::max(accent, head.note->effects.accent);
        }
        if (!staccato && !staccatissimo && !tenuto && accent == core::Accent::None) {
            return;
        }
        const bool above = !chord.stemUp;
        const double direction = above ? -1.0 : 1.0;
        const Head& outer = above ? chord.heads.front() : chord.heads.back();
        const BoundingBox& head = metrics(outer.glyph).box;
        const double centre = chord.x + outer.dx + (head.southWest.x + head.northEast.x) / 2;
        double edge = yOf(outer.step) + direction * (0.5 + kArticulationGap);
        int target = outer.step;
        const auto inSpace = [&](Glyph mark) {
            target += above ? -2 : 2;
            if (target % 2 == 0 && target >= 0 && target <= 2 * kMiddleStep) {
                target += above ? -1 : 1;
            }
            glyphCentred(mark, centre, yOf(target), Role::Articulation);
            chord.marks = yOf(target) + direction * metrics(mark).box.height() / 2;
            edge = *chord.marks + direction * kArticulationGap;
        };
        if (staccatissimo) {
            inSpace(above ? Glyph::ArticStaccatissimoAbove : Glyph::ArticStaccatissimoBelow);
        } else if (staccato) {
            inSpace(above ? Glyph::ArticStaccatoAbove : Glyph::ArticStaccatoBelow);
        }
        if (tenuto) {
            inSpace(above ? Glyph::ArticTenutoAbove : Glyph::ArticTenutoBelow);
        }
        if (accent == core::Accent::None) {
            return;
        }
        const Glyph mark = accent == core::Accent::Marcato
                               ? (above ? Glyph::ArticMarcatoAbove : Glyph::ArticMarcatoBelow)
                               : (above ? Glyph::ArticAccentAbove : Glyph::ArticAccentBelow);
        const BoundingBox& box = metrics(mark).box;
        glyph(mark, centre - (box.southWest.x + box.northEast.x) / 2,
              edge + (above ? box.southWest.y : box.northEast.y), Role::Articulation);
        chord.marks = edge + direction * box.height();
    }

    // ---- Slurs (ADR-0086) ----

    [[nodiscard]] SlurNote slurNote(const Chord& chord) const {
        SlurNote note;
        note.voiceSide = voiceCount_ > 1 ? (voiceUp_[chord.voice] ? -1 : 1) : 0;
        note.rest = chord.rest || chord.heads.empty();
        if (chord.measureRest) { // drawn later, in the middle of its measure
            note.x = chord.x;
            note.headTop = note.inkTop = 1.0;
            note.headBottom = note.inkBottom = 2.0;
            return note;
        }
        if (note.rest) {
            note.x = chord.x + (chord.left + chord.right) / 2;
            note.halfWidth = (chord.right - chord.left) / 2;
            note.headTop = note.inkTop = chord.inkTop;
            note.headBottom = note.inkBottom = chord.inkBottom;
            return note;
        }
        note.x = chord.x + (chord.headLeft + chord.headRight) / 2;
        note.halfWidth = (chord.headRight - chord.headLeft) / 2;
        note.stemUp = chord.stemUp;
        note.stem = hasStem(chord.duration.value());
        note.stemX = chord.stemX;
        note.headTop = yOf(chord.heads.front().step) - 0.5;
        note.headBottom = yOf(chord.heads.back().step) + 0.5;
        note.inkTop = chord.inkTop;
        note.inkBottom = chord.inkBottom;
        if (chord.marks) {
            if (chord.stemUp) {
                note.headBottom = std::max(note.headBottom, *chord.marks);
                note.inkBottom = std::max(note.inkBottom, *chord.marks);
            } else {
                note.headTop = std::min(note.headTop, *chord.marks);
                note.inkTop = std::min(note.inkTop, *chord.marks);
            }
        }
        return note;
    }

    // The beat after `beat` of voice v in measure m: the next in the measure,
    // else the voice's first in the next measure; null at the end of the song
    // or of the voice.
    [[nodiscard]] const core::Beat* beatAfter(std::size_t m, std::size_t v,
                                              const core::Beat& beat) const {
        const std::vector<core::Beat>& beats = staff_.measure(m).voices[v].beats;
        const auto i = static_cast<std::size_t>(&beat - beats.data());
        if (i + 1 < beats.size()) {
            return &beats[i + 1];
        }
        if (m + 1 < song_.measureCount()) {
            const std::vector<core::Beat>& next = staff_.measure(m + 1).voices[v].beats;
            return next.empty() ? nullptr : &next.front();
        }
        return nullptr;
    }

    // A slur from the last grace note of a chord to its noteheads, played
    // legato from it (ADR-0097): on the side away from the grace notes' stems,
    // ending before the chord's stem where that comes down on the same side.
    void graceSlur(const Chord& chord, bool graceUp) {
        const Chord& grace = chord.graces.back();
        if (grace.heads.empty() || chord.heads.empty()) {
            return;
        }
        const double direction = graceUp ? 1.0 : -1.0;
        const Head& start = graceUp ? grace.heads.back() : grace.heads.front();
        const Head& end = graceUp ? chord.heads.back() : chord.heads.front();
        const Point from{chord.x + grace.x + (start.dx + widthOf(start.glyph) / 2) * kGraceScale,
                         yOf(start.step) + direction * (0.5 * kGraceScale + kSlurGap)};
        Point to{chord.x + (chord.headLeft + chord.headRight) / 2,
                 yOf(end.step) + direction * (0.5 + kSlurGap)};
        if (hasStem(chord.duration.value()) && chord.stemUp != graceUp) {
            to = {chord.stemX - kSlurGap, yOf(end.step) + direction * 0.5};
        }
        if (to.x - from.x < kEpsilon) {
            return;
        }
        curve(from, to, direction * kGraceSlurHeight, defaults_.slurMidpointThickness * kGraceScale,
              defaults_.slurEndpointThickness, Role::Slur);
    }

    // Gathers the beats of the slur going on in each voice and draws it when
    // the beat after its last slurred beat comes. A voice without beats in the
    // measure ends its slur at its last beat.
    void slurs() {
        std::array<bool, core::Measure::kMaxVoices> seen{};
        for (const Chord& chord : chords_) {
            if (chord.beat == nullptr) {
                continue; // a measure no voice has notes in
            }
            const std::size_t v = chord.voice;
            seen[v] = true;
            const bool slurred = slurredTo(*chord.beat, beatAfter(measureIndex_, v, *chord.beat));
            if (!slurOpen_[v] && !slurred) {
                continue;
            }
            slurOpen_[v] = true;
            slurNotes_[v].push_back(slurNote(chord));
            if (!slurred) {
                slur(v, std::nullopt);
            }
        }
        for (std::size_t v = 0; v < seen.size(); ++v) {
            if (slurOpen_[v] && !seen[v]) {
                slur(v, std::nullopt);
            }
        }
    }

    // At the end of a line: the slurs that go on into the next measure, cut
    // at `end`; those with nothing after them end at their last beat.
    void slursOut(double end) {
        measure_ = measureIndex_;
        const std::size_t next = measureIndex_ + 1;
        for (std::size_t v = 0; v < slurOpen_.size(); ++v) {
            if (!slurOpen_[v]) {
                continue;
            }
            const bool goesOn =
                next < song_.measureCount() && !staff_.measure(next).voices[v].beats.empty();
            slur(v, goesOn ? std::optional(end) : std::nullopt);
        }
    }

    // How far up its height a curve() is a fraction u of the way across:
    // 4t(1 - t) at the t where its x reaches u.
    [[nodiscard]] static double slurBulge(double u) {
        double low = 0.0;
        double high = 1.0;
        for (int i = 0; i < 40; ++i) {
            const double t = (low + high) / 2;
            const double s = 1.0 - t;
            const double x = 0.75 * t * s * s + 2.25 * t * t * s + t * t * t;
            (x < u ? low : high) = t;
        }
        const double t = (low + high) / 2;
        return 4.0 * t * (1.0 - t);
    }

    // Draws the slur of voice v over the beats gathered for it, from the start
    // of the line when it came from the line before and to `lineEnd` when it
    // goes on in the next. It goes on the notehead side when all its stems go
    // up, above otherwise, and above (below) for the upper (lower) voice of
    // several; it arches over the notes between its ends, and when that would
    // take it too high it moves away from them whole.
    void slur(std::size_t v, std::optional<double> lineEnd) {
        const std::vector<SlurNote> notes = std::exchange(slurNotes_[v], {});
        slurOpen_[v] = false;
        const bool incoming = std::exchange(slurIncoming_[v], false);
        const std::size_t ends = notes.size() + (incoming ? 1 : 0) + (lineEnd ? 1 : 0);
        if (notes.empty() || ends < 2) {
            return;
        }
        bool above = true;
        if (notes.front().voiceSide != 0) {
            above = notes.front().voiceSide < 0;
        } else {
            bool sounding = false;
            bool allUp = true;
            for (const SlurNote& note : notes) {
                if (!note.rest) {
                    sounding = true;
                    allUp = allUp && note.stemUp;
                }
            }
            above = !(sounding && allUp);
        }
        const double direction = above ? -1.0 : 1.0;
        const auto endOf = [&](const SlurNote& note) {
            if (!note.rest && note.stem && note.stemUp == above) { // at the tip of its stem
                return Point{note.stemX,
                             (above ? note.inkTop : note.inkBottom) + direction * kSlurGap};
            }
            if (note.rest) {
                return Point{note.x, (above ? note.inkTop : note.inkBottom) + direction * kSlurGap};
            }
            return Point{note.x, (above ? note.headTop : note.headBottom) + direction * kSlurGap};
        };
        Point from = endOf(notes.front());
        Point to = endOf(notes.back());
        if (incoming) {
            from.x = lineStart_ + kSlurLineGap;
        }
        if (lineEnd) {
            to.x = *lineEnd;
        }
        const double width = to.x - from.x;
        if (width < kEpsilon) {
            return;
        }
        // What lies between its ends: each note's sides, where the slur has to
        // clear its ink.
        struct Clear {
            double u = 0.0;
            double y = 0.0;
        };
        std::vector<Clear> between;
        const std::size_t first = incoming ? 0 : 1;
        const std::size_t last = lineEnd ? notes.size() : notes.size() - 1;
        for (std::size_t i = first; i < last; ++i) {
            const SlurNote& note = notes[i];
            const double y = (above ? note.inkTop : note.inkBottom) + direction * kSlurClearance;
            for (const double x : {note.x - note.halfWidth, note.x + note.halfWidth}) {
                const double u = (x - from.x) / width;
                if (u > 0.0 && u < 1.0) {
                    between.push_back({.u = u, .y = y});
                }
            }
        }
        const auto lineAt = [&](double u) {
            return from.y + (to.y - from.y) * u;
        };
        double height =
            direction * std::clamp(width * kSlurRise, kSlurMinHeight, kSlurNaturalHeight);
        for (const Clear& clear : between) {
            const double needed = (clear.y - lineAt(clear.u)) / slurBulge(clear.u);
            height = above ? std::min(height, needed) : std::max(height, needed);
        }
        if (std::abs(height) > kSlurMaxHeight) {
            height = direction * kSlurMaxHeight;
            double away = 0.0;
            for (const Clear& clear : between) {
                const double curveY = lineAt(clear.u) + height * slurBulge(clear.u);
                away = std::max(away, direction * (clear.y - curveY));
            }
            from.y += direction * away;
            to.y += direction * away;
        }
        curve(from, to, height, defaults_.slurMidpointThickness, defaults_.slurEndpointThickness,
              Role::Slur);
    }

    // Palm mutes, let ring, vibrato and pick strokes of the measure's columns.
    void markTechniques(std::span<const ColumnPosition> positions, double barline) {
        if (columns_.empty()) {
            lanes_.column({}, barline, barline, measure_);
            return;
        }
        const double centre = widthOf(Glyph::NoteheadBlack) / 2;
        for (const Column& column : columns_) {
            ColumnTechniques marks;
            for (const std::size_t c : column.chords) {
                if (!chords_[c].rest) {
                    addTechniques(marks, *chords_[c].beat);
                }
            }
            if (rolls_) {
                marks.strum = core::Strum::None; // an arpeggio line instead of an arrow
            }
            marks.tremoloStrokes = 0; // slashes through the stems instead (ADR-0101)
            if (lanes_.wants(marks)) {
                const double end = nextColumnX(positions, column.start, barline) - kRunEndGap;
                lanes_.column(marks, column.x + centre, end, measure_);
            }
        }
    }

    // The dynamics of the measure's columns (ADR-0044).
    void strikeDynamics() {
        const double centre = widthOf(Glyph::NoteheadBlack) / 2;
        for (const Column& column : columns_) {
            std::optional<core::Dynamic> loudest;
            for (const std::size_t c : column.chords) {
                if (!chords_[c].rest) {
                    addStruck(loudest, *chords_[c].beat);
                }
            }
            if (loudest) {
                dynamics_.strike(*loudest, column.x + centre, measure_);
            }
        }
    }

    // ---- Octave lines (ADR-0089) ----

    struct OctaveSpan {
        Fraction start;
        Fraction lastStart; // of its last beat
        Fraction end;
        core::Ottava ottava = core::Ottava::None;
    };

    struct OctaveRun {
        core::Ottava ottava = core::Ottava::None;
        double left = 0.0;       // the column of its first beat
        double right = 0.0;      // the end of the notehead of its last beat
        std::size_t measure = 0; // where it starts
        std::size_t last = 0;    // where it ends
        bool toBarline = false;  // its last beat ends `last`
    };

    // Whether a voice of measure m starts with a beat under `ottava`.
    [[nodiscard]] bool startsUnder(std::size_t m, core::Ottava ottava) const {
        return m < staff_.measureCount() &&
               std::ranges::any_of(
                   staff_.measure(m).voices,
                   [ottava](const core::Voice& voice) {
                       return !voice.beats.empty() && voice.beats.front().effects.ottava == ottava;
                   });
    }

    // The octave lines of the measure drawn last, one per run of beats under
    // the same line in any voice; a run reaching the barline goes on into the
    // next measure when it starts under the same line.
    void octaveLines(std::span<const ColumnPosition> positions) {
        if (percussion_) {
            return;
        }
        octaveSpans_.clear();
        for (const core::Voice& voice : staff_.measure(measureIndex_).voices) {
            Fraction at;
            for (const Beat& beat : voice.beats) {
                const Fraction end = at + beat.duration.length();
                if (beat.effects.ottava != core::Ottava::None) {
                    octaveSpans_.push_back(OctaveSpan{
                        .start = at, .lastStart = at, .end = end, .ottava = beat.effects.ottava});
                }
                at = end;
            }
        }
        if (octaveSpans_.empty()) {
            return;
        }
        std::ranges::stable_sort(octaveSpans_, std::less<>{}, &OctaveSpan::start);
        std::size_t kept = 0;
        for (std::size_t i = 1; i < octaveSpans_.size(); ++i) {
            OctaveSpan& last = octaveSpans_[kept];
            if (octaveSpans_[i].ottava == last.ottava && octaveSpans_[i].start <= last.end) {
                last.end = std::max(last.end, octaveSpans_[i].end);
                last.lastStart = std::max(last.lastStart, octaveSpans_[i].lastStart);
            } else {
                octaveSpans_[++kept] = octaveSpans_[i];
            }
        }
        octaveSpans_.resize(kept + 1);
        const double headWidth = metrics(Glyph::NoteheadBlack).box.northEast.x;
        for (const OctaveSpan& span : octaveSpans_) {
            const double right = columnX(positions, span.lastStart) + headWidth;
            const bool toBarline =
                std::ranges::lower_bound(positions, span.end, std::less<>{},
                                         &ColumnPosition::start) == positions.end();
            if (span.start.isZero() && !octaveRuns_.empty()) {
                OctaveRun& last = octaveRuns_.back();
                if (last.ottava == span.ottava && last.toBarline &&
                    last.last + 1 == measureIndex_) {
                    last.right = right; // over the barline
                    last.last = measureIndex_;
                    last.toBarline = toBarline;
                    continue;
                }
            }
            octaveRuns_.push_back(OctaveRun{.ottava = span.ottava,
                                            .left = columnX(positions, span.start),
                                            .right = right,
                                            .measure = measureIndex_,
                                            .last = measureIndex_,
                                            .toBarline = toBarline});
        }
    }

    // Draws the octave lines of the line ending at `width`: the mark over (or
    // under) the first beat, clear of the ink, then dashes to the end of the
    // last note and a hook towards the staff. A line going on into the next
    // system runs to the end of this one without a hook; the next starts with
    // the mark again.
    void octaveLinesOut(double width) {
        const double thickness = defaults_.octaveLineThickness;
        for (const OctaveRun& run : octaveRuns_) {
            using enum core::Ottava;
            const bool above = run.ottava == Alta || run.ottava == QuindicesimaAlta;
            const Glyph mark = run.ottava == Alta               ? Glyph::OttavaAlta
                               : run.ottava == Bassa            ? Glyph::OttavaBassaVb
                               : run.ottava == QuindicesimaAlta ? Glyph::QuindicesimaAlta
                                                                : Glyph::QuindicesimaBassaMb;
            const BoundingBox& box = metrics(mark).box;
            const bool cut = run.toBarline && startsUnder(run.last + 1, run.ottava);
            const double from = run.left + box.width() + kOctaveLineGap;
            const double to = cut ? width : std::max(run.right, from + kOctaveMinLine);
            const Extents ink = inkBetween(run.left, to);
            // The mark kOctaveGap off the ink, the dashes through its middle
            // and the hook back to the level of its edge nearest the staff.
            const double edge = above ? std::min(ink.top, 0.0) - kOctaveGap
                                      : std::max(ink.bottom, height()) + kOctaveGap;
            const double y = above ? edge + box.southWest.y : edge + box.northEast.y;
            const double middle = y - (box.southWest.y + box.northEast.y) / 2;
            measure_ = run.measure;
            glyph(mark, run.left - box.southWest.x, y, Role::OctaveLine);
            line({from, middle}, {to, middle}, thickness, Role::OctaveLine, kOctaveDash);
            if (!cut) {
                line({to, middle}, {to, edge}, thickness, Role::OctaveLine);
            }
        }
    }

    struct PedalRun {
        double left = 0.0;           // the column of its first beat
        double right = 0.0;          // just before the column where it comes up
        std::vector<double> changes; // the columns where it is changed
        std::size_t measure = 0;     // where it starts
        std::size_t last = 0;        // where it ends
        Fraction end;                // where it comes up in `last`
        bool toBarline = false;      // at the barline of `last`
    };

    // What the pedal of measure m reaches under its columns (ADR-0092): "Ped."
    // and the start of its line right of where it goes down, a notch on both
    // sides of a change, the end of the line left of where it comes up.
    void pedalInk(std::size_t m, std::vector<ColumnInk>& ink) const {
        if (percussion_) {
            return;
        }
        const std::vector<core::PedalSpan> spans = core::pedalSpans(staff_.measure(m));
        const double mark = widthOf(Glyph::KeyboardPedalPed) + kPedalLineGap;
        const auto reach = [&ink](const Fraction& start, double left, double right) {
            const auto column = std::ranges::lower_bound(ink, start, {}, &ColumnInk::start);
            if (column == ink.end() || column->start != start) {
                return; // no beat starts there
            }
            column->pedalLeft = column->pedal ? std::min(column->pedalLeft, left) : left;
            column->pedalRight = column->pedal ? std::max(column->pedalRight, right) : right;
            column->pedal = true;
        };
        std::optional<Fraction> end; // of the span before
        for (const core::PedalSpan& span : spans) {
            if (span.change && end == span.start) {
                reach(span.start, -kPedalNotch, kPedalNotch);
            } else {
                reach(span.start, 0.0, mark);
            }
            reach(span.end, -kPedalEndGap, 0.0);
            end = span.end;
        }
    }

    // Whether measure m starts with a beat under the pedal.
    [[nodiscard]] bool startsPedalled(std::size_t m) const {
        if (m >= staff_.measureCount()) {
            return false;
        }
        const std::vector<core::PedalSpan> spans = core::pedalSpans(staff_.measure(m));
        return !spans.empty() && spans.front().start.isZero();
    }

    // The pedallings of the measure drawn last: the beats of the staff's
    // voices under the pedal, a change in one notching it; one coming up at the
    // barline goes on into the next measure when that starts under it.
    void pedalLines(std::span<const ColumnPosition> positions, double barline) {
        if (percussion_) {
            return;
        }
        for (const core::PedalSpan& span : core::pedalSpans(staff_.measure(measureIndex_))) {
            const double from = columnX(positions, span.start);
            const bool toBarline =
                std::ranges::lower_bound(positions, span.end, std::less<>{},
                                         &ColumnPosition::start) == positions.end();
            const double right = toBarline ? barline : columnX(positions, span.end) - kPedalEndGap;
            if (!pedalRuns_.empty()) {
                PedalRun& last = pedalRuns_.back();
                const bool meets = span.start.isZero()
                                       ? last.toBarline && last.last + 1 == measureIndex_
                                       : last.last == measureIndex_ && last.end == span.start;
                if (meets) {
                    if (span.change) {
                        last.changes.push_back(from);
                    }
                    last.right = right;
                    last.last = measureIndex_;
                    last.end = span.end;
                    last.toBarline = toBarline;
                    continue;
                }
            }
            pedalRuns_.push_back(PedalRun{.left = from,
                                          .right = right,
                                          .changes = {},
                                          .measure = measureIndex_,
                                          .last = measureIndex_,
                                          .end = span.end,
                                          .toBarline = toBarline});
        }
    }

    // Draws the pedallings of the line ending at `width` under the ink and the
    // dynamics: "Ped." at the first beat, a line along its foot to where the
    // pedal comes up and a hook up, a notch at each change. One going on into
    // the next system runs to the end of this one without a hook; the next
    // starts with "Ped." again.
    void pedalLinesOut(double width) {
        const double thickness = defaults_.pedalLineThickness;
        const BoundingBox& box = metrics(Glyph::KeyboardPedalPed).box;
        for (const PedalRun& run : pedalRuns_) {
            const bool cut = run.toBarline && startsPedalled(run.last + 1);
            const double from = run.left + box.width() + kPedalLineGap;
            const double to = cut ? width : std::max(run.right, from + kPedalMinLine);
            const Extents ink = inkBetween(run.left, to);
            const double y = std::max(ink.bottom, height()) + kPedalGap + box.northEast.y;
            const double level = y - box.southWest.y; // the foot of "Ped."
            measure_ = run.measure;
            glyph(Glyph::KeyboardPedalPed, run.left - box.southWest.x, y, Role::Pedal);
            double x = from;
            for (const double change : run.changes) {
                if (change - kPedalNotch < x || change + kPedalNotch > to) {
                    continue; // no room for it next to "Ped." or the end
                }
                const Point peak{change, level - kPedalNotchHeight};
                line({x, level}, {change - kPedalNotch, level}, thickness, Role::Pedal);
                line({change - kPedalNotch, level}, peak, thickness, Role::Pedal);
                line(peak, {change + kPedalNotch, level}, thickness, Role::Pedal);
                x = change + kPedalNotch;
            }
            line({x, level}, {to, level}, thickness, Role::Pedal);
            if (!cut) {
                line({to, level}, {to, level - kPedalHook}, thickness, Role::Pedal);
            }
        }
    }

    struct TieStart {
        int step = 0;
        double right = 0.0; // after the notehead and its dots
        bool above = false; // the side the tie leaves on
    };

    // A note sliding to the next note on its string (ADR-0098): where its line
    // starts, in which measure, and whether the line is cut, coming from the
    // line before (its height then follows the note it goes to).
    struct SlideStart {
        Point at;
        const core::Note* note = nullptr;
        std::size_t measure = 0;
        bool cut = false;
    };

    const core::Song& song_;
    const core::Staff& staff_;
    Clef clef_;                       // in force at the measure being drawn
    Clef clefCached_ = staff_.clef(); // clefIn(): at clefMeasure_
    std::size_t clefMeasure_ = static_cast<std::size_t>(-1);
    bool fretting_;   // fingerings are of a fretting hand, the plucking one's apart (ADR-0093)
    bool lowerStaff_; // a staff under the track's first, whose fingerings go under it
    bool percussion_;
    int offset_;
    bool techniques_;
    bool rolls_; // strums are arpeggio lines (ADR-0076), not arrows above the staff
    TechniqueLanes lanes_;
    DynamicMarks dynamics_;
    AboveRow aboveRow_; // chord symbols and beat text (ADR-0080, ADR-0081)
    LyricsRow lyrics_;  // ADR-0084
    std::size_t measureIndex_ = 0;
    core::MeasureRepeat repeat_ = core::MeasureRepeat::None; // the sign of the measure read
    std::size_t voiceCount_ = 0;
    std::array<std::vector<TieStart>, core::Measure::kMaxVoices> tieStarts_{};
    std::array<bool, core::Measure::kMaxVoices> incoming_{}; // ties from the line before
    // Per voice, the slides of its last chord to the next, and whether its
    // first chord on the line takes slides from the line before (ADR-0098).
    std::array<std::vector<SlideStart>, core::Measure::kMaxVoices> slideStarts_{};
    std::array<bool, core::Measure::kMaxVoices> slideIncoming_{};
    std::size_t lineFirst_ = 0; // the line's first measure
    // Per voice, the beats of the slur going on (ADR-0086), whether one is
    // going on and whether it came from the line before.
    std::array<std::vector<SlurNote>, core::Measure::kMaxVoices> slurNotes_{};
    std::array<bool, core::Measure::kMaxVoices> slurOpen_{};
    std::array<bool, core::Measure::kMaxVoices> slurIncoming_{};
    double lineStart_ = 0.0;              // where the header ends
    std::vector<OctaveSpan> octaveSpans_; // of the measure being drawn
    std::vector<OctaveRun> octaveRuns_;   // of the line
    std::vector<PedalRun> pedalRuns_;     // of the line (ADR-0092)
    std::array<bool, core::Measure::kMaxVoices> voiceUp_{};
    std::vector<Chord> chords_;
    std::vector<Column> columns_;
    std::vector<std::vector<std::size_t>> beamGroups_;
    std::vector<BeamLine> beamLines_; // of the beam groups, as drawn
    std::vector<TupletSpan> tuplets_;
    std::vector<RhythmItem> rhythm_; // of the voice being grouped
};

double StandardWriter::header(std::size_t first) {
    dynamics_.start(staff_, first);
    lyrics_.start(first);
    octaveRuns_.clear();
    pedalRuns_.clear();
    return headerAt(first);
}

double StandardWriter::changes(std::size_t m, double x) {
    return changesAt(m, x);
}

void StandardWriter::collect(std::size_t m, std::vector<ColumnInk>& ink) {
    read(m);
    columnInk(ink);
    pedalInk(m, ink);
    aboveRow_.collect(staff_.measure(m), ink, widthOf(Glyph::NoteheadBlack) / 2);
    lyrics_.collect(m, ink, widthOf(Glyph::NoteheadBlack) / 2);
}

void StandardWriter::draw(std::span<const ColumnPosition> positions, double start, double barline) {
    drawMeasure(positions, start, barline);
    aboveRow_.draw(positions, widthOf(Glyph::NoteheadBlack) / 2, measureIndex_);
    lyrics_.draw(positions, widthOf(Glyph::NoteheadBlack) / 2);
    strikeDynamics();
    dynamics_.hairpins(staff_.measure(measureIndex_), measureIndex_, positions, barline);
    octaveLines(positions);
    pedalLines(positions, barline);
}

} // namespace

std::unique_ptr<StaffWriter> makeStandardWriter(const core::Song& song, std::size_t track,
                                                std::size_t staff, const EngravingFont& font,
                                                bool techniques) {
    const core::Track& part = song.track(track);
    return std::make_unique<StandardWriter>(song, part, part.staff(staff), staff > 0, font,
                                            techniques);
}

} // namespace elara::notation::detail
