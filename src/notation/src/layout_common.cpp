// SPDX-License-Identifier: AGPL-3.0-or-later
#include "layout_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation::detail {

namespace {

// Technique lanes, in staff spaces.
constexpr double kLaneGap = 0.6;       // between the staff's ink and the first lane
constexpr double kLaneTextSize = 0.95; // "P.M.", "let ring", harmonic labels
constexpr double kLaneBaseline = 0.2;  // text baseline above the bottom of its lane
constexpr double kRunDash = 0.3;       // dashes of the line after "P.M."
constexpr double kRunHook = 0.4;       // the line's closing hook
constexpr double kRunLineGap = 0.25;   // between the text and its line
constexpr double kVibratoLift = 0.25;  // vibrato strokes above the bottom of their lane
constexpr double kLaneHeight[kLaneCount]{1.25, 1.2, 1.15, 1.15, 1.05, 2.3};

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
        cover(item.origin.y - box.northEast.y);
        cover(item.origin.y - box.southWest.y);
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

void addTechniques(ColumnTechniques& marks, const core::Beat& beat) {
    if (beat.effects.pickStroke != core::PickStroke::None) {
        marks.pickStroke = beat.effects.pickStroke;
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
        const Glyph bow = marks.pickStroke == core::PickStroke::Down ? Glyph::StringsDownBow
                                                                     : Glyph::StringsUpBow;
        const BoundingBox& box = font_.metrics(bow).box;
        items(Lane::PickStroke)
            .glyphs.push_back(GlyphItem{.glyph = bow,
                                        .origin = {centre - (box.southWest.x + box.northEast.x) / 2,
                                                   -kLaneBaseline + box.southWest.y},
                                        .role = Role::Technique,
                                        .measure = measure});
        use(Lane::PickStroke);
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

} // namespace elara::notation::detail
