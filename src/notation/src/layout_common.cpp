// SPDX-License-Identifier: AGPL-3.0-or-later
#include "layout_common.hpp"

#include <elara/core/song.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation::detail {

namespace {

// Technique lanes, in staff spaces.
constexpr double kLaneGap = 0.6;        // between the staff's ink and the first lane
constexpr double kLaneTextSize = 0.95;  // "P.M.", "let ring", harmonic labels
constexpr double kLaneBaseline = 0.2;   // text baseline above the bottom of its lane
constexpr double kLaneGlyphScale = 0.7; // strum arrows and tremolo slashes in their lanes
constexpr double kRunDash = 0.3;        // dashes of the line after "P.M."
constexpr double kRunHook = 0.4;        // the line's closing hook
constexpr double kRunLineGap = 0.25;    // between the text and its line
constexpr double kVibratoLift = 0.25;   // vibrato strokes above the bottom of their lane
constexpr double kLaneHeight[kLaneCount]{1.25, 2.3, 1.2, 1.15, 1.15, 1.05, 1.05, 2.3, 1.9, 1.9};
// The whammy-bar lane (ADR-0061): its label on top, the curve under it.
constexpr double kWhammyLow = 0.25;      // the lowest pitch of the curve above the lane's bottom
constexpr double kWhammyHigh = 1.25;     // its highest
constexpr double kWhammyBaseline = 1.5;  // the label's baseline
constexpr double kWhammyTextSize = 0.85; // "w/bar -1 1/2"
constexpr double kWhammyMinWidth = 1.0;  // a curve is at least this wide
constexpr double kWhammyEndGap = 0.3;    // between a curve and the next column

// Dynamics marks, in staff spaces (ADR-0044).
constexpr double kDynamicBaseline = 2.8;  // the baseline at least this far under the staff
constexpr double kDynamicInkGap = 0.5;    // at least between the ink above and the marks
constexpr double kDynamicGap = 0.4;       // at least between neighbouring marks
constexpr double kHairpinOpening = 1.0;   // between the lines at the open end of a wedge
constexpr double kHairpinContinued = 0.5; // where a wedge is cut by the end of a line
constexpr double kHairpinEndGap = 0.4;    // before the column or barline a wedge ends at
constexpr double kHairpinMinLength = 1.0;

// Chord symbols, in staff spaces (ADR-0080).
constexpr double kChordNameSize = 1.5;       // font size of a name
constexpr double kChordInkGap = 0.6;         // between the ink below and the row
constexpr double kChordNameGap = 0.3;        // between a name and its diagram
constexpr double kChordStringSpace = 0.6;    // between the strings of a diagram
constexpr double kChordFretSpace = 0.7;      // between its frets
constexpr int kChordMinFrets = 4;            // a diagram shows at least this many frets
constexpr double kChordMarkScale = 0.75;     // of the dots and the open and muted marks
constexpr double kChordMarkGap = 0.15;       // between the open and muted marks and the nut
constexpr double kChordLineThickness = 0.1;  // strings and frets
constexpr double kChordNutThickness = 0.25;  // the nut, at the first fret
constexpr double kChordFretNumberSize = 0.9; // font size of the first fret's number
constexpr double kChordFretNumberGap = 0.25; // between that number and the strings

// The frets a diagram shows: from its first fret to its highest, at least
// kChordMinFrets.
int fretsShown(const core::ChordDiagram& diagram) {
    int highest = diagram.firstFret;
    for (const std::int8_t fret : diagram.frets) {
        highest = std::max<int>(highest, fret);
    }
    return std::max(kChordMinFrets, highest - diagram.firstFret + 1);
}

// The width of the strings of a diagram, from the lowest to the highest.
double stringsWidth(const core::ChordDiagram& diagram) {
    return static_cast<double>(diagram.frets.size() - 1) * kChordStringSpace;
}

} // namespace

double idealSpace(const core::Fraction& length) {
    return kQuarterSpace * std::sqrt(std::max(length.toDouble(), 1.0 / 128.0) * 4.0);
}

double columnX(std::span<const ColumnPosition> positions, const core::Fraction& start) {
    const auto it = std::ranges::lower_bound(positions, start, {}, &ColumnPosition::start);
    return it == positions.end() ? 0.0 : it->x;
}

double nextColumnX(std::span<const ColumnPosition> positions, const core::Fraction& start,
                   double barline) {
    const auto it = std::ranges::upper_bound(positions, start, {}, &ColumnPosition::start);
    return it == positions.end() ? barline : it->x;
}

Extents inkExtents(const DisplayList& list, const EngravingFont& font, double staffHeight) {
    const double half = font.defaults().staffLineThickness / 2;
    double top = -half;
    double bottom = staffHeight + half;
    const auto cover = [&](double y) {
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    };
    for (const GlyphItem& item : list.glyphs) {
        const BoundingBox& box = font.metrics(item.glyph).box;
        cover(item.origin.y - box.northEast.y * item.scale);
        cover(item.origin.y - box.southWest.y * item.scale);
    }
    for (const LineItem& item : list.lines) {
        cover(item.from.y);
        cover(item.to.y);
    }
    for (const BeamItem& item : list.beams) {
        cover(item.left.y - item.thickness / 2);
        cover(item.right.y - item.thickness / 2);
        cover(item.left.y + item.thickness / 2);
        cover(item.right.y + item.thickness / 2);
    }
    for (const TextItem& item : list.texts) {
        cover(item.origin.y - kTextAscent * item.size);
        cover(item.origin.y + kTextDescent * item.size);
    }
    for (const CurveItem& item : list.curves) {
        // A Bézier curve stays inside the hull of its control points.
        for (const Point& point : {item.from, item.control1, item.control2, item.to}) {
            cover(point.y);
        }
    }
    for (const PolygonItem& item : list.polygons) {
        for (const Point& point : item.points) {
            cover(point.y);
        }
    }
    return {.top = top, .bottom = bottom};
}

void measureExtents(DisplayList& list, const EngravingFont& font, double staffHeight) {
    const Extents extents = inkExtents(list, font, staffHeight);
    list.top = extents.top;
    list.bottom = extents.bottom;
}

