// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

namespace {

template <class T>
std::size_t heapBytes(const std::vector<T>& values) noexcept {
    return values.capacity() * sizeof(T);
}

// A string short enough to live in the object holds nothing more: an empty
// string's capacity is that room on every standard library.
std::size_t heapBytes(const std::string& text) noexcept {
    return text.capacity() > std::string().capacity() ? text.capacity() + 1 : 0;
}

// A shared value's object and its control block (two counts and two pointers).
template <class T>
std::size_t sharedBytes() noexcept {
    return sizeof(T) + (2 * sizeof(long)) + (2 * sizeof(void*));
}

std::size_t heapBytes(const Beat& beat) noexcept {
    std::size_t bytes = heapBytes(beat.notes) + heapBytes(beat.graces);
    for (const Grace& grace : beat.graces) {
        bytes += heapBytes(grace.notes);
    }
    if (beat.chord) {
        bytes += sharedBytes<ChordSymbol>() + heapBytes(beat.chord->name);
        if (beat.chord->diagram) {
            bytes += heapBytes(beat.chord->diagram->frets);
        }
    }
    if (beat.text) {
        bytes += sharedBytes<std::string>() + heapBytes(*beat.text);
    }
    if (beat.lyrics) {
        bytes += sharedBytes<std::vector<Syllable>>() + heapBytes(*beat.lyrics);
        for (const Syllable& syllable : *beat.lyrics) {
            bytes += heapBytes(syllable.text);
        }
    }
    return bytes;
}

} // namespace

std::size_t approximateBytes(const Measure& measure) noexcept {
    std::size_t bytes = sizeof(Measure);
    for (const Voice& voice : measure.voices) {
        bytes += heapBytes(voice.beats);
        for (const Beat& beat : voice.beats) {
            bytes += heapBytes(beat);
        }
    }
    return bytes;
}

bool Measure::isEmpty() const noexcept {
    return std::ranges::all_of(voices, &Voice::isEmpty);
}

} // namespace elara::core
