// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/duration.hpp>

#include <charconv>
#include <format>

namespace elara::core {

namespace {

constexpr std::string_view kBreve = "breve";
constexpr int kLastValue = static_cast<int>(NoteValue::HundredTwentyEighth);

// Parses a positive decimal integer without sign or leading zero at the front
// of `text`, advancing it. Returns 0 when there is none.
int takeCount(std::string_view& text) {
    if (text.empty() || text.front() < '1' || text.front() > '9') {
        return 0;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{}) {
        return 0;
    }
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    return value;
}

std::optional<NoteValue> takeValue(std::string_view& text) {
    if (text.starts_with(kBreve)) {
        text.remove_prefix(kBreve.size());
        return NoteValue::DoubleWhole;
    }
    const int denominator = takeCount(text);
    for (int index = 1; index <= kLastValue; ++index) {
        if (denominator == 1 << (index - 1)) {
            return static_cast<NoteValue>(index);
        }
    }
    return std::nullopt;
}

} // namespace

std::string Duration::toString() const {
    std::string text = value_ == NoteValue::DoubleWhole
                           ? std::string(kBreve)
                           : std::to_string(1 << (static_cast<int>(value_) - 1));
    text.append(dots_, '.');
    if (tuplet_ != Tuplet{}) {
        text += std::format("*{}:{}", tuplet_.actual(), tuplet_.normal());
    }
    return text;
}

std::optional<Duration> Duration::parse(std::string_view text) {
    const auto value = takeValue(text);
    if (!value) {
        return std::nullopt;
    }
    int dots = 0;
    while (!text.empty() && text.front() == '.') {
        ++dots;
        text.remove_prefix(1);
    }
    if (dots > kMaxDots) {
        return std::nullopt;
    }
    Tuplet tuplet;
    if (!text.empty() && text.front() == '*') {
        text.remove_prefix(1);
        const int actual = takeCount(text);
        if (text.empty() || text.front() != ':') {
            return std::nullopt;
        }
        text.remove_prefix(1);
        const int normal = takeCount(text);
        if (actual < 1 || normal < 1 || actual > Tuplet::kMaxCount || normal > Tuplet::kMaxCount) {
            return std::nullopt;
        }
        tuplet = Tuplet(actual, normal);
    }
    if (!text.empty()) {
        return std::nullopt;
    }
    return Duration(*value, dots, tuplet);
}

} // namespace elara::core