void shift(DisplayList& list, double dx, double dy) {
    if (dx == 0.0 && dy == 0.0) {
        return;
    }
    const auto move = [dx, dy](Point& point) {
        point.x += dx;
        point.y += dy;
    };
    for (GlyphItem& item : list.glyphs) {
        move(item.origin);
    }
    for (LineItem& item : list.lines) {
        move(item.from);
        move(item.to);
    }
    for (BeamItem& item : list.beams) {
        move(item.left);
        move(item.right);
    }
    for (TextItem& item : list.texts) {
        move(item.origin);
    }
    for (CurveItem& item : list.curves) {
        move(item.from);
        move(item.control1);
        move(item.control2);
        move(item.to);
    }
    for (PolygonItem& item : list.polygons) {
        for (Point& point : item.points) {
            move(point);
        }
    }
    for (SystemBox& system : list.systems) {
        system.x += dx;
        system.y += dy;
        system.top += dy;
        system.bottom += dy;
    }
    for (StaffBox& staff : list.staves) {
        staff.x += dx;
        staff.y += dy;
    }
    for (MeasureBox& measure : list.measures) {
        measure.x += dx;
        measure.y += dy;
        for (MeasureOnset& onset : measure.onsets) {
            onset.x += dx;
        }
    }
    for (ColumnBox& column : list.columns) {
        column.x += dx;
    }
    list.top += dy;
    list.bottom += dy;
}

void append(DisplayList& to, DisplayList&& from) {
    const auto move = [](auto& into, auto& items) {
        if (into.empty()) {
            into = std::move(items); // the first staff: no copy
            return;
        }
        into.insert(into.end(), std::make_move_iterator(items.begin()),
                    std::make_move_iterator(items.end()));
    };
    move(to.systems, from.systems);
    move(to.staves, from.staves);
    move(to.measures, from.measures);
    move(to.columns, from.columns);
    move(to.glyphs, from.glyphs);
    move(to.lines, from.lines);
    move(to.beams, from.beams);
    move(to.texts, from.texts);
    move(to.curves, from.curves);
    move(to.polygons, from.polygons);
}

void appendShifted(DisplayList& to, const DisplayList& from, double dx, double dy) {
    // Like shift(), nothing is added when nothing moves.
    const bool still = dx == 0.0 && dy == 0.0;
    const auto copy = [still](auto& into, const auto& items, auto&& move) {
        if (still) {
            into.insert(into.end(), items.begin(), items.end());
            return;
        }
        for (const auto& item : items) {
            move(into.emplace_back(item));
        }
    };
    const auto point = [dx, dy](Point& at) {
        at.x += dx;
        at.y += dy;
    };
    copy(to.systems, from.systems, [dx, dy](SystemBox& system) {
        system.x += dx;
        system.y += dy;
        system.top += dy;
        system.bottom += dy;
    });
    copy(to.staves, from.staves, [dx, dy](StaffBox& staff) {
        staff.x += dx;
        staff.y += dy;
    });
    copy(to.measures, from.measures, [dx, dy](MeasureBox& measure) {
        measure.x += dx;
        measure.y += dy;
        for (MeasureOnset& onset : measure.onsets) {
            onset.x += dx;
        }
    });
    copy(to.columns, from.columns, [dx](ColumnBox& column) { column.x += dx; });
    copy(to.glyphs, from.glyphs, [&point](GlyphItem& item) { point(item.origin); });
    copy(to.lines, from.lines, [&point](LineItem& item) {
        point(item.from);
        point(item.to);
    });
    copy(to.beams, from.beams, [&point](BeamItem& item) {
        point(item.left);
        point(item.right);
    });
    copy(to.texts, from.texts, [&point](TextItem& item) { point(item.origin); });
    copy(to.curves, from.curves, [&point](CurveItem& item) {
        point(item.from);
        point(item.control1);
        point(item.control2);
        point(item.to);
    });
    copy(to.polygons, from.polygons, [&point](PolygonItem& item) {
        for (Point& at : item.points) {
            point(at);
        }
    });
}

void reserveFor(DisplayList& to, std::span<const DisplayList* const> from) {
    const auto room = [&](auto member) {
        std::size_t size = (to.*member).size();
        for (const DisplayList* list : from) {
            size += (list->*member).size();
        }
        (to.*member).reserve(size);
    };
    room(&DisplayList::systems);
    room(&DisplayList::staves);
    room(&DisplayList::measures);
    room(&DisplayList::columns);
    room(&DisplayList::glyphs);
    room(&DisplayList::lines);
    room(&DisplayList::beams);
    room(&DisplayList::texts);
    room(&DisplayList::curves);
    room(&DisplayList::polygons);
}

Glyph dynamicGlyph(core::Dynamic dynamic) noexcept {
    switch (dynamic) {
    case core::Dynamic::PPP:
        return Glyph::DynamicPPP;
    case core::Dynamic::PP:
        return Glyph::DynamicPP;
    case core::Dynamic::P:
        return Glyph::DynamicPiano;
    case core::Dynamic::MP:
        return Glyph::DynamicMP;
    case core::Dynamic::MF:
        return Glyph::DynamicMF;
    case core::Dynamic::F:
        return Glyph::DynamicForte;
    case core::Dynamic::FF:
        return Glyph::DynamicFF;
    case core::Dynamic::FFF:
        return Glyph::DynamicFFF;
    }
    return Glyph::DynamicMF;
}

void addStruck(std::optional<core::Dynamic>& loudest, const core::Beat& beat) {
    for (const core::Note& note : beat.notes) {
        if (!note.effects.tiedFromPrevious && (!loudest || note.effects.dynamic > *loudest)) {
            loudest = note.effects.dynamic;
        }
    }
}

