// SPDX-License-Identifier: AGPL-3.0-or-later
// What layout produces and renderers draw: glyphs, lines and beams at positions
// (ADR-0012).
#pragma once

#include <elara/notation/smufl.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace elara::notation {

/// What an item shows, so renderers can style it and editors can pick it.
enum class Role : std::uint8_t {
    StaffLine,
    LedgerLine,
    Barline,
    Clef,
    KeySignature,
    TimeSignature,
    Notehead,
    Accidental,
    Dot,
    Stem,
    Flag,
    Beam,
    Rest,
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::Rest) + 1;

/// The role's name in JSON and SVG output ("staffLine", "notehead").
[[nodiscard]] std::string_view roleName(Role role) noexcept;

/// A glyph drawn with its SMuFL origin at `origin`.
struct GlyphItem {
    Glyph glyph{};
    Point origin;
    Role role{};
    std::size_t measure = 0; ///< index of the measure it belongs to in the song
    friend bool operator==(const GlyphItem&, const GlyphItem&) = default;
};

/// A straight line with square-cut ends, `thickness` wide across its length.
struct LineItem {
    Point from;
    Point to;
    double thickness = 0.0;
    Role role{};
    std::size_t measure = 0;
    friend bool operator==(const LineItem&, const LineItem&) = default;
};

/// A beam: a parallelogram with vertical ends at left.x and right.x, `thickness`
/// high, centred on the line from `left` to `right`.
struct BeamItem {
    Point left;
    Point right;
    double thickness = 0.0;
    std::size_t measure = 0;
    friend bool operator==(const BeamItem&, const BeamItem&) = default;
};

/// Where a measure sits: from the end of the barline before it to the start
/// of the barline after it.
struct MeasureBox {
    std::size_t index = 0;
    double x = 0.0;
    double width = 0.0;
    friend bool operator==(const MeasureBox&, const MeasureBox&) = default;
};

/// Positioned glyphs, lines and beams. Lengths are in staff spaces and y grows
/// downwards: the top staff line is at y = 0 and the bottom one at y = 4.
struct DisplayList {
    double width = 0.0;  ///< from x = 0 to the end of the last barline
    double top = 0.0;    ///< highest ink, negative above the staff
    double bottom = 4.0; ///< lowest ink
    std::vector<MeasureBox> measures;
    std::vector<GlyphItem> glyphs;
    std::vector<LineItem> lines;
    std::vector<BeamItem> beams;
};

} // namespace elara::notation
