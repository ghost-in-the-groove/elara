// SPDX-License-Identifier: AGPL-3.0-or-later
// What a laid-out score could not keep: ink that overlaps where it never may,
// and a measure wider than its system (ADR-0150).
#pragma once

#include <elara/notation/caret.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace elara::notation {

enum class LayoutIssueKind : std::uint8_t {
    Overlap,        ///< the ink of two items that must never touch overlaps
    MeasureTooWide, ///< a measure reaches past the right margin of its page
};

/// The kind's name in JSON: "overlap", "measureTooWide".
[[nodiscard]] std::string_view issueKindName(LayoutIssueKind kind) noexcept;

/// One thing the layout could not keep.
struct LayoutIssue {
    LayoutIssueKind kind{};
    std::size_t page = 0;    ///< index of the page in the span checked
    std::size_t measure = 0; ///< the measure it belongs to (of an overlap, the earlier)
    Role first{};            ///< an overlap's roles, in the order they stand from the left
    Role second{};
    Rect box; ///< where the ink overlaps, or the part of the system past the margin

    friend bool operator==(const LayoutIssue&, const LayoutIssue&) = default;
};

/// How far two inks may reach into each other, in staff spaces, before they
/// overlap: neighbours drawn to touch (the noteheads of a second, a stem
/// between them) go into each other by about a stem's thickness.
inline constexpr double kOverlapTolerance = 0.15;

/// Whether the ink of `a` and `b` must never overlap: noteheads, accidentals,
/// fret numbers, syllables of lyrics, chord names and dynamics, among
/// themselves, and noteheads with accidentals.
[[nodiscard]] bool mustNotTouch(Role a, Role b) noexcept;

/// The overlaps of `pages`: items of roles that must never touch whose ink goes
/// into each other by more than kOverlapTolerance in both directions. Items are
/// compared only within the system that holds them. A glyph drawn twice at one
/// place (a notehead two voices share) is one glyph. Glyphs are where the
/// font's metrics put their ink; texts where layout assumes they are, digits
/// from their baseline up to their cap height. In page order, then from left to
/// right in each system.
[[nodiscard]] std::vector<LayoutIssue> overlaps(std::span<const DisplayList> pages,
                                                const EngravingFont& font);

/// overlaps(), and the systems that reach past the right margin of pages laid
/// out with `options`: a measure wider than the page has a system of its own
/// that does not fit. Page by page, its measures too wide before its overlaps.
[[nodiscard]] std::vector<LayoutIssue> layoutIssues(std::span<const DisplayList> pages,
                                                    const EngravingFont& font,
                                                    const PageLayoutOptions& options);

} // namespace elara::notation