core::Dynamic dynamicBefore(const core::Staff& staff, std::size_t m) {
    while (m > 0) {
        --m;
        // The latest start in the measure with struck notes, over every voice.
        std::optional<core::Fraction> latest;
        std::optional<core::Dynamic> loudest;
        for (const core::Voice& voice : staff.measure(m).voices) {
            core::Fraction at;
            for (const core::Beat& beat : voice.beats) {
                std::optional<core::Dynamic> struck;
                addStruck(struck, beat);
                if (struck) {
                    if (!latest || *latest < at) {
                        latest = at;
                        loudest = struck;
                    } else if (*latest == at && *struck > *loudest) {
                        loudest = struck;
                    }
                }
                at += beat.duration.length();
            }
        }
        if (loudest) {
            return *loudest;
        }
    }
    return core::Dynamic::MF;
}

void DynamicMarks::strike(core::Dynamic dynamic, double centre, std::size_t measure) {
    if (!known_) {
        current_ = staff_ != nullptr ? dynamicBefore(*staff_, first_) : core::Dynamic::MF;
        known_ = true;
    }
    if (dynamic != current_) {
        marks_.push_back(Mark{.dynamic = dynamic, .centre = centre, .measure = measure});
        current_ = dynamic;
    }
}

namespace {

// Whether a voice of `measure` starts (ends) with a beat under `hairpin`.
bool startsWith(const core::Measure& measure, core::Hairpin hairpin) {
    return std::ranges::any_of(measure.voices, [hairpin](const core::Voice& voice) {
        return !voice.beats.empty() && voice.beats.front().effects.hairpin == hairpin;
    });
}

bool endsWith(const core::Measure& measure, core::Hairpin hairpin) {
    return std::ranges::any_of(measure.voices, [hairpin](const core::Voice& voice) {
        return !voice.beats.empty() && voice.beats.back().effects.hairpin == hairpin;
    });
}

} // namespace

void DynamicMarks::hairpins(const core::Measure& measure, std::size_t m,
                            std::span<const ColumnPosition> positions, double barline) {
    spans_.clear();
    for (const core::Voice& voice : measure.voices) {
        core::Fraction at;
        for (const core::Beat& beat : voice.beats) {
            const core::Fraction end = at + beat.duration.length();
            if (beat.effects.hairpin != core::Hairpin::None) {
                spans_.push_back({.start = at, .end = end, .hairpin = beat.effects.hairpin});
            }
            at = end;
        }
    }
    if (spans_.empty()) {
        return;
    }
    // One wedge per run of the same hairpin, whichever voices hold it.
    std::ranges::stable_sort(spans_, std::less<>{}, &Span::start);
    std::size_t kept = 0;
    for (std::size_t i = 1; i < spans_.size(); ++i) {
        Span& last = spans_[kept];
        if (spans_[i].hairpin == last.hairpin && spans_[i].start <= last.end) {
            last.end = std::max(last.end, spans_[i].end);
        } else {
            spans_[++kept] = spans_[i];
        }
    }
    spans_.resize(kept + 1);
    for (const Span& span : spans_) {
        const auto after =
            std::ranges::lower_bound(positions, span.end, std::less<>{}, &ColumnPosition::start);
        const bool toBarline = after == positions.end();
        const double right = (toBarline ? barline : after->x) - kHairpinEndGap;
        if (span.start.isZero() && !wedges_.empty()) {
            Wedge& last = wedges_.back();
            if (last.hairpin == span.hairpin && last.toBarline && last.last + 1 == m) {
                last.right = right; // over the barline
                last.last = m;
                last.toBarline = toBarline;
                continue;
            }
        }
        const bool continued = span.start.isZero() && m == first_ && m > 0 && staff_ != nullptr &&
                               endsWith(staff_->measure(m - 1), span.hairpin);
        wedges_.push_back(Wedge{.hairpin = span.hairpin,
                                .left = columnX(positions, span.start),
                                .right = right,
                                .measure = m,
                                .last = m,
                                .toBarline = toBarline,
                                .continued = continued});
    }
}

void DynamicMarks::placeWedges(DisplayList& list, double middle, double end) {
    const double thickness = font_.defaults().hairpinThickness;
    for (Wedge& wedge : wedges_) {
        // Clear of the marks: a mark in the first half pushes the start right,
        // one in the second half pulls the end left.
        for (std::size_t i = 0; i < marks_.size(); ++i) {
            const BoundingBox& box = font_.metrics(dynamicGlyph(marks_[i].dynamic)).box;
            const double markLeft = origins_[i] + box.southWest.x;
            const double markRight = origins_[i] + box.northEast.x;
            if (markRight + kDynamicGap <= wedge.left || markLeft - kDynamicGap >= wedge.right) {
                continue;
            }
            if ((markLeft + markRight) / 2 < (wedge.left + wedge.right) / 2) {
                wedge.left = markRight + kDynamicGap;
            } else {
                wedge.right = markLeft - kDynamicGap;
            }
        }
        wedge.right = std::min(wedge.right, end - kHairpinEndGap);
        if (wedge.right - wedge.left < kHairpinMinLength) {
            wedge.right = wedge.left + kHairpinMinLength;
        }
        const bool continues = wedge.toBarline && staff_ != nullptr &&
                               wedge.last + 1 < staff_->measureCount() &&
                               startsWith(staff_->measure(wedge.last + 1), wedge.hairpin);
        // A crescendo opens from its point, a diminuendo closes to it; a wedge
        // cut by the end of a line is half open there.
        const bool crescendo = wedge.hairpin == core::Hairpin::Crescendo;
        const double open = kHairpinOpening / 2;
        const double cut = kHairpinContinued / 2;
        const double atLeft = wedge.continued ? cut : (crescendo ? 0.0 : open);
        const double atRight = continues ? cut : (crescendo ? open : 0.0);
        for (const double side : {-1.0, 1.0}) {
            list.lines.push_back(LineItem{.from = {wedge.left, middle + side * atLeft},
                                          .to = {wedge.right, middle + side * atRight},
                                          .thickness = thickness,
                                          .role = Role::Hairpin,
                                          .measure = wedge.measure});
        }
    }
}

