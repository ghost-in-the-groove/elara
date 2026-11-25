// SPDX-License-Identifier: AGPL-3.0-or-later
// The outlines of the music font's glyphs, read from its OpenType file, for
// renderers that draw glyphs as shapes instead of text: the PDF writer
// (ADR-0074).
#pragma once

#include <elara/notation/smufl.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace elara::notation {

/// One step of a glyph's outline, in staff spaces from the glyph's origin, y up
/// (as SMuFL measures).
struct OutlineStep {
    enum class Kind : std::uint8_t {
        Move,  ///< starts a contour at `to`
        Line,  ///< a straight line to `to`
        Curve, ///< a cubic Bézier curve to `to` through `control1` and `control2`
        Close, ///< closes the contour
    };
    Kind kind = Kind::Move;
    Point to{};
    Point control1{};
    Point control2{};
    friend bool operator==(const OutlineStep&, const OutlineStep&) = default;
};

/// The outlines of the glyphs Elara draws (`Glyph`), read once from an
/// OpenType font with CFF outlines, such as Bravura.otf: its `cmap` table maps
/// each glyph's code point to the font's glyph, whose Type 2 charstring gives
/// the outline. One em is four staff spaces.
class FontOutlines {
public:
    /// Reads the font at `path`; an error message when it cannot be read, has
    /// no CFF outlines or is a CID-keyed font.
    [[nodiscard]] static std::expected<FontOutlines, std::string>
    fromFile(const std::filesystem::path& path);

    /// The same from the bytes of the font file.
    [[nodiscard]] static std::expected<FontOutlines, std::string>
    fromBytes(std::span<const std::uint8_t> bytes);

    /// The closed contours of `glyph`; empty when the font has no glyph for its
    /// code point.
    [[nodiscard]] const std::vector<OutlineStep>& outline(Glyph glyph) const noexcept {
        return outlines_[static_cast<std::size_t>(glyph)];
    }

private:
    std::array<std::vector<OutlineStep>, kGlyphCount> outlines_{};
};

} // namespace elara::notation
