// SPDX-License-Identifier: AGPL-3.0-or-later
// Tablature of a fretted staff: fret numbers on string lines and guitar
// techniques (ADR-0012).
#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation::detail {

namespace {

using core::Fraction;

constexpr double kLineSpace = 1.5;      // between string lines
constexpr double kFretSize = 1.3;       // font size of fret numbers
constexpr double kFretPadding = 0.12;   // around a fret number, in its column's ink
constexpr double kArcGap = 0.15;        // between a fret number and a hammer-on arc
constexpr double kArcHeight = 0.45;     // of hammer-on and pull-off arcs
constexpr double kArcInset = 0.25;      // arcs start this share of a number's width in
constexpr double kArcLabelSize = 0.85;  // "H" and "P"
constexpr double kArcLabelGap = 0.12;   // between an arc and its label
constexpr double kSlideGap = 0.15;      // between a slide line and its fret numbers
constexpr double kSlideRise = 0.3;      // half the height a slide line climbs
constexpr double kSlideOff = 1.0;       // slides into and out of a note
constexpr double kLineThickness = 0.12; // slides and bends
constexpr double kBendGap = 0.1;        // between a fret number and its bend
constexpr double kBendWidth = 0.9;      // horizontal run of a bend or release arrow
constexpr double kArrowLength = 0.5;
constexpr double kArrowWidth = 0.45;
constexpr double kBendLabelSize = 0.95;
constexpr double kBendLabelGap = 0.2;  // between an arrowhead and its label
constexpr double kReleaseDrop = 0.6;   // a release ends this far above its string
constexpr double kRunEndGap = 0.3;     // a technique run ends before the next column
constexpr double kRhythmGap = 0.8;     // between the last string and the rhythm stems
constexpr double kRhythmStem = 2.5;    // rhythm stems of quarters and shorter
constexpr double kRhythmDotGap = 0.3;  // between a rhythm stem and its dots
constexpr double kRhythmDotRise = 0.9; // dots this far above the end of their stem
constexpr double kDotSeparation = 0.25;
constexpr double kGraceGap = 0.4; // between grace fret numbers, and before their column

struct TabNote {
    const core::Note* note = nullptr;
    int string = 1;
    int fret = 0;
    std::string text;
    double size = kFretSize;
    double width = 0.0; // of the text
};

// The fret numbers of a grace note, small, left of their column (ADR-0079).
struct TabGrace {
    std::vector<TabNote> notes;
    double offset = 0.0; // of their centre, from the column's
};

struct TabColumn {
    Fraction start;
    Fraction longest;
    std::vector<TabNote> notes;
    std::vector<TabGrace> graces;         // in the order they are played
    std::vector<const core::Beat*> beats; // every beat starting here, rests included
    double x = 0.0;
};

// The last note drawn on a string, for hammer-ons, pull-offs and slides.
struct LastNote {
    double centre = 0.0;
    double halfWidth = 0.0;
    int fret = 0;
    core::SlideOut slideOut = core::SlideOut::None;
};

struct PendingBend {
    double centre = 0.0; // of the fret number
    double right = 0.0;  // where the arrow leaves the fret number
    double line = 0.0;   // y of the string
    double top = 0.0;    // top of the fret number
    bool pre = false;    // bent before the attack
    bool release = false;
    std::string label;
    std::size_t measure = 0;
};

// "12", "3.2": the fret a natural harmonic is touched at.
std::string harmonicFret(const core::Harmonic& harmonic, int fret) {
    if (harmonic.fretTenths <= 0) {
        return std::to_string(fret);
    }
    std::string text = std::to_string(harmonic.fretTenths / 10);
    if (harmonic.fretTenths % 10 != 0) {
        text += '.';
        text += std::to_string(harmonic.fretTenths % 10);
    }
    return text;
}

class TabWriter final : public StaffWriter {
public:
    TabWriter(const core::Song& song, const core::Staff& staff, const EngravingFont& font,
              bool alone)
        : StaffWriter(font, kLineSpace * (staff.tuning()->stringCount() - 1),
                      static_cast<std::size_t>(staff.tuning()->stringCount())),
          song_(song), staff_(staff), tuning_(*staff.tuning()), alone_(alone), lanes_(font),
          dynamics_(font) {}