void DynamicMarks::place(DisplayList& list, double bottom, double staffHeight, double end) {
    if (marks_.empty() && wedges_.empty()) {
        return;
    }
    double ascent = 0.0;
    for (const Mark& mark : marks_) {
        ascent = std::max(ascent, font_.metrics(dynamicGlyph(mark.dynamic)).box.northEast.y);
    }
    // Wedges are centred on the middle of the letters of the marks.
    const double rise = font_.metrics(Glyph::DynamicMezzo).box.northEast.y / 2;
    if (!wedges_.empty()) {
        ascent = std::max(ascent, rise + kHairpinOpening / 2);
    }
    const double baseline =
        std::max(staffHeight + kDynamicBaseline, bottom + kDynamicInkGap + ascent);
    // Centred by eye under their notes, moved right off the mark before...
    origins_.clear();
    double right = -std::numeric_limits<double>::infinity();
    for (const Mark& mark : marks_) {
        const BoundingBox& box = font_.metrics(dynamicGlyph(mark.dynamic)).box;
        const double middle = font_.metrics(dynamicGlyph(mark.dynamic))
                                  .anchor(Anchor::OpticalCenter)
                                  .value_or(Point{.x = (box.southWest.x + box.northEast.x) / 2})
                                  .x;
        const double x = std::max(mark.centre - middle, right + kDynamicGap - box.southWest.x);
        origins_.push_back(x);
        right = x + box.northEast.x;
    }
    // ...and left, off the end of the line and the mark after; never off the
    // start of the line, overlapping when there is no room at all.
    double limit = end;
    for (std::size_t i = marks_.size(); i-- > 0;) {
        const BoundingBox& box = font_.metrics(dynamicGlyph(marks_[i].dynamic)).box;
        origins_[i] = std::max(std::min(origins_[i], limit - box.northEast.x), -box.southWest.x);
        limit = origins_[i] + box.southWest.x - kDynamicGap;
    }
    for (std::size_t i = 0; i < marks_.size(); ++i) {
        list.glyphs.push_back(GlyphItem{.glyph = dynamicGlyph(marks_[i].dynamic),
                                        .origin = {origins_[i], baseline},
                                        .role = Role::Dynamic,
                                        .measure = marks_[i].measure});
    }
    placeWedges(list, baseline - rise, end);
    marks_.clear();
    wedges_.clear();
}

std::string whammyLabel(int cents) {
    const int quarters = static_cast<int>(std::lround(std::abs(cents) / 50.0));
    if (quarters == 0) {
        return {};
    }
    std::string label = cents < 0 ? "-" : "+";
    if (quarters >= 4) {
        label += std::to_string(quarters / 4);
    }
    static constexpr std::array<const char*, 4> kQuarters{"", "1/4", "1/2", "3/4"};
    if (quarters % 4 != 0) {
        if (quarters >= 4) {
            label += ' ';
        }
        label += kQuarters.at(static_cast<std::size_t>(quarters % 4));
    }
    return label;
}

std::string bendLabel(int cents) {
    const int quarters = static_cast<int>(std::lround(cents / 50.0));
    if (quarters <= 0) {
        return {};
    }
    if (quarters == 4) {
        return "full";
    }
    std::string label = quarters >= 4 ? std::to_string(quarters / 4) : std::string();
    static constexpr const char* kQuarters[]{"", "1/4", "1/2", "3/4"};
    if (quarters % 4 != 0) {
        if (!label.empty()) {
            label += ' ';
        }
        label += kQuarters[quarters % 4];
    }
    return label;
}

namespace {

// Slashes of tremolo picking at `speed`: one per beam of the repeated notes.
int tremoloStrokes(core::NoteValue speed) {
    if (speed >= core::NoteValue::ThirtySecond) {
        return 3;
    }
    return speed == core::NoteValue::Sixteenth ? 2 : 1;
}

} // namespace

void addTechniques(ColumnTechniques& marks, const core::Beat& beat) {
    if (marks.whammy.isEmpty()) {
        marks.whammy = beat.effects.whammyBar;
    }
    if (beat.effects.bassTechnique != core::BassTechnique::None) {
        marks.bass = beat.effects.bassTechnique;
    }
    if (beat.effects.pickStroke != core::PickStroke::None) {
        marks.pickStroke = beat.effects.pickStroke;
    }
    if (beat.effects.strum != core::Strum::None) {
        marks.strum = beat.effects.strum;
    }
    if (beat.effects.tremoloPicking) {
        marks.tremoloStrokes =
            std::max(marks.tremoloStrokes, tremoloStrokes(*beat.effects.tremoloPicking));
    }
    for (const core::Note& note : beat.notes) {
        const core::NoteEffects& effects = note.effects;
        marks.palmMute = marks.palmMute || effects.palmMute;
        marks.letRing = marks.letRing || effects.letRing;
        marks.vibrato = std::max(marks.vibrato, effects.vibrato);
        if (marks.harmonic == core::HarmonicKind::None) {
            marks.harmonic = effects.harmonic.kind;
        }
    }
}

// ---- TechniqueLanes ---------------------------------------------------------------------

TechniqueLanes::TechniqueLanes(const EngravingFont& font) : font_(font) {}

bool TechniqueLanes::empty() const noexcept {
    return std::ranges::none_of(used_, [](bool used) { return used; });
}

