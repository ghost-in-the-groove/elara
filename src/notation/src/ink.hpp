// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: where the glyphs and texts of a display list leave ink, as layout
// assumes it. Layout makes room for texts by kTextAscent and kTextDescent; the
// layout issues and the symbol under the pointer use the rectangles below, and
// the SVG and PDF writers place texts by textLeft().
#pragma once

#include <elara/notation/caret.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <algorithm>
#include <string_view>

namespace elara::notation::detail {

// Text extents as layout assumes them: ascenders (and parentheses) reach
// 0.75 em above the baseline, descenders 0.21 em below.
inline constexpr double kTextAscent = 0.75;
inline constexpr double kTextDescent = 0.21;

/// The left edge of a text drawn at its origin with its alignment.
[[nodiscard]] inline double textLeft(const TextItem& text) noexcept {
    const double width = textWidth(text);
    switch (text.align) {
    case TextAlign::Left:
        return text.origin.x;
    case TextAlign::Right:
        return text.origin.x - width;
    case TextAlign::Centre:
        break;
    }
    return text.origin.x - (width / 2);
}

/// The rectangle round a glyph's ink: the font's box, whose y goes up from the
/// glyph's origin, on the page, whose y goes down.
[[nodiscard]] inline Rect glyphInk(const GlyphItem& glyph, const EngravingFont& font) {
    const BoundingBox& box = font.metrics(glyph.glyph).box;
    return {.x = glyph.origin.x + (box.southWest.x * glyph.scale),
            .y = glyph.origin.y - (box.northEast.y * glyph.scale),
            .width = box.width() * glyph.scale,
            .height = box.height() * glyph.scale};
}

/// Whether `text` stands on its baseline and reaches no higher than its cap
/// height: the digits and letters of a fret number and the angle brackets of a
/// natural harmonic. Other texts have ascenders and descenders.
[[nodiscard]] inline bool capsOnly(std::string_view text) noexcept {
    return std::ranges::all_of(
        text, [](char c) { return (c >= '0' && c <= '9') || c == 'X' || c == '<' || c == '>'; });
}

/// The rectangle round a text's ink, textWidth() wide: from its baseline up to
/// its cap height when it is capsOnly(), else from kTextAscent above the
/// baseline to kTextDescent below.
[[nodiscard]] inline Rect textInk(const TextItem& text) {
    const double left = textLeft(text);
    const double width = textWidth(text);
    if (capsOnly(text.text)) {
        const double height = capHeight(text.size);
        return {.x = left, .y = text.origin.y - height, .width = width, .height = height};
    }
    return {.x = left,
            .y = text.origin.y - (kTextAscent * text.size),
            .width = width,
            .height = (kTextAscent + kTextDescent) * text.size};
}

} // namespace elara::notation::detail
