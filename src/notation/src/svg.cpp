// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/smufl.hpp>
#include <elara/notation/svg.hpp>

#include <cstdint>
#include <format>
#include <string>

namespace elara::notation {

namespace {

// At most three decimals, no trailing zeros, no "-0".
std::string number(double value) {
    std::string text = std::format("{:.3f}", value);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string escapeAttribute(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '<':
            out += "&lt;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

} // namespace

std::string toSvg(const DisplayList& list, const SvgOptions& options) {
    const double x = -options.margin;
    const double y = list.top - options.margin;
    const double width = list.width + 2 * options.margin;
    const double height = list.bottom - list.top + 2 * options.margin;
    std::string svg = std::format(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="{} {} {} {}">)"
        "\n",
        number(width * options.staffSpace), number(height * options.staffSpace), number(x),
        number(y), number(width), number(height));
    const std::string family = escapeAttribute(options.fontFamily);
    svg += "<style>";
    if (!options.fontUrl.empty()) {
        svg += std::format(R"(@font-face{{font-family:"{}";src:url("{}")}})", family,
                           escapeAttribute(options.fontUrl));
    }
    svg += std::format(R"(text{{font-family:"{}";font-size:4px}})", family);
    svg += "</style>\n";
    svg += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="white"/>)"
                       "\n",
                       number(x), number(y), number(width), number(height));
    svg += "<g fill=\"black\" stroke=\"black\">\n";
    for (const LineItem& line : list.lines) {
        svg += std::format(R"(<line class="{}" x1="{}" y1="{}" x2="{}" y2="{}" stroke-width="{}"/>)"
                           "\n",
                           roleName(line.role), number(line.from.x), number(line.from.y),
                           number(line.to.x), number(line.to.y), number(line.thickness));
    }
    for (const BeamItem& beam : list.beams) {
        const double half = beam.thickness / 2;
        svg += std::format(
            R"(<polygon class="beam" stroke="none" points="{},{} {},{} {},{} {},{}"/>)"
            "\n",
            number(beam.left.x), number(beam.left.y - half), number(beam.right.x),
            number(beam.right.y - half), number(beam.right.x), number(beam.right.y + half),
            number(beam.left.x), number(beam.left.y + half));
    }
    for (const GlyphItem& glyph : list.glyphs) {
        svg += std::format(R"(<text class="{}" x="{}" y="{}" stroke="none">&#x{:X};</text>)"
                           "\n",
                           roleName(glyph.role), number(glyph.origin.x), number(glyph.origin.y),
                           static_cast<std::uint32_t>(codepoint(glyph.glyph)));
    }
    svg += "</g>\n</svg>\n";
    return svg;
}

} // namespace elara::notation