void TechniqueLanes::column(const ColumnTechniques& marks, double centre, double end,
                            std::size_t measure) {
    extend(Lane::PalmMute, marks.palmMute, centre, end, measure);
    extend(Lane::LetRing, marks.letRing, centre, end, measure);
    extend(Lane::Vibrato, marks.vibrato != core::Vibrato::None, centre, end, measure,
           marks.vibrato);
    if (marks.pickStroke != core::PickStroke::None) {
        mark(Lane::PickStroke,
             marks.pickStroke == core::PickStroke::Down ? Glyph::StringsDownBow
                                                        : Glyph::StringsUpBow,
             centre, measure);
    }
    // A strum is an arrow the way the pick goes, as strumming patterns write it.
    if (marks.strum != core::Strum::None) {
        mark(Lane::Strum,
             marks.strum == core::Strum::Down ? Glyph::ArrowBlackDown : Glyph::ArrowBlackUp, centre,
             measure, kLaneGlyphScale);
    }
    if (marks.tremoloStrokes > 0) {
        constexpr std::array kSlashes{Glyph::Tremolo1, Glyph::Tremolo2, Glyph::Tremolo3};
        mark(Lane::Tremolo,
             kSlashes.at(static_cast<std::size_t>(std::min(marks.tremoloStrokes, 3) - 1)), centre,
             measure, kLaneGlyphScale);
    }
    if (marks.whammy.isEmpty()) {
        whammyRun_ = false;
    } else {
        whammy(marks.whammy, centre, end, measure);
    }
    if (marks.bass != core::BassTechnique::None) {
        static constexpr std::array<const char*, 4> kLetters{"", "T", "S", "P"};
        TextItem item;
        item.text = kLetters.at(static_cast<std::size_t>(marks.bass));
        item.origin = {centre, -kLaneBaseline};
        item.size = kLaneTextSize;
        item.role = Role::Technique;
        item.measure = measure;
        items(Lane::Bass).texts.push_back(std::move(item));
        use(Lane::Bass);
    }
    const char* label = nullptr;
    switch (marks.harmonic) {
    case core::HarmonicKind::Artificial:
        label = "A.H.";
        break;
    case core::HarmonicKind::Pinch:
        label = "P.H.";
        break;
    case core::HarmonicKind::Tapped:
        label = "T.H.";
        break;
    case core::HarmonicKind::Semi:
        label = "S.H.";
        break;
    case core::HarmonicKind::None:
    case core::HarmonicKind::Natural:
        break;
    }
    if (label != nullptr) {
        TextItem item;
        item.text = label;
        item.origin = {centre, -kLaneBaseline};
        item.size = kLaneTextSize;
        item.role = Role::Technique;
        item.measure = measure;
        items(Lane::Harmonic).texts.push_back(std::move(item));
        use(Lane::Harmonic);
    }
}

// A whammy bar: its curve from the column's centre towards the next column,
// as high as the lane allows whatever its depth, and over it how far it goes;
// the first of a run of whammy-bar columns says "w/bar".
void TechniqueLanes::whammy(const core::BendCurve& curve, double centre, double end,
                            std::size_t measure) {
    const std::vector<core::BendPoint>& points = curve.points();
    int lowest = 0;
    int highest = 0;
    for (const core::BendPoint& point : points) {
        lowest = std::min(lowest, point.cents);
        highest = std::max(highest, point.cents);
    }
    const double range = highest > lowest ? static_cast<double>(highest - lowest) : 1.0;
    const auto y = [&](int cents) {
        return -kWhammyLow - (cents - lowest) / range * (kWhammyHigh - kWhammyLow);
    };
    const double width = std::max(kWhammyMinWidth, end - centre - kWhammyEndGap);
    const auto x = [&](int position) {
        return centre + width * position / 100.0;
    };
    const double thickness = font_.defaults().stemThickness;
    DisplayList& out = items(Lane::Whammy);
    const auto segment = [&](Point from, Point to) {
        LineItem item;
        item.from = from;
        item.to = to;
        item.thickness = thickness;
        item.role = Role::Bend;
        item.measure = measure;
        out.lines.push_back(item);
    };
    // Pushed or pulled before the attack: upright from the written pitch.
    if (points.front().cents != 0) {
        segment({x(points.front().position), y(0)},
                {x(points.front().position), y(points.front().cents)});
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        segment({x(points[i - 1].position), y(points[i - 1].cents)},
                {x(points[i].position), y(points[i].cents)});
    }
    if (points.size() == 1) {
        segment({x(0), y(points.front().cents)}, {x(100), y(points.front().cents)});
    }
    std::string label = whammyLabel(core::whammyCents(curve));
    if (!whammyRun_) {
        label = label.empty() ? "w/bar" : "w/bar " + label;
    }
    whammyRun_ = true;
    if (!label.empty()) {
        TextItem item;
        item.text = std::move(label);
        item.origin = {centre, -kWhammyBaseline};
        item.size = kWhammyTextSize;
        item.align = TextAlign::Left;
        item.role = Role::Bend;
        item.measure = measure;
        out.texts.push_back(std::move(item));
    }
    use(Lane::Whammy);
}

void TechniqueLanes::mark(Lane lane, Glyph glyph, double centre, std::size_t measure,
                          double scale) {
    const BoundingBox& box = font_.metrics(glyph).box;
    items(lane).glyphs.push_back(
        GlyphItem{.glyph = glyph,
                  .origin = {centre - (box.southWest.x + box.northEast.x) * scale / 2,
                             -kLaneBaseline + box.southWest.y * scale},
                  .role = Role::Technique,
                  .measure = measure,
                  .scale = scale});
    use(lane);
}

void TechniqueLanes::extend(Lane lane, bool active, double centre, double end, std::size_t measure,
                            core::Vibrato vibrato) {
    Run& run = runs_[static_cast<std::size_t>(lane)];
    if (!active || (run.open && run.vibrato != vibrato)) {
        close(lane);
    }
    if (!active) {
        return;
    }
    if (!run.open) {
        run = Run{.open = true, .from = centre, .to = end, .measure = measure, .vibrato = vibrato};
        use(lane);
        return;
    }
    run.to = std::max(run.to, end);
}