    double header(std::size_t first) override {
        measure_ = first;
        dynamics_.start(staff_, first);
        const Glyph clef = lineCount() >= 5 ? Glyph::TabClef6 : Glyph::TabClef4;
        const BoundingBox& box = metrics(clef).box;
        double x = glyphFrom(clef, kClefX, height() / 2 + (box.southWest.y + box.northEast.y) / 2,
                             Role::Clef);
        if (first == 0 || song_.measureHeader(first - 1).timeSignature !=
                              song_.measureHeader(first).timeSignature) {
            x = timeSignature(song_.measureHeader(first).timeSignature, x + kHeaderGap,
                              height() / 2);
        }
        return x;
    }

    double changes(std::size_t m, double x) override {
        measure_ = m;
        if (song_.measureHeader(m).timeSignature != song_.measureHeader(m - 1).timeSignature) {
            x = timeSignature(song_.measureHeader(m).timeSignature, x + kHeaderGap, height() / 2);
        }
        return x;
    }

    void collect(std::size_t m, std::vector<ColumnInk>& ink) override {
        measure_ = m;
        measureIndex_ = m;
        columns_.clear();
        bool notes = false;
        for (const core::Voice& voice : staff_.measure(m).voices) {
            Fraction at;
            for (const core::Beat& beat : voice.beats) {
                TabColumn& column = columnAt(at);
                column.beats.push_back(&beat);
                column.longest = std::max(column.longest, beat.duration.length());
                for (const core::Note& note : beat.notes) {
                    if (std::optional<TabNote> tab = tabNote(note)) {
                        column.notes.push_back(std::move(*tab));
                        notes = true;
                    }
                }
                for (const core::Grace& grace : beat.graces) {
                    TabGrace small;
                    for (const core::Note& note : grace.notes) {
                        if (std::optional<TabNote> tab = tabNote(note, kFretSize * kGraceScale)) {
                            small.notes.push_back(std::move(*tab));
                        }
                    }
                    if (!small.notes.empty()) {
                        column.graces.push_back(std::move(small));
                    }
                }
                at += beat.duration.length();
            }
        }
        ink.clear();
        rhythm_.clear();
        if (!notes) {
            columns_.clear(); // a measure of rests: one centred rest, if any
            return;
        }
        if (alone_) {
            readRhythm(staff_.measure(m));
        }
        for (TabColumn& column : columns_) {
            const double centre = centreOf(column);
            ColumnInk item{.start = column.start, .longest = column.longest};
            const auto beat = std::ranges::find(rhythm_, column.start, &RhythmItem::start);
            if (beat != rhythm_.end() && !beat->rest && beat->duration.dots() > 0) {
                item.right = centre + kRhythmDotGap + dotsWidth(beat->duration.dots());
            }
            if (column.notes.empty()) {
                const double width = alone_ ? widthOf(restFor(*column.beats.front())) : 0.0;
                item.left = centre - width / 2;
                item.right = centre + width / 2;
                ink.push_back(item);
                continue;
            }
            item.left = centre;
            item.right = std::max(item.right, centre);
            for (const TabNote& note : column.notes) {
                const core::NoteEffects& effects = note.note->effects;
                const double half = note.width / 2 + kFretPadding;
                double left = centre - half;
                double right = centre + half;
                if (effects.slideIn != core::SlideIn::None) {
                    left -= kSlideOff + kSlideGap;
                }
                if (effects.slideOut == core::SlideOut::Downwards ||
                    effects.slideOut == core::SlideOut::Upwards) {
                    right += kSlideOff + kSlideGap;
                }
                if (!effects.bend.isEmpty()) {
                    const auto [pre, release] = bendShape(effects.bend);
                    right += kBendGap + kBendWidth * ((pre ? 0 : 1) + (release ? 1 : 0)) +
                             kArrowWidth / 2;
                }
                item.left = std::min(item.left, left);
                item.right = std::max(item.right, right);
            }
            double edge = item.left - kGraceGap;
            for (auto grace = column.graces.rbegin(); grace != column.graces.rend(); ++grace) {
                const double half = std::ranges::max(grace->notes, {}, &TabNote::width).width / 2 +
                                    kFretPadding * kGraceScale;
                grace->offset = edge - half - centre;
                edge = centre + grace->offset - half - kGraceGap;
            }
            item.left = edge + kGraceGap;
            ink.push_back(item);
        }
    }

