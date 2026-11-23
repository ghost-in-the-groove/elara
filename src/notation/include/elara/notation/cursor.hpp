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

/// A time in a written measure, `fraction` of the way through it.
struct MeasurePosition {
    std::size_t measure = 0;
    double fraction = 0.0;
    friend bool operator==(const MeasurePosition&, const MeasurePosition&) = default;
};

/// Where playback would start for a click at (`x`, `y`) on `page`, in staff
/// spaces: in the measure that spans `x` and lies nearest `y` (within a few
/// staff spaces of its staves), the last column of notes that starts left of
/// `x` or a notehead's width right of it, or the start of the measure before
/// its first column; nothing away from every measure.
[[nodiscard]] std::optional<MeasurePosition> measurePositionAt(const DisplayList& page, double x,
                                                               double y) noexcept;

} // namespace elara::notation
