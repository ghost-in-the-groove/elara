// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/pitch.hpp>

#include <charconv>
#include <cmath>
#include <format>
#include <system_error>

namespace elara::core {

namespace {

constexpr std::string_view kStepNames = "CDEFGAB";

std::optional<Step> stepFromLetter(char letter) {
    const char upper =
        letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
    const auto index = kStepNames.find(upper);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    return static_cast<Step>(index);
}

// Consumes the accidentals at the start of `rest`; nullopt when they are mixed or
// more than a double alteration.
std::optional<int> takeAlter(std::string_view& rest) {
    if (rest.starts_with("##") || rest.starts_with('x')) {
        rest.remove_prefix(rest.starts_with('x') ? 1 : 2);
        return 2;
    }
    if (rest.starts_with("bb")) {
        rest.remove_prefix(2);
        return -2;
    }
    if (rest.starts_with('#')) {
        rest.remove_prefix(1);
        return 1;
    }
    if (rest.starts_with('b')) {
        rest.remove_prefix(1);
        return -1;
    }
    return 0;
}

// The whole of `rest` must be an octave number: optional '-', no leading zeros,
// at most two digits (MIDI ends at octave 9; this also keeps midiFor in range).
std::optional<int> takeOctave(std::string_view rest) {
    const bool negative = rest.starts_with('-');
    const std::string_view digits = negative ? rest.substr(1) : rest;
    if (digits.empty() || digits.size() > 2 || digits.front() < '0' || digits.front() > '9' ||
        (digits.size() > 1 && digits.front() == '0') || (negative && digits == "0")) {
        return std::nullopt;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

} // namespace

std::string Pitch::toString() const {
    std::string_view accidentals;
    switch (alter_) {
    case -2:
        accidentals = "bb";
        break;
    case -1:
        accidentals = "b";
        break;
    case 1:
        accidentals = "#";
        break;
    case 2:
        accidentals = "##";
        break;
    default:
        break;
    }
    return std::format("{}{}{}", kStepNames[static_cast<std::size_t>(step_)], accidentals,
                       static_cast<int>(octave_));
}

double Pitch::frequency(double a4Hz) const noexcept {
    constexpr int kA4 = 69;
    return a4Hz * std::exp2(static_cast<double>(midiNumber() - kA4) / 12.0);
}

std::optional<Pitch> Pitch::parse(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const auto step = stepFromLetter(text.front());
    std::string_view rest = text.substr(1);
    const auto alter = takeAlter(rest);
    const auto octave = takeOctave(rest);
    if (!step || !alter || !octave) {
        return std::nullopt;
    }
    if (const int midi = midiFor(*step, *alter, *octave); midi < 0 || midi > 127) {
        return std::nullopt;
    }
    return Pitch(*step, *alter, *octave);
}

} // namespace elara::core
