// SPDX-License-Identifier: AGPL-3.0-or-later
// Where the playback cursor stands on laid-out pages.
#include <elara/notation/cursor.hpp>

#include <algorithm>
#include <iterator>
#include <limits>

namespace elara::notation {

namespace {

// How far above or below its staves a click still belongs to a system, and
// how far left of a column it may start (a notehead's width), in staff spaces.
constexpr double kSystemReach = 6.0;
constexpr double kColumnReach = 1.0;

// From the onset that has started to the next one (or to the measure's ends)
// in proportion to the time between them.
double xAt(const MeasureBox& box, double through) noexcept {
    const auto next = std::ranges::upper_bound(box.onsets, through, {}, &MeasureOnset::at);
    const MeasureOnset from =
        next == box.onsets.begin() ? MeasureOnset{.at = 0.0, .x = box.x} : *std::prev(next);
    const MeasureOnset to =
        next == box.onsets.end() ? MeasureOnset{.at = 1.0, .x = box.x + box.width} : *next;
    const double span = to.at - from.at;
    return span > 0.0 ? from.x + (through - from.at) / span * (to.x - from.x) : from.x;
}

} // namespace

std::optional<Cursor> cursorAt(std::span<const DisplayList> pages, std::size_t measure,
                               double fraction) noexcept {
    const double through = std::clamp(fraction, 0.0, 1.0);
    for (std::size_t p = 0; p < pages.size(); ++p) {
        for (const MeasureBox& box : pages[p].measures) {
            if (box.index == measure) {
                return Cursor{.page = p, .x = xAt(box, through), .y = box.y, .height = box.height};
            }
        }
    }
    return std::nullopt;
}

std::optional<MeasurePosition> measurePositionAt(const DisplayList& page, double x,
                                                 double y) noexcept {
    const MeasureBox* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (const MeasureBox& box : page.measures) {
        if (x < box.x || x > box.x + box.width) {
            continue;
        }
        const double distance = std::max({box.y - y, y - (box.y + box.height), 0.0});
        if (distance < nearestDistance) {
            nearest = &box;
            nearestDistance = distance;
        }
    }
    if (nearest == nullptr || nearestDistance > kSystemReach) {
        return std::nullopt;
    }
    double fraction = 0.0;
    for (const MeasureOnset& onset : nearest->onsets) {
        if (onset.x > x + kColumnReach) {
            break;
        }
        fraction = onset.at;
    }
    return MeasurePosition{.measure = nearest->index, .fraction = fraction};
}

} // namespace elara::notation
