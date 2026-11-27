// SPDX-License-Identifier: AGPL-3.0-or-later
// What a writer asks of the layout for a measure (ADR-0152): a break before it,
// in the scope of the view that shows it.
#pragma once

#include <elara/core/names.hpp>
#include <elara/core/shared_value.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

/// Where a system or a page starts.
enum class Break : std::uint8_t {
    Auto,   ///< where the line breaker puts it
    System, ///< a system starts at this measure
    Page,   ///< a page (and so a system) starts at this measure
    Keep,   ///< no system starts at this measure while it fits beside the one before
};
template <>
struct EnumNames<Break> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"auto", "system", "page", "keep"});
};
static_assert(namesEnd(Break::Keep));

/// The layout adjustments of one measure in one scope. Two bytes: a scope that
/// holds them holds one per measure.
struct MeasureLayout {
    static constexpr std::uint8_t kDefaultSpacing = 100;

    Break breakBefore = Break::Auto; ///< of the first measure: means nothing
    /// Percent of the measure's ideal spaces, 50 to 200 (read by layout later).
    std::uint8_t spacing = kDefaultSpacing;

    [[nodiscard]] bool isDefault() const noexcept { return *this == MeasureLayout{}; }

    friend bool operator==(const MeasureLayout&, const MeasureLayout&) = default;
};
static_assert(sizeof(MeasureLayout) == 2);

/// The measure layouts of a scope: none (a null pointer) until the first
/// adjustment, then one per measure, shared by the song's copies and replaced
/// as a whole.
using MeasureLayouts = SharedOptional<std::vector<MeasureLayout>>;

/// Whose measure layouts are meant: a track's own, used when staves of that
/// track only are shown, or the song's, used when staves of several are.
struct LayoutScope {
    std::optional<std::size_t> track; ///< none: the song's

    [[nodiscard]] static LayoutScope song() noexcept { return {}; }
    [[nodiscard]] static LayoutScope ofTrack(std::size_t index) noexcept {
        return {.track = index};
    }

    friend bool operator==(const LayoutScope&, const LayoutScope&) = default;
};

} // namespace elara::core