    [[nodiscard]] double emptyWidth() const override {
        return alone_ ? widthOf(Glyph::RestWhole) : 0.0;
    }

    void draw(std::span<const ColumnPosition> positions, double start, double barline) override {
        measure_ = measureIndex_;
        if (alone_) {
            dynamics_.hairpins(staff_.measure(measureIndex_), measureIndex_, positions, barline);
        }
        if (columns_.empty()) {
            if (alone_) {
                glyphCentred(Glyph::RestWhole, (start + barline) / 2, height() / 2, Role::Rest);
            }
            lanes_.column({}, barline, barline, measure_);
            return;
        }
        for (TabColumn& column : columns_) {
            column.x = columnX(positions, column.start);
            const double centre = column.x + centreOf(column);
            if (column.notes.empty()) {
                if (alone_) {
                    glyphCentred(restFor(*column.beats.front()), centre, height() / 2, Role::Rest);
                }
            }
            ColumnTechniques marks;
            std::optional<core::Dynamic> loudest;
            for (const core::Beat* beat : column.beats) {
                addTechniques(marks, *beat);
                addStruck(loudest, *beat);
            }
            // Dynamics go under the standard notation when the system has it.
            if (alone_ && loudest) {
                dynamics_.strike(*loudest, centre, measure_);
            }
            for (const TabGrace& grace : column.graces) {
                for (const TabNote& note : grace.notes) {
                    drawNote(note, centre + grace.offset);
                }
            }
            for (const TabNote& note : column.notes) {
                drawNote(note, centre);
            }
            if (lanes_.wants(marks)) {
                lanes_.column(marks, centre,
                              nextColumnX(positions, column.start, barline) - kRunEndGap, measure_);
            }
        }
        if (!rhythm_.empty()) {
            drawRhythm();
        }
    }

private:
    // The rhythm shown under the staff: the first voice of the measure with
    // notes (ADR-0041).
    void readRhythm(const core::Measure& measure) {
        for (const core::Voice& voice : measure.voices) {
            if (std::ranges::none_of(voice.beats,
                                     [](const core::Beat& beat) { return !beat.isRest(); })) {
                continue;
            }
            Fraction at;
            for (const core::Beat& beat : voice.beats) {
                rhythm_.push_back(
                    RhythmItem{.start = at, .duration = beat.duration, .rest = beat.isRest()});
                at += beat.duration.length();
            }
            return;
        }
    }

    [[nodiscard]] double dotsWidth(int dots) const {
        return dots * widthOf(Glyph::AugmentationDot) + (dots - 1) * kDotSeparation;
    }

    // Stems down from under the staff, flags, beams, dots and tuplet numbers
    // of the rhythm voice, as standard notation would write them: no stem for
    // whole notes, half the stem for halves, horizontal beams at the stems'
    // end.
    void drawRhythm() {
        const double top = height() + kRhythmGap;
        const double end = top + kRhythmStem;
        const double stemWidth = defaults_.stemThickness;
        rhythmX_.clear();
        for (const RhythmItem& item : rhythm_) {
            const auto column =
                std::ranges::lower_bound(columns_, item.start, {}, &TabColumn::start);
            rhythmX_.push_back(column->x + centreOf(*column));
        }
        const std::vector<std::vector<std::size_t>> groups =
            beamGroups(rhythm_, song_.measureHeader(measureIndex_).timeSignature);
        std::vector<int> groupOf(rhythm_.size(), -1);
        for (std::size_t g = 0; g < groups.size(); ++g) {
            for (const std::size_t i : groups[g]) {
                groupOf[i] = static_cast<int>(g);
            }
        }
        for (std::size_t i = 0; i < rhythm_.size(); ++i) {
            const RhythmItem& item = rhythm_[i];
            const core::NoteValue value = item.duration.value();
            if (item.rest || value < core::NoteValue::Half) {
                continue;
            }
            const double x = rhythmX_[i];
            line({x, value == core::NoteValue::Half ? top + kRhythmStem / 2 : top}, {x, end},
                 stemWidth, Role::Stem);
            if (groupOf[i] < 0 && beamCount(value) > 0) {
                const Glyph flag = offsetGlyph(Glyph::Flag8thDown, 2 * (beamCount(value) - 1));
                const Point attach = metrics(flag).anchor(Anchor::StemDownSW).value_or(Point{});
                glyph(flag, x - stemWidth / 2 - attach.x, end + attach.y, Role::Flag);
            }
            double dotX = x + kRhythmDotGap;
            for (int d = 0; d < item.duration.dots(); ++d) {
                dotX = glyphFrom(Glyph::AugmentationDot, dotX, end - kRhythmDotRise, Role::Dot) +
                       kDotSeparation;
            }
        }
        for (const std::vector<std::size_t>& group : groups) {
            rhythmBeams(group, end);
        }
        for (const TupletSpan& span : tupletSpans(rhythm_)) {
            const int g = groupOf[span.first];
            bool beamed = g >= 0;
            for (std::size_t i = span.first; beamed && i <= span.last; ++i) {
                beamed = groupOf[i] == g;
            }
            if (beamed) {
                beamed = std::ranges::none_of(
                    groups[static_cast<std::size_t>(g)],
                    [this](std::size_t i) { return rhythm_[i].duration.tuplet().isNone(); });
            }
            const TupletNumber number = tupletNumber(span.tuplet.actual());
            const double middle = (rhythmX_[span.first] + rhythmX_[span.last]) / 2;
            if (beamed) {
                drawTupletNumber(number, middle, end + kTupletGap + number.top);
                continue;
            }
            const double half = std::max((rhythmX_[span.last] - rhythmX_[span.first]) / 2,
                                         number.width / 2 + kTupletNumberGap + kTupletHook);
            tupletBracket(number, middle - half, middle + half, end + bracketReach(number), false);
        }
    }

