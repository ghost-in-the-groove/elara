// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/tuning.hpp>

#include <algorithm>

namespace elara::core {

namespace {

struct Preset {
    std::string_view name;
    std::string_view strings; // Tuning::toString() form
};

constexpr std::array kPresets{
    Preset{"guitar", "E2 A2 D3 G3 B3 E4"},
    Preset{"guitar-drop-d", "D2 A2 D3 G3 B3 E4"},
    Preset{"guitar-half-step-down", "Eb2 Ab2 Db3 Gb3 Bb3 Eb4"},
    Preset{"guitar-dadgad", "D2 A2 D3 G3 A3 D4"},
    Preset{"guitar-open-g", "D2 G2 D3 G3 B3 D4"},
    Preset{"guitar-open-d", "D2 A2 D3 F#3 A3 D4"},
    Preset{"guitar-7", "B1 E2 A2 D3 G3 B3 E4"},
    Preset{"guitar-8", "F#1 B1 E2 A2 D3 G3 B3 E4"},
    Preset{"bass", "E1 A1 D2 G2"},
    Preset{"bass-drop-d", "D1 A1 D2 G2"},
    Preset{"bass-5", "B0 E1 A1 D2 G2"},
    Preset{"bass-6", "B0 E1 A1 D2 G2 C3"},
    Preset{"ukulele", "G4 C4 E4 A4"},
    Preset{"mandolin", "G3 D4 A4 E5"},
    Preset{"banjo", "G4 D3 G3 B3 D4"},
};

constexpr auto kPresetNames = [] {
    std::array<std::string_view, kPresets.size()> names{};
    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        names[i] = kPresets[i].name;
    }
    return names;
}();

constexpr bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

} // namespace

std::vector<FretPosition> Tuning::positionsFor(const Pitch& pitch, int capo, int maxFret) const {
    std::vector<FretPosition> positions;
    const int highestFret = std::min(maxFret, kMaxFret - capo);
    for (int string = 1; string <= count_; ++string) {
        const int fret = pitch.midiNumber() - openString(string).midiNumber() - capo;
        if (fret >= 0 && fret <= highestFret) {
            positions.push_back({.string = string, .fret = fret});
        }
    }
    return positions;
}

std::string Tuning::toString() const {
    std::string text;
    for (int string = count_; string >= 1; --string) {
        if (!text.empty()) {
            text += ' ';
        }
        text += openString(string).toString();
    }
    return text;
}

std::optional<Tuning> Tuning::parse(std::string_view text) {
    std::array<Pitch, kMaxStrings> lowestFirst{};
    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && isSpace(text[pos])) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && !isSpace(text[pos])) {
            ++pos;
        }
        if (start == pos) {
            break;
        }
        const auto pitch = Pitch::parse(text.substr(start, pos - start));
        if (!pitch || count == lowestFirst.size()) {
            return std::nullopt;
        }
        lowestFirst[count++] = *pitch;
    }
    if (count == 0) {
        return std::nullopt;
    }
    std::reverse(lowestFirst.begin(), lowestFirst.begin() + static_cast<std::ptrdiff_t>(count));
    return Tuning(std::span<const Pitch>(lowestFirst.data(), count));
}

std::optional<Tuning> Tuning::preset(std::string_view name) {
    const auto found = std::ranges::find(kPresets, name, &Preset::name);
    if (found == kPresets.end()) {
        return std::nullopt;
    }
    return parse(found->strings);
}

std::span<const std::string_view> Tuning::presetNames() noexcept {
    return kPresetNames;
}

} // namespace elara::core
