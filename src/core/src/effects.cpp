// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

constexpr std::array<std::string_view, 6> kFingeringTexts{"", "1", "2", "3", "4", "5"};
constexpr std::array<std::string_view, 6> kFrettingTexts{"", "T", "1", "2", "3", "4"};
constexpr std::array<std::string_view, 6> kPluckTexts{"", "p", "i", "m", "a", "c"};

std::size_t indexOf(Finger finger) noexcept {
    return static_cast<std::size_t>(finger) % kFingeringTexts.size();
}

} // namespace

std::string_view fingeringText(Finger finger, bool fretted) noexcept {
    return (fretted ? kFrettingTexts : kFingeringTexts)[indexOf(finger)];
}

std::optional<Finger> fingerOfText(std::string_view text, bool fretted) noexcept {
    if (text == "0") {
        return Finger::None;
    }
    if (fretted && text == "t") {
        return Finger::Thumb;
    }
    const auto& texts = fretted ? kFrettingTexts : kFingeringTexts;
    for (std::size_t i = 1; i < texts.size(); ++i) {
        if (texts[i] == text) {
            return static_cast<Finger>(i);
        }
    }
    return std::nullopt;
}

std::string_view pluckText(Finger finger) noexcept {
    return kPluckTexts[indexOf(finger)];
}

std::optional<Finger> pluckOfText(std::string_view text) noexcept {
    if (text == "e" || text == "x") {
        return Finger::Little;
    }
    for (std::size_t i = 1; i < kPluckTexts.size(); ++i) {
        if (kPluckTexts[i] == text) {
            return static_cast<Finger>(i);
        }
    }
    return std::nullopt;
}

Finger nextFingering(Finger finger, bool fretted) noexcept {
    // After none, the thumb, the index, ... the little finger.
    constexpr std::array kAfterFretting{Finger::Index, Finger::None,   Finger::Middle,
                                        Finger::Ring,  Finger::Little, Finger::Thumb};
    if (fretted) {
        return kAfterFretting[indexOf(finger)];
    }
    return static_cast<Finger>((indexOf(finger) + 1) % kFingeringTexts.size());
}

Finger nextPluck(Finger finger) noexcept {
    return static_cast<Finger>((indexOf(finger) + 1) % kPluckTexts.size());
}

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

BendForm bendForm(const BendCurve& curve) {
    const std::vector<BendPoint>& points = curve.points();
    if (points.empty()) {
        return BendForm::None;
    }
    const int top = curve.maxCents();
    const bool dips = std::ranges::any_of(points, [](const BendPoint& p) { return p.cents < 0; });
    if (top == 0 || dips) {
        return BendForm::Custom;
    }
    // Rises (never falling) to its top, then falls (never rising) to its end.
    std::size_t i = 1;
    while (i < points.size() && points[i].cents >= points[i - 1].cents) {
        ++i;
    }
    while (i < points.size() && points[i].cents <= points[i - 1].cents) {
        ++i;
    }
    if (i < points.size()) {
        return BendForm::Custom;
    }
    const int first = points.front().cents;
    const int last = points.back().cents;
    if (first == 0) {
        return last == top ? BendForm::Bend : last == 0 ? BendForm::BendRelease : BendForm::Custom;
    }
    if (first == top) {
        return last == top ? BendForm::PreBend
               : last == 0 ? BendForm::PreBendRelease
                           : BendForm::Custom;
    }
    return BendForm::Custom;
}

BendCurve bendCurve(BendForm form, int cents) {
    if (form == BendForm::None) {
        return {};
    }
    if (cents < 1 || cents > BendCurve::kMaxCents) {
        throw std::invalid_argument("bendCurve: the bend must go up by 1 to 2400 cents");
    }
    switch (form) {
    case BendForm::Bend:
        return BendCurve::bend(cents);
    case BendForm::BendRelease:
        return BendCurve::bendRelease(cents);
    case BendForm::PreBend:
        return BendCurve::preBend(cents);
    case BendForm::PreBendRelease:
        return BendCurve::preBendRelease(cents);
    case BendForm::None:
    case BendForm::Custom:
        break;
    }
    throw std::invalid_argument("bendCurve: a custom bend has no usual curve");
}

namespace {

// `curve` turned upside down around the written pitch.
BendCurve mirrored(const BendCurve& curve) {
    std::vector<BendPoint> points = curve.points();
    for (BendPoint& point : points) {
        point.cents = -point.cents;
    }
    return BendCurve(std::move(points));
}

} // namespace

BendForm whammyForm(const BendCurve& curve) {
    const std::vector<BendPoint>& points = curve.points();
    if (points.empty()) {
        return BendForm::None;
    }
    const bool down = std::ranges::any_of(points, [](const BendPoint& p) { return p.cents < 0; });
    const bool up = std::ranges::any_of(points, [](const BendPoint& p) { return p.cents > 0; });
    if (down && up) {
        return BendForm::Custom;
    }
    return bendForm(down ? mirrored(curve) : curve);
}

int whammyCents(const BendCurve& curve) {
    int farthest = 0;
    for (const BendPoint& point : curve.points()) {
        if (std::abs(point.cents) > std::abs(farthest)) {
            farthest = point.cents;
        }
    }
    return farthest;
}

BendCurve whammyCurve(BendForm form, int cents) {
    if (form == BendForm::None) {
        return {};
    }
    if (cents == 0 || std::abs(cents) > BendCurve::kMaxCents) {
        throw std::invalid_argument("whammyCurve: the bar must go 1 to 2400 cents down or up");
    }
    const BendCurve up = bendCurve(form, std::abs(cents));
    return cents < 0 ? mirrored(up) : up;
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
