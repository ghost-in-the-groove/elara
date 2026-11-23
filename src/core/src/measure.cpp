// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>

#include <algorithm>
#include <stdexcept>

namespace elara::core {

Fraction Voice::length() const {
    Fraction total;
    for (const auto& beat : beats) {
        total += beat.duration.length();
    }
    return total;
}

Fraction Voice::startOf(std::size_t index) const {
    if (index >= beats.size()) {
        throw std::out_of_range("Voice: no such beat");
    }
    Fraction start;
    for (std::size_t i = 0; i < index; ++i) {
        start += beats[i].duration.length();
    }
    return start;
}

bool Measure::isEmpty() const noexcept {
    return std::ranges::all_of(voices, &Voice::isEmpty);
}

} // namespace elara::core