    // The beams of one group, the primary one at the end of the stems and the
    // others above it; a lone shorter note gets a short beam towards its group.
    void rhythmBeams(const std::vector<std::size_t>& group, double end) {
        const double thickness = defaults_.beamThickness;
        const double stemWidth = defaults_.stemThickness;
        const double step = thickness + defaults_.beamSpacing;
        int levels = 1;
        for (const std::size_t i : group) {
            levels = std::max(levels, beamCount(rhythm_[i].duration.value()));
        }
        const auto count = [&](std::size_t k) {
            return beamCount(rhythm_[group[k]].duration.value());
        };
        for (int level = 1; level <= levels; ++level) {
            const double y = end - thickness / 2 - (level - 1) * step;
            const auto add = [&](double left, double right) {
                list_.beams.push_back(BeamItem{.left = {left, y},
                                               .right = {right, y},
                                               .thickness = thickness,
                                               .measure = measure_});
            };
            std::size_t k = 0;
            while (k < group.size()) {
                if (count(k) < level) {
                    ++k;
                    continue;
                }
                std::size_t j = k;
                while (j + 1 < group.size() && count(j + 1) >= level) {
                    ++j;
                }
                const double leftStem = rhythmX_[group[k]];
                const double rightStem = rhythmX_[group[j]];
                if (j > k) {
                    add(leftStem - stemWidth / 2, rightStem + stemWidth / 2);
                } else if (k == 0) {
                    add(leftStem - stemWidth / 2, leftStem - stemWidth / 2 + kBeamletLength);
                } else {
                    add(rightStem + stemWidth / 2 - kBeamletLength, rightStem + stemWidth / 2);
                }
                k = j + 1;
            }
        }
    }

    // Where fret numbers go from the column's x: under the centre of the
    // notehead the standard notation draws for the first note.
    [[nodiscard]] double centreOf(const TabColumn& column) const {
        Glyph head = Glyph::NoteheadBlack;
        for (const core::Beat* beat : column.beats) {
            if (beat->isRest()) {
                continue;
            }
            switch (beat->duration.value()) {
            case core::NoteValue::DoubleWhole:
                head = Glyph::NoteheadDoubleWhole;
                break;
            case core::NoteValue::Whole:
                head = Glyph::NoteheadWhole;
                break;
            case core::NoteValue::Half:
                head = Glyph::NoteheadHalf;
                break;
            default:
                break;
            }
            break;
        }
        const BoundingBox& box = metrics(head).box;
        return (box.southWest.x + box.northEast.x) / 2;
    }

    [[nodiscard]] static Glyph restFor(const core::Beat& beat) {
        return offsetGlyph(Glyph::RestDoubleWhole, static_cast<int>(beat.duration.value()));
    }

