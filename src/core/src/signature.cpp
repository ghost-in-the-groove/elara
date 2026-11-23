// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/signature.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <system_error>

namespace elara::core {

namespace {

constexpr std::array<int, 7> kNaturalSemitones{0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
constexpr std::string_view kStepNames = "CDEFGAB";

// Steps in circle-of-fifths order; F is -1 fifths, C is 0, ... B is 5.
constexpr std::array<Step, 7> kFifthsOrder{Step::F, Step::C, Step::G, Step::D,
                                           Step::A, Step::E, Step::B};

constexpr int floorDiv(int a, int b) {
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}

constexpr int semitoneOf(Step step) {
    return kNaturalSemitones[static_cast<std::size_t>(step)];
}

constexpr int positiveModulo(int value, int modulus) {
    return ((value % modulus) + modulus) % modulus;
}

// Fifths of a step (F = -1 ... B = 5).
constexpr int fifthsOfStep(Step step) {
    for (std::size_t i = 0; i < kFifthsOrder.size(); ++i) {
        if (kFifthsOrder[i] == step) {
            return static_cast<int>(i) - 1;
        }
    }
    return 0;
}

std::string_view trim(std::string_view text) {
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t';
    };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

// The tonic of the key, as a circle-of-fifths value (major: fifths; minor: +3).
struct Tonic {
    Step step;
    int alter;
};

Tonic tonicFor(int fifths, KeyMode mode) {
    const int value = fifths + (mode == KeyMode::Minor ? 3 : 0);
    const int index = value + 1; // F at 0
    const int alter = floorDiv(index, 7);
    return {kFifthsOrder[static_cast<std::size_t>(index - 7 * alter)], alter};
}

Pitch spelled(int midi, Step step, int alter) {
    const int octave = (midi - semitoneOf(step) - alter) / 12 - 1;
    return Pitch(step, alter, octave);
}

} // namespace

// ---- TimeSignature ----------------------------------------------------------

std::string TimeSignature::toString() const {
    return std::format("{}/{}", static_cast<int>(numerator_), static_cast<int>(denominator_));
}

std::optional<TimeSignature> TimeSignature::parse(std::string_view text) {
    const auto slash = text.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const auto readInt = [](std::string_view digits) -> std::optional<int> {
        int value = 0;
        const auto* end = digits.data() + digits.size();
        const auto [ptr, error] = std::from_chars(digits.data(), end, value);
        if (digits.empty() || error != std::errc{} || ptr != end) {
            return std::nullopt;
        }
        return value;
    };
    const auto numerator = readInt(text.substr(0, slash));
    const auto denominator = readInt(text.substr(slash + 1));
    if (!numerator || !denominator) {
        return std::nullopt;
    }
    try {
        return TimeSignature(*numerator, *denominator);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

// ---- KeySignature -------------------------------------------------------------

Step KeySignature::tonic() const noexcept {
    return tonicFor(fifths_, mode_).step;
}

int KeySignature::tonicAlter() const noexcept {
    return tonicFor(fifths_, mode_).alter;
}

int KeySignature::alterFor(Step step) const noexcept {
    // Sharps are added in fifths order from F, flats in reverse order from B.
    const int position = fifthsOfStep(step) + 1; // F = 0 ... B = 6
    if (fifths_ > 0) {
        return position < fifths_ ? 1 : 0;
    }
    if (fifths_ < 0) {
        return (6 - position) < -fifths_ ? -1 : 0;
    }
    return 0;
}

std::vector<Step> KeySignature::alteredSteps() const {
    std::vector<Step> steps;
    for (int i = 0; i < fifths_; ++i) {
        steps.push_back(kFifthsOrder[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < -fifths_; ++i) {
        steps.push_back(kFifthsOrder[kFifthsOrder.size() - 1 - static_cast<std::size_t>(i)]);
    }
    return steps;
}

Pitch KeySignature::spell(int midiNumber) const {
    if (midiNumber < 0 || midiNumber > 127) {
        throw std::invalid_argument("KeySignature: outside the MIDI range");
    }
    const int pitchClass = midiNumber % 12;
    const auto matches = [pitchClass](Step step, int alter) {
        return positiveModulo(semitoneOf(step) + alter, 12) == pitchClass;
    };
    constexpr std::array kSteps{Step::C, Step::D, Step::E, Step::F, Step::G, Step::A, Step::B};

    for (const Step step : kSteps) { // 1. diatonic
        if (matches(step, alterFor(step))) {
            return spelled(midiNumber, step, alterFor(step));
        }
    }
    if (mode_ == KeyMode::Minor) { // 2. raised leading tone
        const auto leading =
            static_cast<Step>((static_cast<int>(tonic()) + 6) % static_cast<int>(kSteps.size()));
        if (const int alter = alterFor(leading) + 1; matches(leading, alter)) {
            return spelled(midiNumber, leading, alter);
        }
    }
    for (const Step step : kSteps) { // 3. natural
        if (matches(step, 0)) {
            return spelled(midiNumber, step, 0);
        }
    }
    const int alter = fifths_ >= 0 ? 1 : -1; // 4. sharp or flat by key direction
    for (const Step step : kSteps) {
        if (matches(step, alter)) {
            return spelled(midiNumber, step, alter);
        }
    }
    throw std::logic_error("KeySignature: no spelling found"); // unreachable
}

std::string KeySignature::toString() const {
    const auto [step, alter] = tonicFor(fifths_, mode_);
    const std::string_view accidental = alter > 0 ? "#" : alter < 0 ? "b" : "";
    return std::format("{}{} {}", kStepNames[static_cast<std::size_t>(step)], accidental,
                       mode_ == KeyMode::Major ? "major" : "minor");
}

std::optional<KeySignature> KeySignature::parse(std::string_view text) {
    text = trim(text);
    const auto space = text.find_first_of(" \t");
    const std::string_view tonicText = text.substr(0, space);
    const std::string_view modeText =
        space == std::string_view::npos ? std::string_view{} : trim(text.substr(space));

    KeyMode mode = KeyMode::Major;
    if (equalsIgnoreCase(modeText, "minor")) {
        mode = KeyMode::Minor;
    } else if (!modeText.empty() && !equalsIgnoreCase(modeText, "major")) {
        return std::nullopt;
    }

    if (tonicText.empty() || tonicText.size() > 2) {
        return std::nullopt;
    }
    const char letter = tonicText.front();
    const char upper =
        letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
    const auto stepIndex = kStepNames.find(upper);
    if (stepIndex == std::string_view::npos) {
        return std::nullopt;
    }
    int alter = 0;
    if (tonicText.size() == 2) {
        if (tonicText[1] == '#') {
            alter = 1;
        } else if (tonicText[1] == 'b') {
            alter = -1;
        } else {
            return std::nullopt;
        }
    }
    const int fifths =
        fifthsOfStep(static_cast<Step>(stepIndex)) + 7 * alter - (mode == KeyMode::Minor ? 3 : 0);
    if (fifths < -kMaxFifths || fifths > kMaxFifths) {
        return std::nullopt;
    }
    return KeySignature(fifths, mode);
}

} // namespace elara::core
