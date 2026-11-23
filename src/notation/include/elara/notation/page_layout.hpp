// SPDX-License-Identifier: AGPL-3.0-or-later
// Measures broken into systems and systems stacked into pages (ADR-0014).
#pragma once

#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace elara::notation {

/// Page geometry and line breaking. Lengths are in staff spaces.
struct PageLayoutOptions {
    std::size_t firstMeasure = 0;
    std::size_t measureCount = kAllMeasures; ///< clipped to the end of the song
    double width = 120.0;                    ///< of the page: A4 with staff spaces 1.75 mm apart
    double height = 169.7;                   ///< of the page; 0 gives one page as long as the music
    double margin = 8.6;                     ///< on every side of the page
    /// Measures in every system; 0 picks the breaks that space the systems most
    /// evenly.
    std::size_t measuresPerSystem = 0;
    /// The last system is stretched to the full width only when its natural
    /// width is at least this share of it.
    double lastSystemFill = 0.6;
    bool credits = true;  ///< title and credits on the first page, page numbers on the others
    unsigned threads = 0; ///< 0 lays out on every core when the song is long enough
};

/// Paper sizes in millimetres.
struct Paper {
    double width = 210.0;
    double height = 297.0;
};
inline constexpr Paper kA4{.width = 210.0, .height = 297.0};
inline constexpr Paper kLetter{.width = 215.9, .height = 279.4};

/// Page options for `paper` with staff spaces `staffSpace` mm apart and margins
/// of `margin` mm.
[[nodiscard]] PageLayoutOptions pageOptions(const Paper& paper, double staffSpace = 1.75,
                                            double margin = 15.0);

/// Lays out the selected staves on pages: the measures are broken into systems
/// as wide as the page between its margins and stacked from the top margin to
/// the bottom one. Notes keep the spacing of layoutSystem(), stretched or
/// tightened (never below what their ink needs) so that every system but a
/// short last one fills the width. Each system starts with clef and key
/// signature (and the time signature where it changes) and, after the first,
/// the number of its first measure. Ties that cross from one system to the next
/// are drawn in two halves.
///
/// Each page's display list has its origin at the page's top-left corner, its
/// width and bottom are the page's, and it holds a SystemBox per system. A page
/// starts with as many systems as fit; a system taller than a page gets a page
/// of its own.
///
/// Throws like layoutSystem(), and std::invalid_argument when the margins leave
/// no room.
[[nodiscard]] std::vector<DisplayList> layoutPages(const core::Song& song,
                                                   std::span<const StaffSelection> staves,
                                                   const EngravingFont& font,
                                                   const PageLayoutOptions& options = {});

} // namespace elara::notation