void TechniqueLanes::close(Lane lane) {
    Run& run = runs_[static_cast<std::size_t>(lane)];
    if (!run.open) {
        return;
    }
    run.open = false;
    DisplayList& out = items(lane);
    if (lane == Lane::Vibrato) {
        const Glyph stroke = run.vibrato == core::Vibrato::Wide ? Glyph::GuitarWideVibratoStroke
                                                                : Glyph::GuitarVibratoStroke;
        const double advance = font_.metrics(stroke).advanceWidth;
        const double from = run.from - advance / 2;
        const int count =
            std::max(1, static_cast<int>(std::floor((run.to - from) / advance + kEpsilon)));
        for (int i = 0; i < count; ++i) {
            out.glyphs.push_back(GlyphItem{.glyph = stroke,
                                           .origin = {from + i * advance, -kVibratoLift},
                                           .role = Role::Vibrato,
                                           .measure = run.measure});
        }
        return;
    }
    const std::string label = lane == Lane::PalmMute ? "P.M." : "let ring";
    const double left = run.from - 0.3;
    TextItem item;
    item.text = label;
    item.origin = {left, -kLaneBaseline};
    item.size = kLaneTextSize;
    item.align = TextAlign::Left;
    item.italic = lane == Lane::LetRing;
    item.role = Role::Technique;
    item.measure = run.measure;
    out.texts.push_back(std::move(item));
    const double lineFrom = left + textWidth(label, kLaneTextSize) + kRunLineGap;
    if (run.to - lineFrom < 2 * kRunDash) {
        return;
    }
    const double y = -kLaneBaseline - capHeight(kLaneTextSize) / 2;
    const double thickness = font_.defaults().repeatEndingLineThickness * 0.75;
    LineItem dashes;
    dashes.from = {lineFrom, y};
    dashes.to = {run.to, y};
    dashes.thickness = thickness;
    dashes.role = Role::Technique;
    dashes.measure = run.measure;
    dashes.dash = kRunDash;
    out.lines.push_back(dashes);
    LineItem hook;
    hook.from = {run.to, y};
    hook.to = {run.to, y + kRunHook};
    hook.thickness = thickness;
    hook.role = Role::Technique;
    hook.measure = run.measure;
    out.lines.push_back(hook);
}

void TechniqueLanes::place(DisplayList& list, double top) {
    for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
        close(static_cast<Lane>(lane));
    }
    double base = std::min(top, 0.0) - kLaneGap;
    for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
        if (!used_[lane]) {
            continue;
        }
        bottom_[lane] = base;
        shift(items_[lane], 0.0, base);
        append(list, std::move(items_[lane]));
        items_[lane] = DisplayList{};
        base -= kLaneHeight[lane];
    }
}

// ---- ChordRow ---------------------------------------------------------------------------

void ChordRow::collect(const core::Measure& measure, std::vector<ColumnInk>& ink, double centre) {
    read_.clear();
    for (const core::Voice& voice : measure.voices) {
        core::Fraction at;
        for (const core::Beat& beat : voice.beats) {
            if (beat.chord && std::ranges::find(read_, at, &Read::start) == read_.end()) {
                read_.push_back(
                    Read{.start = at, .length = beat.duration.length(), .symbol = &*beat.chord});
            }
            at += beat.duration.length();
        }
    }
    if (read_.empty()) {
        return;
    }
    std::ranges::sort(read_, {}, &Read::start);
    for (const Read& chord : read_) {
        auto column = std::ranges::lower_bound(ink, chord.start, {}, &ColumnInk::start);
        if (column == ink.end() || column->start != chord.start) {
            column = ink.insert(column, ColumnInk{.start = chord.start,
                                                  .longest = chord.length,
                                                  .left = centre,
                                                  .right = centre});
        }
        const auto [left, right] = reach(*chord.symbol);
        column->aboveLeft = centre - left;
        column->aboveRight = centre + right;
        column->above = true;
    }
}

void ChordRow::draw(std::span<const ColumnPosition> positions, double centre, std::size_t measure) {
    for (const Read& chord : read_) {
        marks_.push_back(Mark{.symbol = chord.symbol,
                              .centre = columnX(positions, chord.start) + centre,
                              .measure = measure});
    }
}

std::pair<double, double> ChordRow::reach(const core::ChordSymbol& symbol) const {
    const double name = textWidth(symbol.name, kChordNameSize) / 2;
    double left = name;
    double right = name;
    if (showsDiagram(symbol)) {
        const core::ChordDiagram& shape = *symbol.diagram;
        const double strings = stringsWidth(shape) / 2;
        const double half =
            strings + font_.metrics(Glyph::FretboardX).box.width() * kChordMarkScale / 2;
        right = std::max(right, half);
        left = std::max(left, half);
        if (shape.firstFret > 1) {
            left = std::max(left,
                            strings + kChordFretNumberGap +
                                textWidth(std::to_string(shape.firstFret), kChordFretNumberSize));
        }
    }
    return {left, right};
}

void ChordRow::place(DisplayList& list, double top) {
    const double markHeight = font_.metrics(Glyph::FretboardO).box.height() * kChordMarkScale;
    double tallest = 0.0;
    for (const Mark& mark : marks_) {
        if (showsDiagram(*mark.symbol)) {
            tallest = std::max(tallest, markHeight + kChordMarkGap +
                                            fretsShown(*mark.symbol->diagram) * kChordFretSpace);
        }
    }
    // Diagrams hang from one line, the names on one baseline above them.
    const double diagramTop = std::min(top, 0.0) - kChordInkGap - tallest;
    const double baseline =
        diagramTop - (tallest > 0.0 ? kChordNameGap : 0.0) - kTextDescent * kChordNameSize;
    for (const Mark& mark : marks_) {
        if (!mark.symbol->name.empty()) {
            list.texts.push_back(TextItem{.text = mark.symbol->name,
                                          .origin = {mark.centre, baseline},
                                          .size = kChordNameSize,
                                          .align = TextAlign::Centre,
                                          .italic = false,
                                          .knockout = false,
                                          .role = Role::Chord,
                                          .measure = mark.measure});
        }
        if (showsDiagram(*mark.symbol)) {
            diagram(list, *mark.symbol->diagram, mark.centre, diagramTop, mark.measure);
        }
    }
    marks_.clear();
}

