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
constexpr double kBendLabelGap = 0.2; // between an arrowhead and its label
constexpr double kReleaseDrop = 0.6;  // a release ends this far above its string
constexpr double kRunEndGap = 0.3;    // a technique run ends before the next column

struct TabNote {
    const core::Note* note = nullptr;
    int string = 1;
    int fret = 0;
    std::string text;
    double width = 0.0; // of the text
};

struct TabColumn {
    Fraction start;
    Fraction longest;
    std::vector<TabNote> notes;
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
              bool rests)
        : StaffWriter(font, kLineSpace * (staff.tuning()->stringCount() - 1),
                      static_cast<std::size_t>(staff.tuning()->stringCount())),
          song_(song), staff_(staff), tuning_(*staff.tuning()), rests_(rests), lanes_(font) {}

    double header(std::size_t first) override {
        measure_ = first;
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
                at += beat.duration.length();
            }
        }
        ink.clear();
        if (!notes) {
            columns_.clear(); // a measure of rests: one centred rest, if any
            return;
        }
        for (const TabColumn& column : columns_) {
            const double centre = centreOf(column);
            ColumnInk item{.start = column.start, .longest = column.longest};
            if (column.notes.empty()) {
                const double width = rests_ ? widthOf(restFor(*column.beats.front())) : 0.0;
                item.left = centre - width / 2;
                item.right = centre + width / 2;
                ink.push_back(item);
                continue;
            }
            item.left = centre;
            item.right = centre;
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
            ink.push_back(item);
        }
    }

    [[nodiscard]] double emptyWidth() const override {
        return rests_ ? widthOf(Glyph::RestWhole) : 0.0;
    }

    void draw(std::span<const ColumnPosition> positions, double start, double barline) override {
        measure_ = measureIndex_;
        if (columns_.empty()) {
            if (rests_) {
                glyphCentred(Glyph::RestWhole, (start + barline) / 2, height() / 2, Role::Rest);
            }
            lanes_.column({}, barline, barline, measure_);
            return;
        }
        for (TabColumn& column : columns_) {
            column.x = columnX(positions, column.start);
            const double centre = column.x + centreOf(column);
            if (column.notes.empty()) {
                if (rests_) {
                    glyphCentred(restFor(*column.beats.front()), centre, height() / 2, Role::Rest);
                }
            }
            ColumnTechniques marks;
            for (const core::Beat* beat : column.beats) {
                addTechniques(marks, *beat);
            }
            for (const TabNote& note : column.notes) {
                drawNote(note, centre);
            }
            if (lanes_.wants(marks)) {
                lanes_.column(marks, centre,
                              nextColumnX(positions, column.start, barline) - kRunEndGap, measure_);
            }
        }
    }

private:
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
    [[nodiscard]] std::optional<TabNote> tabNote(const core::Note& note) const {
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
        tab.width = textWidth(tab.text, kFretSize);
        return tab;
    }

    void drawNote(const TabNote& note, double centre) {
        const double y = kLineSpace * (note.string - 1);
        const double half = note.width / 2;
        const double cap = capHeight(kFretSize);
        text(note.text, {centre, y + cap / 2}, kFretSize, Role::FretNumber, TextAlign::Centre,
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

    void close() override {
        if (lanes_.empty()) {
            return;
        }
        lanes_.place(list_, inkTop());
        const double tip = lanes_.bottom(Lane::Bend);
        for (const PendingBend& bend : pendingBends_) {
            measure_ = bend.measure;
            bendArrows(bend, tip);
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
    bool rests_;
    TechniqueLanes lanes_;
    std::size_t measureIndex_ = 0;
    std::vector<TabColumn> columns_;
    std::array<std::optional<LastNote>, core::Tuning::kMaxStrings + 1> last_{};
    std::vector<PendingBend> pendingBends_;
};

} // namespace

std::unique_ptr<StaffWriter> makeTabWriter(const core::Song& song, std::size_t track,
                                           std::size_t staff, const EngravingFont& font,
                                           bool rests) {
    return std::make_unique<TabWriter>(song, song.track(track).staff(staff), font, rests);
}

} // namespace elara::notation::detail
