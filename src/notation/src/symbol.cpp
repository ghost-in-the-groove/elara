// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/symbol.hpp>

#include "ink.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace elara::notation {

namespace {

// Pieces of one symbol are drawn this near one another, in staff spaces: the
// sharps of a key signature, a tempo's note and its number. Two staves of a
// system are further apart.
constexpr double kJoin = 1.0;

// One drawn item of a symbol.
struct Piece {
    Role role{};
    std::size_t measure = 0;
    Rect box;
};

Rect around(Point a, Point b, double margin) {
    const double left = std::min(a.x, b.x) - margin;
    const double top = std::min(a.y, b.y) - margin;
    return {.x = left,
            .y = top,
            .width = std::max(a.x, b.x) + margin - left,
            .height = std::max(a.y, b.y) + margin - top};
}

Rect united(const Rect& a, const Rect& b) {
    const double left = std::min(a.x, b.x);
    const double top = std::min(a.y, b.y);
    return {.x = left,
            .y = top,
            .width = std::max(a.x + a.width, b.x + b.width) - left,
            .height = std::max(a.y + a.height, b.y + b.height) - top};
}

// How far apart two rectangles are: 0 when they touch or overlap.
double gap(const Rect& a, const Rect& b) {
    const double dx = std::max({0.0, a.x - (b.x + b.width), b.x - (a.x + a.width)});
    const double dy = std::max({0.0, a.y - (b.y + b.height), b.y - (a.y + a.height)});
    return std::max(dx, dy);
}

bool holds(const Rect& box, Point point, double slack) {
    return point.x >= box.x - slack && point.x <= box.x + box.width + slack &&
           point.y >= box.y - slack && point.y <= box.y + box.height + slack;
}

bool wanted(std::span<const Role> roles, Role role) {
    return std::ranges::find(roles, role) != roles.end();
}

// Every item of `roles`, with the rectangle round its ink.
std::vector<Piece> piecesOf(const DisplayList& list, std::span<const Role> roles,
                            const EngravingFont& font) {
    std::vector<Piece> pieces;
    for (const GlyphItem& glyph : list.glyphs) {
        if (!wanted(roles, glyph.role)) {
            continue;
        }
        const Rect ink = detail::glyphInk(glyph, font);
        if (ink.width <= 0.0 || ink.height <= 0.0) {
            continue;
        }
        pieces.push_back({glyph.role, glyph.measure, ink});
    }
    for (const TextItem& text : list.texts) {
        if (!wanted(roles, text.role) || text.text.empty()) {
            continue;
        }
        pieces.push_back({text.role, text.measure, detail::textInk(text)});
    }
    for (const LineItem& line : list.lines) {
        if (wanted(roles, line.role)) {
            pieces.push_back(
                {line.role, line.measure, around(line.from, line.to, line.thickness / 2.0)});
        }
    }
    for (const CurveItem& curve : list.curves) {
        if (wanted(roles, curve.role)) {
            pieces.push_back({curve.role, curve.measure,
                              united(around(curve.from, curve.control1, 0.0),
                                     around(curve.control2, curve.to, 0.0))});
        }
    }
    for (const PolygonItem& polygon : list.polygons) {
        if (!wanted(roles, polygon.role) || polygon.points.empty()) {
            continue;
        }
        Rect box = around(polygon.points.front(), polygon.points.front(), 0.0);
        for (const Point& corner : polygon.points) {
            box = united(box, around(corner, corner, 0.0));
        }
        pieces.push_back({polygon.role, polygon.measure, box});
    }
    return pieces;
}

// The staff `box` is drawn on, or the nearest one of the system it is in.
std::optional<std::size_t> staffOf(const DisplayList& list, const Rect& box) {
    std::optional<std::size_t> nearest;
    double least = std::numeric_limits<double>::infinity();
    const double middle = box.y + (box.height / 2.0);
    const double centre = box.x + (box.width / 2.0);
    for (std::size_t i = 0; i < list.staves.size(); ++i) {
        const StaffBox& staff = list.staves[i];
        // A staff of another system across the page is not this symbol's.
        const double beside = std::max({0.0, staff.x - centre, centre - (staff.x + staff.width)});
        const double away = std::max({0.0, staff.y - middle, middle - (staff.y + staff.height)});
        const double distance = away + (beside > 0.0 ? 1.0e6 : 0.0);
        if (distance < least) {
            least = distance;
            nearest = i;
        }
    }
    return nearest;
}

} // namespace

std::optional<Symbol> symbolAt(const DisplayList& list, Point point, std::span<const Role> roles,
                               const EngravingFont& font, double slack) {
    const std::vector<Piece> pieces = piecesOf(list, roles, font);
    const Piece* hit = nullptr;
    for (const Piece& piece : pieces) {
        if (holds(piece.box, point, slack) &&
            (hit == nullptr ||
             piece.box.width * piece.box.height < hit->box.width * hit->box.height)) {
            hit = &piece;
        }
    }
    if (hit == nullptr) {
        return std::nullopt;
    }
    // The symbol is the piece under the point and what is drawn with it: the
    // pieces of its role and measure that are near one already taken.
    std::vector<const Piece*> taken{hit};
    std::vector<bool> used(pieces.size(), false);
    used[static_cast<std::size_t>(hit - pieces.data())] = true;
    for (bool grown = true; grown;) {
        grown = false;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const Piece& piece = pieces[i];
            if (used[i] || piece.role != hit->role || piece.measure != hit->measure) {
                continue;
            }
            const bool near = std::ranges::any_of(taken, [&piece](const Piece* other) {
                return gap(piece.box, other->box) <= kJoin;
            });
            if (near) {
                used[i] = true;
                taken.push_back(&piece);
                grown = true;
            }
        }
    }
    Rect box = hit->box;
    for (const Piece* piece : taken) {
        box = united(box, piece->box);
    }
    return Symbol{
        .role = hit->role, .measure = hit->measure, .box = box, .staff = staffOf(list, box)};
}

} // namespace elara::notation