void ChordRow::diagram(DisplayList& list, const core::ChordDiagram& shape, double centre,
                       double top, std::size_t measure) const {
    const double markHeight = font_.metrics(Glyph::FretboardO).box.height() * kChordMarkScale;
    const double nut = top + markHeight + kChordMarkGap;
    const int frets = fretsShown(shape);
    const double half = stringsWidth(shape) / 2;
    const auto line = [&](Point from, Point to, double thickness) {
        list.lines.push_back(LineItem{.from = from,
                                      .to = to,
                                      .thickness = thickness,
                                      .role = Role::Chord,
                                      .measure = measure,
                                      .dash = 0.0});
    };
    // String 1, the highest, on the right.
    const auto stringX = [&](std::size_t i) {
        return centre + half - static_cast<double>(i) * kChordStringSpace;
    };
    const double bottom = nut + frets * kChordFretSpace;
    for (std::size_t i = 0; i < shape.frets.size(); ++i) {
        line({stringX(i), nut}, {stringX(i), bottom}, kChordLineThickness);
    }
    for (int fret = 0; fret <= frets; ++fret) {
        const double y = nut + fret * kChordFretSpace;
        const double ends = kChordLineThickness / 2;
        line({centre - half - ends, y}, {centre + half + ends, y},
             fret == 0 && shape.firstFret == 1 ? kChordNutThickness : kChordLineThickness);
    }
    if (shape.firstFret > 1) {
        list.texts.push_back(
            TextItem{.text = std::to_string(shape.firstFret),
                     .origin = {centre - half - kChordFretNumberGap,
                                nut + kChordFretSpace / 2 + capHeight(kChordFretNumberSize) / 2},
                     .size = kChordFretNumberSize,
                     .align = TextAlign::Right,
                     .italic = false,
                     .knockout = false,
                     .role = Role::Chord,
                     .measure = measure});
    }
    const auto mark = [&](Glyph glyph, double x, double y) {
        const BoundingBox& box = font_.metrics(glyph).box;
        list.glyphs.push_back(
            GlyphItem{.glyph = glyph,
                      .origin = {x - (box.southWest.x + box.northEast.x) / 2 * kChordMarkScale,
                                 y + (box.southWest.y + box.northEast.y) / 2 * kChordMarkScale},
                      .role = Role::Chord,
                      .measure = measure,
                      .scale = kChordMarkScale});
    };
    for (std::size_t i = 0; i < shape.frets.size(); ++i) {
        const int fret = shape.frets[i];
        if (fret == core::ChordDiagram::kMuted) {
            mark(Glyph::FretboardX, stringX(i), top + markHeight / 2);
        } else if (fret == 0) {
            mark(Glyph::FretboardO, stringX(i), top + markHeight / 2);
        } else if (fret >= shape.firstFret && fret < shape.firstFret + frets) {
            mark(Glyph::FretboardFilledCircle, stringX(i),
                 nut + (fret - shape.firstFret + 0.5) * kChordFretSpace);
        }
    }
}

// ---- StaffWriter ------------------------------------------------------------------------

StaffWriter::StaffWriter(const EngravingFont& font, double height, std::size_t lines)
    : font_(font), defaults_(font.defaults()), height_(height), lines_(lines) {}

double StaffWriter::barline(std::size_t m, double x, bool songEnd, bool repeatEnd) {
    measure_ = m;
    if (repeatEnd) {
        x = repeatDots(x) + defaults_.repeatBarlineDotSeparation;
    }
    const double thin = defaults_.thinBarlineThickness;
    line({x + thin / 2, 0.0}, {x + thin / 2, height_}, thin, Role::Barline);
    x += thin;
    if (songEnd || repeatEnd) {
        const double thick = defaults_.thickBarlineThickness;
        x += defaults_.thinThickBarlineSeparation;
        line({x + thick / 2, 0.0}, {x + thick / 2, height_}, thick, Role::Barline);
        x += thick;
    }
    return x;
}

double StaffWriter::opening(std::size_t m, double x) {
    measure_ = m;
    const double thick = defaults_.thickBarlineThickness;
    line({x + thick / 2, 0.0}, {x + thick / 2, height_}, thick, Role::Barline);
    x += thick + defaults_.thinThickBarlineSeparation;
    const double thin = defaults_.thinBarlineThickness;
    line({x + thin / 2, 0.0}, {x + thin / 2, height_}, thin, Role::Barline);
    return repeatDots(x + thin + defaults_.repeatBarlineDotSeparation);
}

double StaffWriter::repeatDots(double x) {
    // In the two spaces around the middle line, or either side of the middle
    // space when there are an odd number of them (tablature).
    const std::size_t spaces = lines_ > 1 ? lines_ - 1 : 0;
    const double spacing = spaces > 0 ? height_ / static_cast<double>(spaces) : 1.0;
    const double offset = spaces % 2 == 0 ? spacing / 2 : spacing;
    const double middle = height_ / 2;
    const double centre = x + widthOf(Glyph::RepeatDot) / 2;
    glyphCentred(Glyph::RepeatDot, centre, middle - offset, Role::Repeat);
    glyphCentred(Glyph::RepeatDot, centre, middle + offset, Role::Repeat);
    return x + widthOf(Glyph::RepeatDot);
}

DisplayList StaffWriter::finish(double width) {
    list_.width = width;
    close(width);
    std::vector<LineItem> staff;
    const double spacing = lines_ > 1 ? height_ / static_cast<double>(lines_ - 1) : 0.0;
    for (std::size_t i = 0; i < lines_; ++i) {
        const double y = spacing * static_cast<double>(i);
        staff.push_back(LineItem{.from = {0.0, y},
                                 .to = {width, y},
                                 .thickness = defaults_.staffLineThickness,
                                 .role = Role::StaffLine,
                                 .measure = list_.measures.empty() ? 0 : list_.measures[0].index,
                                 .dash = 0.0});
    }
    list_.lines.insert(list_.lines.begin(), staff.begin(), staff.end());
    measureExtents(list_, font_, height_);
    return std::exchange(list_, DisplayList{});
}

void StaffWriter::clearItems() {
    list_.glyphs.clear();
    list_.lines.clear();
    list_.beams.clear();
    list_.texts.clear();
    list_.curves.clear();
    list_.polygons.clear();
}

void StaffWriter::glyphCentred(Glyph which, double x, double y, Role role) {
    const BoundingBox& box = metrics(which).box;
    glyph(which, x - (box.southWest.x + box.northEast.x) / 2,
          y + (box.southWest.y + box.northEast.y) / 2, role);
}

void StaffWriter::text(std::string content, Point origin, double size, Role role, TextAlign align,
                       bool knockout) {
    TextItem item;
    item.text = std::move(content);
    item.origin = origin;
    item.size = size;
    item.align = align;
    item.knockout = knockout;
    item.role = role;
    item.measure = measure_;
    list_.texts.push_back(std::move(item));
}

