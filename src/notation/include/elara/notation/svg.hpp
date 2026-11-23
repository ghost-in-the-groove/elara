// SPDX-License-Identifier: AGPL-3.0-or-later
// A display list written as SVG, for snapshot tests and quick looks (ADR-0012).
#pragma once

#include <elara/notation/display_list.hpp>

#include <string>

namespace elara::notation {

struct SvgOptions {
    double staffSpace = 10.0; ///< pixels per staff space
    double margin = 2.0;      ///< staff spaces of white around the ink
    std::string fontFamily = "Bravura";
    std::string fontUrl; ///< when set, an @font-face rule loads the font from it
};

/// The display list as a standalone SVG document. Glyphs are text in the music
/// font (one em is four staff spaces); lines and beams are shapes. Every element
/// carries its role as a class, so a style sheet can colour noteheads or stems.
[[nodiscard]] std::string toSvg(const DisplayList& list, const SvgOptions& options = {});

} // namespace elara::notation