    // A bend made before the attack, and one that comes back down.
    [[nodiscard]] static std::pair<bool, bool> bendShape(const core::BendCurve& bend) {
        const std::vector<core::BendPoint>& points = bend.points();
        const bool pre = points.front().position == 0 && points.front().cents > 0;
        const bool release = points.back().cents < bend.maxCents();
        return {pre, release};
    }

    TabColumn& columnAt(const Fraction& start) {
        const auto it = std::ranges::lower_bound(columns_, start, {}, &TabColumn::start);
        if (it != columns_.end() && it->start == start) {
            return *it;
        }
        TabColumn column;
        column.start = start;
        return *columns_.insert(it, std::move(column));
    }

    // Where the note is played (its position, or the lowest fret that sounds
    // it) and what the tablature writes for it.
    // The fret number of a note, its text `size` high.
    [[nodiscard]] std::optional<TabNote> tabNote(const core::Note& note,
                                                 double size = kFretSize) const {
        core::FretPosition position;
        if (note.position) {
            position = *note.position;
        } else {
            const std::vector<core::FretPosition> all =
                tuning_.positionsFor(note.pitch, staff_.capo());
            if (all.empty()) {
                return std::nullopt;
            }
            position = *std::ranges::min_element(all, {}, &core::FretPosition::fret);
        }
        if (position.string < 1 || position.string > static_cast<int>(lineCount())) {
            return std::nullopt;
        }
        const core::NoteEffects& effects = note.effects;
        TabNote tab;
        tab.note = &note;
        tab.string = position.string;
        tab.fret = position.fret;
        tab.text = effects.deadNote ? std::string("X") : std::to_string(position.fret);
        if (effects.harmonic.kind == core::HarmonicKind::Natural && !effects.deadNote) {
            tab.text = "<" + harmonicFret(effects.harmonic, position.fret) + ">";
        }
        if (effects.ghost || effects.tiedFromPrevious) {
            tab.text = "(" + tab.text + ")";
        }
        tab.size = size;
        tab.width = textWidth(tab.text, size);
        return tab;
    }

    void drawNote(const TabNote& note, double centre) {
        const double y = kLineSpace * (note.string - 1);
        const double half = note.width / 2;
        const double cap = capHeight(note.size);
        text(note.text, {centre, y + cap / 2}, note.size, Role::FretNumber, TextAlign::Centre,
             true);
        const core::NoteEffects& effects = note.note->effects;
        std::optional<LastNote>& previous = last_[static_cast<std::size_t>(note.string)];
        if (previous) {
            const bool slide = previous->slideOut == core::SlideOut::Shift ||
                               previous->slideOut == core::SlideOut::Legato;
            if (slide) {
                const double direction = note.fret > previous->fret   ? 1.0
                                         : note.fret < previous->fret ? -1.0
                                                                      : 0.0;
                line({previous->centre + previous->halfWidth + kSlideGap,
                      y + direction * kSlideRise},
                     {centre - half - kSlideGap, y - direction * kSlideRise}, kLineThickness,
                     Role::Slide);
            }
            if (effects.legatoFromPrevious || previous->slideOut == core::SlideOut::Legato) {
                const double arcY = y - cap / 2 - kArcGap;
                const double from = previous->centre + previous->halfWidth * kArcInset;
                const double to = centre - half * kArcInset;
                curve({from, arcY}, {to, arcY}, -kArcHeight, defaults_.slurMidpointThickness,
                      defaults_.slurEndpointThickness, Role::Slur);
                if (effects.legatoFromPrevious && !slide && note.fret != previous->fret) {
                    text(note.fret > previous->fret ? "H" : "P",
                         {(from + to) / 2, arcY - kArcHeight - kArcLabelGap}, kArcLabelSize,
                         Role::Technique, TextAlign::Centre, true);
                }
            }
        }
        if (effects.slideIn != core::SlideIn::None) {
            const double direction = effects.slideIn == core::SlideIn::FromBelow ? 1.0 : -1.0;
            const double end = centre - half - kSlideGap;
            line({end - kSlideOff, y + direction * kSlideRise}, {end, y - direction * kSlideRise},
                 kLineThickness, Role::Slide);
        }
        if (effects.slideOut == core::SlideOut::Downwards ||
            effects.slideOut == core::SlideOut::Upwards) {
            const double direction = effects.slideOut == core::SlideOut::Downwards ? 1.0 : -1.0;
            const double begin = centre + half + kSlideGap;
            line({begin, y - direction * kSlideRise},
                 {begin + kSlideOff, y + direction * kSlideRise}, kLineThickness, Role::Slide);
        }
        if (!effects.bend.isEmpty()) {
            const auto [pre, release] = bendShape(effects.bend);
            pendingBends_.push_back(PendingBend{.centre = centre,
                                                .right = centre + half + kBendGap,
                                                .line = y,
                                                .top = y - cap / 2,
                                                .pre = pre,
                                                .release = release,
                                                .label = bendLabel(effects.bend.maxCents()),
                                                .measure = measure_});
            lanes_.use(Lane::Bend);
        }
        previous = LastNote{
            .centre = centre, .halfWidth = half, .fret = note.fret, .slideOut = effects.slideOut};
    }

