// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace elara::core {

ChordDiagram ChordDiagram::ofFrets(std::vector<std::int8_t> frets) {
    constexpr int kFretsFromNut = 4; // a diagram shows four frets at least
    int lowest = Tuning::kMaxFret;
    int highest = 0;
    for (const std::int8_t fret : frets) {
        if (fret > 0) {
            lowest = std::min<int>(lowest, fret);
            highest = std::max<int>(highest, fret);
        }
    }
    return {.firstFret = static_cast<std::uint8_t>(highest > kFretsFromNut ? lowest : 1),
            .frets = std::move(frets)};
}

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
