// SPDX-License-Identifier: AGPL-3.0-or-later
// Where the playback cursor stands on laid-out pages.
#pragma once

#include <elara/notation/display_list.hpp>

#include <cstddef>
#include <optional>
#include <span>

namespace elara::notation {

/// A vertical line across a system's staves, in staff spaces on `page`.
struct Cursor {
    std::size_t page = 0;
    double x = 0.0;
    double y = 0.0;      ///< the top line of the system's first staff
    double height = 0.0; ///< down to the bottom line of its last staff
    friend bool operator==(const Cursor&, const Cursor&) = default;
};

/// The cursor `fraction` of the way through measure `measure` (0 at its start,
/// 1 at its end, clamped), on the first page that shows the measure; nothing
/// when no page shows it. It stands on each column of notes as it starts and
/// moves on to the next one (or to the barline) as time passes.
[[nodiscard]] std::optional<Cursor> cursorAt(std::span<const DisplayList> pages,
                                             std::size_t measure, double fraction) noexcept;

} // namespace elara::notation