    void close(double width) override {
        if (!lanes_.empty()) {
            lanes_.place(list_, inkTop());
            const double tip = lanes_.bottom(Lane::Bend);
            for (const PendingBend& bend : pendingBends_) {
                measure_ = bend.measure;
                bendArrows(bend, tip);
            }
        }
        if (!dynamics_.empty()) {
            dynamics_.place(list_, inkBottom(), height(), width);
        }
    }

    // A bend: a curve from the fret number up to an arrowhead at the bottom of
    // the bend lane, labelled with its size; a prebend rises straight from the
    // number; a release curves back down to the string.
    void bendArrows(const PendingBend& bend, double tip) {
        const double base = tip + kArrowLength;
        double peak = bend.centre;
        if (bend.pre) {
            line({peak, bend.top - kBendGap}, {peak, base}, kLineThickness, Role::Bend);
        } else {
            peak = bend.right + kBendWidth;
            stroke({bend.right, bend.line}, {bend.right + kBendWidth * 0.6, bend.line},
                   {peak, bend.line - (bend.line - base) * 0.4}, {peak, base});
        }
        arrowhead(peak, tip, -1.0);
        if (!bend.label.empty()) {
            text(bend.label, {peak, tip - kBendLabelGap}, kBendLabelSize, Role::Bend);
        }
        if (bend.release) {
            const double end = peak + kBendWidth;
            const double bottom = bend.line - kReleaseDrop;
            stroke({peak, tip}, {peak + kBendWidth * 0.4, tip}, {end, tip + (bottom - tip) * 0.4},
                   {end, bottom - kArrowLength});
            arrowhead(end, bottom, 1.0);
        }
    }

    void stroke(Point from, Point control1, Point control2, Point to) {
        CurveItem item;
        item.from = from;
        item.control1 = control1;
        item.control2 = control2;
        item.to = to;
        item.thickness = kLineThickness;
        item.endThickness = kLineThickness;
        item.role = Role::Bend;
        item.measure = measure_;
        list_.curves.push_back(item);
    }

    // An arrowhead with its tip at (x, y), pointing up (-1) or down (+1).
    void arrowhead(double x, double y, double direction) {
        const double base = y - direction * kArrowLength;
        polygon({{x, y}, {x - kArrowWidth / 2, base}, {x + kArrowWidth / 2, base}}, Role::Bend);
    }

    const core::Song& song_;
    const core::Staff& staff_;
    const core::Tuning& tuning_;
    bool alone_; // no standard notation of the staff: rests and rhythm show here
    std::vector<RhythmItem> rhythm_; // the voice whose rhythm shows, in the measure read last
    std::vector<double> rhythmX_;    // the stems' x
    TechniqueLanes lanes_;
    DynamicMarks dynamics_;
    std::size_t measureIndex_ = 0;
    std::vector<TabColumn> columns_;
    std::array<std::optional<LastNote>, core::Tuning::kMaxStrings + 1> last_{};
    std::vector<PendingBend> pendingBends_;
};

} // namespace

std::unique_ptr<StaffWriter> makeTabWriter(const core::Song& song, std::size_t track,
                                           std::size_t staff, const EngravingFont& font,
                                           bool alone) {
    return std::make_unique<TabWriter>(song, song.track(track).staff(staff), font, alone);
}

} // namespace elara::notation::detail