void StaffWriter::curve(Point from, Point to, double height, double thickness, double endThickness,
                        Role role) {
    // Control points at a quarter and three quarters of the way, 4/3 of the
    // wanted height off the chord: the curve's middle then reaches `height`.
    const double lift = height * 4.0 / 3.0;
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    CurveItem item;
    item.from = from;
    item.control1 = {from.x + dx / 4, from.y + dy / 4 + lift};
    item.control2 = {from.x + dx * 3 / 4, from.y + dy * 3 / 4 + lift};
    item.to = to;
    item.thickness = thickness;
    item.endThickness = endThickness;
    item.role = role;
    item.measure = measure_;
    list_.curves.push_back(item);
}

void StaffWriter::polygon(std::vector<Point> points, Role role) {
    PolygonItem item;
    item.points = std::move(points);
    item.role = role;
    item.measure = measure_;
    list_.polygons.push_back(std::move(item));
}

double StaffWriter::timeSignature(const core::TimeSignature& time, double x, double middle) {
    const auto digitsWidth = [this](const std::string& digits) {
        double width = 0.0;
        for (const char digit : digits) {
            width += metrics(offsetGlyph(Glyph::TimeSig0, digit - '0')).advanceWidth;
        }
        return width;
    };
    const std::string numerator = std::to_string(time.numerator());
    const std::string denominator = std::to_string(time.denominator());
    const double width = std::max(digitsWidth(numerator), digitsWidth(denominator));
    const auto draw = [&](const std::string& digits, double y) {
        double cursor = x + (width - digitsWidth(digits)) / 2;
        for (const char digit : digits) {
            const Glyph which = offsetGlyph(Glyph::TimeSig0, digit - '0');
            glyph(which, cursor, y, Role::TimeSignature);
            cursor += metrics(which).advanceWidth;
        }
    };
    draw(numerator, middle - 1.0);
    draw(denominator, middle + 1.0);
    return x + width;
}

double StaffWriter::inkTop() const {
    return inkExtents(list_, font_, height_).top;
}

double StaffWriter::inkBottom() const {
    return inkExtents(list_, font_, height_).bottom;
}

StaffWriter::TupletNumber StaffWriter::tupletNumber(int count) const {
    TupletNumber number{.digits = std::to_string(count),
                        .width = 0.0,
                        .top = std::numeric_limits<double>::lowest(),
                        .bottom = std::numeric_limits<double>::max()};
    for (const char digit : number.digits) {
        const BoundingBox& box = metrics(offsetGlyph(Glyph::Tuplet0, digit - '0')).box;
        number.width += box.width();
        number.top = std::max(number.top, box.northEast.y);
        number.bottom = std::min(number.bottom, box.southWest.y);
    }
    return number;
}

void StaffWriter::drawTupletNumber(const TupletNumber& number, double x, double origin) {
    double from = x - number.width / 2;
    for (const char digit : number.digits) {
        from = glyphFrom(offsetGlyph(Glyph::Tuplet0, digit - '0'), from, origin, Role::Tuplet);
    }
}

void StaffWriter::tupletBracket(const TupletNumber& number, double left, double right, double y,
                                bool above) {
    const double middle = (left + right) / 2;
    const double hook = y + (above ? kTupletHook : -kTupletHook);
    const double thickness = defaults_.tupletBracketThickness;
    line({left, hook}, {left, y}, thickness, Role::Tuplet);
    line({left, y}, {middle - number.width / 2 - kTupletNumberGap, y}, thickness, Role::Tuplet);
    line({middle + number.width / 2 + kTupletNumberGap, y}, {right, y}, thickness, Role::Tuplet);
    line({right, y}, {right, hook}, thickness, Role::Tuplet);
    drawTupletNumber(number, middle, y + (number.top + number.bottom) / 2);
}

std::vector<std::vector<std::size_t>> beamGroups(std::span<const RhythmItem> items,
                                                 const core::TimeSignature& time) {
    const bool compound = time.isCompound() && time.denominator() >= 8;
    const core::Fraction window(compound ? 3 : 1, time.denominator());
    std::vector<std::vector<std::size_t>> groups;
    std::vector<std::size_t> group;
    std::int64_t groupWindow = -1;
    const auto flush = [&] {
        if (group.size() >= 2) {
            groups.push_back(group);
        }
        group.clear();
    };
    for (std::size_t i = 0; i < items.size(); ++i) {
        const RhythmItem& item = items[i];
        const core::Fraction position = item.start / window;
        const std::int64_t index = position.numerator() / position.denominator();
        const bool fits = item.start + item.duration.length() <= window * core::Fraction(index + 1);
        if (item.rest || beamCount(item.duration.value()) == 0 || !fits) {
            flush();
            continue;
        }
        if (index != groupWindow) {
            flush();
            groupWindow = index;
        }
        group.push_back(i);
    }
    flush();
    return groups;
}

std::vector<TupletSpan> tupletSpans(std::span<const RhythmItem> items) {
    std::vector<TupletSpan> spans;
    std::optional<TupletSpan> open;
    core::Fraction written;
    core::Fraction shortest;
    const auto close = [&] {
        if (open) {
            spans.push_back(*open);
            open.reset();
        }
    };
    for (std::size_t i = 0; i < items.size(); ++i) {
        const core::Duration& duration = items[i].duration;
        const core::Tuplet tuplet = duration.tuplet();
        if (open && open->tuplet != tuplet) {
            close();
        }
        if (tuplet.isNone()) {
            continue;
        }
        const core::NoteValue value = duration.value();
        if (!open) {
            open = TupletSpan{.first = i, .last = i, .tuplet = tuplet};
            written = core::Fraction();
            shortest = core::noteValueLength(value);
        }
        open->last = i;
        written += core::Duration(value, duration.dots()).length();
        shortest = std::min(shortest, core::noteValueLength(value));
        const core::Fraction unit = written / core::Fraction(tuplet.actual());
        const std::int64_t den = unit.denominator();
        if (unit.numerator() == 1 && (den & (den - 1)) == 0 && unit >= shortest) {
            close();
        }
    }
    close();
    return spans;
}

} // namespace elara::notation::detail
