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
constexpr double kMaxBeamSlant = 1.0;
constexpr double kBeamletLength = 1.1;
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
constexpr double kArticulationGap = 0.3; // between a notehead and its articulation
constexpr double kRunEndGap = 0.3;       // a technique run ends before the next column

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

// Flags or beams the value needs (0 for a quarter or longer).
int beamCount(NoteValue value) {
    return std::max(0, valueIndex(value) - valueIndex(NoteValue::Quarter));
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
    double left = 0.0; // ink extents from the chord's x
    double right = 0.0;
    double x = 0.0;
    double stemX = 0.0; // centre of the stem
    double stemStart = 0.0;
};

struct Column {
    Fraction start;
    std::vector<std::size_t> chords;
    double left = 0.0;
    double right = 0.0;
    double x = 0.0;
};

class StandardWriter final : public StaffWriter {
public:
    StandardWriter(const core::Song& song, const core::Track& track, const core::Staff& staff,
                   const EngravingFont& font, bool techniques)
        : StaffWriter(font, kStaffHeight, 5), song_(song), staff_(staff), clef_(staff.clef()),
          percussion_(staff.clef() == Clef::Percussion ||
                      track.instrument().kind == core::InstrumentKind::Percussion),
          offset_(percussion_ ? 0 : track.instrument().writtenOffset), techniques_(techniques),
          lanes_(font) {}

    double header(std::size_t first) override;
    double changes(std::size_t m, double x) override;
    void collect(std::size_t m, std::vector<ColumnInk>& ink) override;
    [[nodiscard]] double emptyWidth() const override { return widthOf(Glyph::RestWhole); }
    void draw(std::span<const ColumnPosition> positions, double start, double barline) override;

private:
    void close(double width) override {
        tiesOut(width - defaults_.thinBarlineThickness - kTieGap);
        if (techniques_ && !lanes_.empty()) {
            lanes_.place(list_, inkTop());
        }
    }

    // ---- Signatures ----

    [[nodiscard]] KeySignature keyAt(std::size_t m) const {
        return writtenKey(song_.measureHeader(m).keySignature, offset_);
    }

