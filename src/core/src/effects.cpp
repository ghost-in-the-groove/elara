// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace elara::core {

BendCurve::BendCurve(std::vector<BendPoint> points) : points_(std::move(points)) {
    if (points_.size() > kMaxPoints) {
        throw std::invalid_argument("BendCurve: too many points");
    }
    for (std::size_t i = 0; i < points_.size(); ++i) {
        const BendPoint& point = points_[i];
        if (point.position < 0 || point.position > 100) {
            throw std::invalid_argument("BendCurve: position outside 0..100");
        }
        if (point.cents < -kMaxCents || point.cents > kMaxCents) {
            throw std::invalid_argument("BendCurve: offset too large");
        }
        if (i > 0 && point.position <= points_[i - 1].position) {
            throw std::invalid_argument("BendCurve: positions must increase");
        }
    }
}

BendCurve BendCurve::bend(int cents) {
    return BendCurve({{.position = 0, .cents = 0},
                      {.position = 50, .cents = cents},
                      {.position = 100, .cents = cents}});
}

BendCurve BendCurve::bendRelease(int cents) {
    return BendCurve({{.position = 0, .cents = 0},
                      {.position = 33, .cents = cents},
                      {.position = 66, .cents = cents},
                      {.position = 100, .cents = 0}});
}

BendCurve BendCurve::preBend(int cents) {
    return BendCurve({{.position = 0, .cents = cents}, {.position = 100, .cents = cents}});
}

BendCurve BendCurve::preBendRelease(int cents) {
    return BendCurve({{.position = 0, .cents = cents},
                      {.position = 50, .cents = cents},
                      {.position = 100, .cents = 0}});
}

int BendCurve::centsAt(int position) const noexcept {
    if (points_.empty()) {
        return 0;
    }
    if (position <= points_.front().position) {
        return points_.front().cents;
    }
    if (position >= points_.back().position) {
        return points_.back().cents;
    }
    const auto next = std::ranges::find_if(
        points_, [position](const BendPoint& point) { return point.position > position; });
    const BendPoint& to = *next;
    const BendPoint& from = *(next - 1);
    const double t = static_cast<double>(position - from.position) /
                     static_cast<double>(to.position - from.position);
    return static_cast<int>(std::lround(static_cast<double>(from.cents) +
                                        t * static_cast<double>(to.cents - from.cents)));
}

int BendCurve::maxCents() const noexcept {
    int highest = 0;
    for (const BendPoint& point : points_) {
        highest = std::max(highest, point.cents);
    }
    return highest;
}

std::optional<int> naturalHarmonicSemitones(double fret) {
    // A fret sits at 1 - 2^(-fret/12) of the string length from the nut; a
    // natural harmonic needs that point near a node k/n of partial n.
    const double position = 1.0 - std::exp2(-fret / 12.0);
    constexpr int kMaxPartial = 16;
    constexpr double kTolerance = 0.05;
    for (int partial = 1; partial <= kMaxPartial; ++partial) {
        const double nodes = position * partial;
        const double nearest = std::round(nodes);
        if (nearest >= 1.0 && std::abs(nodes - nearest) <= kTolerance) {
            return static_cast<int>(std::lround(12.0 * std::log2(static_cast<double>(partial))));
        }
    }
    return std::nullopt;
}

} // namespace elara::core
