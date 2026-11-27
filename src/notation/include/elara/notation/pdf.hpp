// SPDX-License-Identifier: AGPL-3.0-or-later
// Display lists written as a PDF document, one page each (ADR-0074).
#pragma once

#include <elara/notation/display_list.hpp>
#include <elara/notation/font_outlines.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace elara::notation {

struct PdfOptions {
    double staffSpace = 1.75 * 72.0 / 25.4; ///< points per staff space (1.75 mm)
    double margin = 0.0;                    ///< staff spaces of white around each page
    std::string title{};                    ///< the document's title, in its properties
    /// Compresses a stream into the zlib format (RFC 1950), for the FlateDecode
    /// filter; without it the streams are written as they are. Called from
    /// several threads at once when there are several pages.
    std::function<std::string(std::string_view)> compress{};
    /// Threads that write (and compress) the pages: 0 for every core when
    /// there are several pages, 1 for the calling thread only.
    unsigned threads = 0;
};

/// The display lists as a PDF document, one page each: a page shows its list
/// from x = 0 to its width and from its top to its bottom, `margin` around,
/// at `staffSpace` points per staff space. Glyphs are shapes drawn from
/// `font`'s outlines, each written once and placed where it is used; texts
/// are in the PDF's standard Helvetica (Helvetica-Oblique for italics,
/// Helvetica-Bold for bold), in the Windows Latin-1 encoding ("?" for other
/// characters).
[[nodiscard]] std::string toPdf(std::span<const DisplayList> pages, const FontOutlines& font,
                                const PdfOptions& options = {});

} // namespace elara::notation