    double headerAt(std::size_t first) {
        measure_ = first;
        Glyph clefGlyph = Glyph::GClef;
        double clefY = 3.0;
        switch (clef_) {
        case Clef::Treble:
            break;
        case Clef::Treble8vb:
            clefGlyph = Glyph::GClef8vb;
            break;
        case Clef::Bass:
            clefGlyph = Glyph::FClef;
            clefY = 1.0;
            break;
        case Clef::Bass8vb:
            clefGlyph = Glyph::FClef8vb;
            clefY = 1.0;
            break;
        case Clef::Alto:
            clefGlyph = Glyph::CClef;
            clefY = 2.0;
            break;
        case Clef::Tenor:
            clefGlyph = Glyph::CClef;
            clefY = 1.0;
            break;
        case Clef::Percussion:
            clefGlyph = Glyph::PercussionClef;
            clefY = 2.0;
            break;
        }
        double x = glyphFrom(clefGlyph, kClefX, clefY, Role::Clef);
        x = keySignature(keyAt(first), x);
        if (first == 0 || song_.measureHeader(first - 1).timeSignature !=
                              song_.measureHeader(first).timeSignature) {
            x = timeSignature(song_.measureHeader(first).timeSignature, x + kHeaderGap,
                              kMiddleLine);
        }
        // Ties from the line before start after the header.
        lineStart_ = x;
        incoming_.fill(first > 0);
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
                continue;
            }
            draw(chord);
            ties(chord);
            articulations(chord);
        }
        if (voiceCount_ == 0) {
            for (auto& starts : tieStarts_) {
                starts.clear();
            }
            incoming_.fill(false);
        }
        for (const std::vector<std::size_t>& group : beamGroups_) {
            beam(group);
        }
        centreMeasureRests(start, barline);
        if (techniques_) {
            markTechniques(positions, barline);
        }
    }

    void centreMeasureRests(double start, double barline) {
        for (const Chord& chord : chords_) {
            if (chord.measureRest) {
                const BoundingBox& box = metrics(Glyph::RestWhole).box;
                const double middle = (start + barline) / 2;
                glyph(Glyph::RestWhole, middle - (box.southWest.x + box.northEast.x) / 2,
                      restY(chord, Glyph::RestWhole), Role::Rest);
            }
        }
    }

    // Reads measure m into chords_, columns_ and beamGroups_.
    void read(std::size_t m) {
        measure_ = m;
        measureIndex_ = m;
        chords_.clear();
        columns_.clear();
        beamGroups_.clear();
        const core::Measure& measure = staff_.measure(m);
        const TimeSignature time = song_.measureHeader(m).timeSignature;
        const Fraction length = time.measureLength();
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
                    chord.heads.push_back(headOf(note, beat.duration.value()));
                }
                std::ranges::sort(chord.heads, {}, &Head::step);
                at += chord.length;
                chords_.push_back(std::move(chord));
            }
            groupBeams(voiceStart, time);
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
    }

    [[nodiscard]] Head headOf(const core::Note& note, NoteValue value) const {
        Head head;
        head.note = &note;
        head.tied = note.effects.tiedFromPrevious;
        if (percussion_) {
            const detail::DrumNote drum = detail::drumNote(note.pitch.midiNumber());
            head.step = drum.step;
            head.foot = drum.foot;
            head.glyph = noteheadFor(value, drum.head);
            return head;
        }
        const Pitch written = writtenPitch(note.pitch, offset_);
        head.diatonic = diatonicIndex(written);
        head.step = topLineIndex(clef_) - head.diatonic;
        head.alter = written.alter();
        head.glyph = noteheadFor(value, note.effects.deadNote ? detail::DrumHead::X
                                                              : detail::DrumHead::Normal);
        return head;
    }

    // Beams consecutive eighths and shorter of one voice inside one beat of the
    // meter (a dotted beat in compound meters).
    void groupBeams(std::size_t from, const TimeSignature& time) {
        const bool compound = time.isCompound() && time.denominator() >= 8;
        const Fraction window(compound ? 3 : 1, time.denominator());
        std::vector<std::size_t> group;
        std::int64_t groupWindow = -1;
        const auto flush = [&] {
            if (group.size() >= 2) {
                for (const std::size_t c : group) {
                    chords_[c].beamGroup = static_cast<int>(beamGroups_.size());
                }
                beamGroups_.push_back(group);
            }
            group.clear();
        };
        for (std::size_t c = from; c < chords_.size(); ++c) {
            const Chord& chord = chords_[c];
            const Fraction position = chord.start / window;
            const std::int64_t index = position.numerator() / position.denominator();
            const bool fits = chord.start + chord.length <= window * Fraction(index + 1);
            if (chord.rest || beamCount(chord.duration.value()) == 0 || !fits) {
                flush();
                continue;
            }
            if (index != groupWindow) {
                flush();
                groupWindow = index;
            }
            group.push_back(c);
        }
        flush();
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
        for (const std::size_t c : order) {
            for (Head& head : chords_[c].heads) {
                if (head.tied || head.diatonic < 0 ||
                    head.diatonic >= static_cast<int>(state.size())) {
                    continue;
                }
                std::int8_t& current = state[static_cast<std::size_t>(head.diatonic)];
                const int expected = current == kUnset
                                         ? key.alterFor(static_cast<Step>(head.diatonic % 7))
                                         : current;
                if (head.alter != expected) {
                    head.accidental = accidentalFor(head.alter);
                }
                current = static_cast<std::int8_t>(head.alter);
            }
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
        chord.left = chord.headLeft - placeAccidentals(chord);
        chord.right = chord.headRight + dotsWidth(chord.duration.dots());
        if (stem && chord.beamGroup < 0 && beamCount(chord.duration.value()) > 0) {
            const Glyph flag = flagFor(chord.duration.value(), chord.stemUp);
            const Head& base = chord.stemUp ? heads.back() : heads.front();
            const double stemLeft =
                stemAnchor(base, chord.stemUp).x - (chord.stemUp ? stemWidth : 0.0);
            chord.right = std::max(chord.right, stemLeft + metrics(flag).box.northEast.x);
        }
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
        double edge = chord.headLeft - kAccidentalGap;
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
        return chord.headLeft - (edge + kAccidentalColumnGap);
    }

    void draw(Chord& chord) {
        const double x = chord.x;
        if (chord.rest) {
            const Glyph rest = restFor(chord.duration.value());
            const double y = restY(chord, rest);
            glyph(rest, x - metrics(rest).box.southWest.x, y, Role::Rest);
            dots(x + chord.headRight, {y - 0.5}, chord.duration.dots());
            return;
        }
        std::vector<double> dotYs;
        for (const Head& head : chord.heads) {
            glyph(head.glyph, x + head.dx, yOf(head.step), Role::Notehead);
            if (head.accidental) {
                glyph(*head.accidental, x + head.accidentalX, yOf(head.step), Role::Accidental);
            }
            const bool onLine = head.step % 2 == 0;
            const bool lower = voiceCount_ > 1 && !voiceUp_[chord.voice];
            const double dotY = yOf(head.step) + (onLine ? (lower ? 0.5 : -0.5) : 0.0);
            if (std::ranges::find(dotYs, dotY) == dotYs.end()) {
                dotYs.push_back(dotY);
            }
        }
        dots(x + chord.headRight, dotYs, chord.duration.dots());
        ledgerLines(chord);
        if (!hasStem(chord.duration.value())) {
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
        const double length = stemLengthFor(chord.duration.value());
        const double tip = chord.stemUp
                               ? std::min(yOf(chord.heads.front().step) - length, kMiddleLine)
                               : std::max(yOf(chord.heads.back().step) + length, kMiddleLine);
        line({chord.stemX, chord.stemStart}, {chord.stemX, tip}, stemWidth, Role::Stem);
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

    void ledgerLines(const Chord& chord) {
        const double extension = defaults_.legerLineExtension;
        const auto ledger = [&](int step, bool above) {
            double from = std::numeric_limits<double>::max();
            double to = std::numeric_limits<double>::lowest();
            for (const Head& head : chord.heads) {
                if (above ? head.step <= step : head.step >= step) {
                    const BoundingBox& box = metrics(head.glyph).box;
                    from = std::min(from, chord.x + head.dx + box.southWest.x - extension);
                    to = std::max(to, chord.x + head.dx + box.northEast.x + extension);
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

    // Stems and beams of a beam group: the beam follows the melody, slanted by
    // half the interval between its outer notes (one staff space at most, flat
    // when an inner note reaches beyond both), and lies far enough from every
    // notehead.
    void beam(const std::vector<std::size_t>& group) {
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
        // The outer edge of the primary beam: y = edge + slope * (x - first.stemX).
        double edge =
            up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
        for (const std::size_t c : group) {
            const Chord& chord = chords_[c];
            const double needed =
                end(chord) + (up ? -minimum : minimum) - slope * (chord.stemX - first.stemX);
            edge = up ? std::min(edge, needed) : std::max(edge, needed);
        }
        // Notes far outside the staff: the beam still reaches the middle line.
        const double nearest = up ? std::max(edge, edge + slant) : std::min(edge, edge + slant);
        if (up && nearest > kMiddleLine) {
            edge -= nearest - kMiddleLine;
        } else if (!up && nearest < kMiddleLine) {
            edge += kMiddleLine - nearest;
        }
        const auto edgeAt = [&](double x) {
            return edge + slope * (x - first.stemX);
        };
        for (const std::size_t c : group) {
            const Chord& chord = chords_[c];
            line({chord.stemX, chord.stemStart}, {chord.stemX, edgeAt(chord.stemX)}, stemWidth,
                 Role::Stem);
        }
        const double step = thickness + defaults_.beamSpacing;
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
            const double right = chord.x + head.dx + metrics(head.glyph).box.southWest.x;
            tie(head.step, tieAbove(chord, i), left + kTieGap, right - kTieGap);
        }
        previous.clear();
        if (chord.rest) {
            return;
        }
        const double dots = dotsWidth(chord.duration.dots());
        for (std::size_t i = 0; i < count; ++i) {
            const Head& head = chord.heads[i];
            previous.push_back(
                TieStart{.step = head.step,
                         .right = chord.x + head.dx + metrics(head.glyph).box.northEast.x + dots,
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
                const int step = headOf(note, beats.front().duration.value()).step;
                const auto from = std::ranges::find(starts, step, &TieStart::step);
                if (from != starts.end()) {
                    tie(step, from->above, from->right + kTieGap, end);
                }
            }
        }
    }

    // Staccato and accents on the notehead side of the chord, the staccato
    // dot in a space when inside the staff.
    void articulations(const Chord& chord) {
        if (chord.rest || chord.heads.empty()) {
            return;
        }
        bool staccato = false;
        core::Accent accent = core::Accent::None;
        for (const Head& head : chord.heads) {
            staccato = staccato || head.note->effects.staccato;
            accent = std::max(accent, head.note->effects.accent);
        }
        if (!staccato && accent == core::Accent::None) {
            return;
        }
        const bool above = !chord.stemUp;
        const double direction = above ? -1.0 : 1.0;
        const Head& outer = above ? chord.heads.front() : chord.heads.back();
        const BoundingBox& head = metrics(outer.glyph).box;
        const double centre = chord.x + outer.dx + (head.southWest.x + head.northEast.x) / 2;
        double edge = yOf(outer.step) + direction * (0.5 + kArticulationGap);
        if (staccato) {
            int target = outer.step + (above ? -2 : 2);
            if (target % 2 == 0 && target >= 0 && target <= 2 * kMiddleStep) {
                target += above ? -1 : 1;
            }
            const Glyph dot = above ? Glyph::ArticStaccatoAbove : Glyph::ArticStaccatoBelow;
            glyphCentred(dot, centre, yOf(target), Role::Articulation);
            edge = yOf(target) + direction * (metrics(dot).box.height() / 2 + kArticulationGap);
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
            if (lanes_.wants(marks)) {
                const double end = nextColumnX(positions, column.start, barline) - kRunEndGap;
                lanes_.column(marks, column.x + centre, end, measure_);
            }
        }
    }

    struct TieStart {
        int step = 0;
        double right = 0.0; // after the notehead and its dots
        bool above = false; // the side the tie leaves on
    };

    const core::Song& song_;
    const core::Staff& staff_;
    Clef clef_;
    bool percussion_;
    int offset_;
    bool techniques_;
    TechniqueLanes lanes_;
    std::size_t measureIndex_ = 0;
    std::size_t voiceCount_ = 0;
    std::array<std::vector<TieStart>, core::Measure::kMaxVoices> tieStarts_{};
    std::array<bool, core::Measure::kMaxVoices> incoming_{}; // ties from the line before
    double lineStart_ = 0.0;                                 // where the header ends
    std::array<bool, core::Measure::kMaxVoices> voiceUp_{};
    std::vector<Chord> chords_;
    std::vector<Column> columns_;
    std::vector<std::vector<std::size_t>> beamGroups_;
};

double StandardWriter::header(std::size_t first) {
    return headerAt(first);
}

double StandardWriter::changes(std::size_t m, double x) {
    return changesAt(m, x);
}

void StandardWriter::collect(std::size_t m, std::vector<ColumnInk>& ink) {
    read(m);
    columnInk(ink);
}

void StandardWriter::draw(std::span<const ColumnPosition> positions, double start, double barline) {
    drawMeasure(positions, start, barline);
}

} // namespace

std::unique_ptr<StaffWriter> makeStandardWriter(const core::Song& song, std::size_t track,
                                                std::size_t staff, const EngravingFont& font,
                                                bool techniques) {
    const core::Track& part = song.track(track);
    return std::make_unique<StandardWriter>(song, part, part.staff(staff), font, techniques);
}

} // namespace elara::notation::detail
