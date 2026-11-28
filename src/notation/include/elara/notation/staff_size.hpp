// SPDX-License-Identifier: AGPL-3.0-or-later
// The staff size that lets the staves shown fit a page (ADR-0171), and its
// widest measure fit the page's width (ADR-0176).
#pragma once

#include <elara/core/song.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/staff_layout.hpp>

#include <span>

namespace elara::notation {

/// The least share of the staff space asked for that fittedStaffSpace() gives
/// while it makes room for two systems on a page.
inline constexpr double kLeastStaffShare = 0.6;
/// The least staff space fittedStaffSpace() gives, in millimetres.
inline constexpr double kLeastStaffSpace = 0.5;

/// How high a system of `staves` stands, in staff spaces: the height of each
/// staff (four spaces for notation, 1.5 between the string lines of
/// tablature), a usual distance between neighbouring staves and room for the
/// ink above and below. It depends only on which staves are shown, so that
/// editing the music never changes it. Throws std::out_of_range for a missing
/// track or staff.
[[nodiscard]] double typicalSystemHeight(const core::Song& song,
                                         std::span<const StaffSelection> staves);

/// The staff space, in millimetres, to lay `staves` out with on `paper` with
/// margins of `margin` mm, so that the score shows more of itself as more
/// staves are shown at once (ADR-0171): `staffSpace` while two systems of
/// typicalSystemHeight() fit between the margins; smaller as the systems grow,
/// so that two still fit, down to kLeastStaffShare of `staffSpace`; below
/// that only as far as one system a page needs, and never under
/// kLeastStaffSpace. Rounded down to a hundredth of a millimetre. Throws like
/// typicalSystemHeight(), and std::invalid_argument when the margins leave no
/// room or `staffSpace` is not positive.
[[nodiscard]] double fittedStaffSpace(const core::Song& song,
                                      std::span<const StaffSelection> staves, const Paper& paper,
                                      double staffSpace = 1.75, double margin = 15.0);

/// The system of `pages` that runs furthest past the right margin of
/// `options`, the pages' own: a measure wider than the page between its
/// margins gets a system of its own that does (ADR-0150).
struct WidthOverflow {
    double amount = 0.0;     ///< staff spaces past the margin; 0 when no system runs past it
    std::size_t measure = 0; ///< the first measure of that system
};
[[nodiscard]] WidthOverflow widthOverflow(std::span<const DisplayList> pages,
                                          const PageLayoutOptions& options);

/// The staff space, in millimetres, at which a system fits `paper` between
/// margins of `margin` mm when it ran `overflow` staff spaces past the right
/// margin of pages laid out with staff spaces `laidOut` mm apart (ADR-0176):
/// the page is as many millimetres wide, so it is wider in smaller staff
/// spaces. Rounded down to a hundredth of a millimetre; never below
/// kLeastStaffShare of `asked`, the staff space of a part, nor below
/// kLeastStaffSpace, where a system still too wide stays so. `laidOut` when
/// `overflow` is not positive or `laidOut` is at that least already. Throws
/// std::invalid_argument when the margins leave no room or a staff space is
/// not positive.
[[nodiscard]] double narrowedStaffSpace(const Paper& paper, double asked, double laidOut,
                                        double overflow, double margin = 15.0);

} // namespace elara::notation
