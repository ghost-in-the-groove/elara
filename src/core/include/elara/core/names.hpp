// SPDX-License-Identifier: AGPL-3.0-or-later
// The text names of the model's enumerations: one word per value, written and
// read by the native file format, the command line and its reports.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace elara::core {

/// The text names of an enumeration of the model, one per value from 0 up, in
/// kebab-case ("none", "from-below", "8va"). Specialised right after each
/// enumeration that has names, as `static constexpr std::array kNames`, with
/// `static_assert(namesEnd(LastValue))` under it.
template <typename Enum>
struct EnumNames;

/// An enumeration with text names: its values run from 0 to kEnumCount - 1.
template <typename Enum>
concept NamedEnum = std::is_enum_v<Enum> && requires {
    { EnumNames<Enum>::kNames.size() } -> std::convertible_to<std::size_t>;
};

/// How many values a named enumeration has.
template <NamedEnum Enum>
inline constexpr std::size_t kEnumCount = EnumNames<Enum>::kNames.size();

/// The name of `value`; "" for a number the enumeration has no value for.
template <NamedEnum Enum>
[[nodiscard]] constexpr std::string_view nameOf(Enum value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < kEnumCount<Enum> ? EnumNames<Enum>::kNames[index] : std::string_view{};
}

/// The value `name` names, as nameOf() writes it; nullopt for any other text.
template <NamedEnum Enum>
[[nodiscard]] constexpr std::optional<Enum> parseName(std::string_view name) noexcept {
    const auto& names = EnumNames<Enum>::kNames;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

/// The value numbered `index` (its underlying value), as a control of the app
/// or another numbered source gives it; nullopt when the enumeration has none.
template <NamedEnum Enum, std::integral Index>
[[nodiscard]] constexpr std::optional<Enum> enumFromIndex(Index index) noexcept {
    if (std::cmp_less(index, 0) || std::cmp_greater_equal(index, kEnumCount<Enum>)) {
        return std::nullopt;
    }
    return static_cast<Enum>(index);
}

/// Every value of a named enumeration, in order.
template <NamedEnum Enum>
[[nodiscard]] constexpr std::array<Enum, kEnumCount<Enum>> enumValues() noexcept {
    std::array<Enum, kEnumCount<Enum>> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<Enum>(i);
    }
    return values;
}

/// Whether the names of `Enum` name its values up to `last` and no more, each
/// with a word of its own: the check under each specialisation.
template <NamedEnum Enum>
[[nodiscard]] consteval bool namesEnd(Enum last) {
    const auto& names = EnumNames<Enum>::kNames;
    if (names.size() != static_cast<std::size_t>(last) + 1) {
        return false;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (names[j] == names[i]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace elara::core
