// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/smufl.hpp>
#include <elara/notation/svg.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

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

std::string escapeAttribute(std::string_view text) {
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
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string point(const Point& p) {
    return number(p.x) + "," + number(p.y);
}

std::string_view anchor(TextAlign align) {
    switch (align) {
    case TextAlign::Left:
        return "start";
    case TextAlign::Right:
        return "end";
    case TextAlign::Centre:
        break;
    }
    return "middle";
}

// What the style of a text adds for its face.
std::string_view faceStyle(TextFace face) {
    switch (face) {
    case TextFace::Italic:
        return ";font-style:italic";
    case TextFace::Bold:
        return ";font-weight:bold";
    case TextFace::Regular:
        break;
    }
    return "";
}

// The left edge of a text drawn at its origin with its alignment.
double textLeft(const TextItem& text) {
    const double width = textWidth(text);
    switch (text.align) {
    case TextAlign::Left:
        return text.origin.x;
    case TextAlign::Right:
        return text.origin.x - width;
    case TextAlign::Centre:
        break;
    }
    return text.origin.x - width / 2;
}

void writeCurve(std::string& svg, const CurveItem& curve) {
    const std::string_view role = roleName(curve.role);
    if (curve.thickness <= curve.endThickness) {
        svg += std::format(R"(<path class="{}" fill="none" stroke-width="{}" d="M{} C{} {} {}"/>)"
                           "\n",
                           role, number(curve.thickness), point(curve.from), point(curve.control1),
                           point(curve.control2), point(curve.to));
        return;
    }
    // A lens: the control points moved up and down so that the middle gets
    // `thickness`, stroked with the end thickness to round the tips.
    const double offset = (curve.thickness - curve.endThickness) / 1.5;
    const auto moved = [](const Point& p, double by) {
        return point(Point{p.x, p.y + by});
    };
    svg += std::format(
        R"(<path class="{}" stroke-width="{}" stroke-linejoin="round" d="M{} C{} {} {} C{} {} {}Z"/>)"
        "\n",
        role, number(curve.endThickness), point(curve.from), moved(curve.control1, -offset / 2),
        moved(curve.control2, -offset / 2), point(curve.to), moved(curve.control2, offset / 2),
        moved(curve.control1, offset / 2), point(curve.from));
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
    svg += R"(text.t{font-family:Arial,Helvetica,sans-serif})";
    svg += "</style>\n";
    svg += std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="white"/>)"
                       "\n",
                       number(x), number(y), number(width), number(height));
    svg += "<g fill=\"black\" stroke=\"black\">\n";
    for (const LineItem& line : list.lines) {
        const std::string dash =
            line.dash > 0.0 ? std::format(R"( stroke-dasharray="{}")", number(line.dash)) : "";
        svg +=
            std::format(R"(<line class="{}" x1="{}" y1="{}" x2="{}" y2="{}" stroke-width="{}"{}/>)"
                        "\n",
                        roleName(line.role), number(line.from.x), number(line.from.y),
                        number(line.to.x), number(line.to.y), number(line.thickness), dash);
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
        const std::string size = glyph.scale == 1.0 ? std::string()
                                                    : std::format(R"( style="font-size:{}px")",
                                                                  number(4.0 * glyph.scale));
        svg += std::format(R"(<text class="{}" x="{}" y="{}"{} stroke="none">&#x{:X};</text>)"
                           "\n",
                           roleName(glyph.role), number(glyph.origin.x), number(glyph.origin.y),
                           size, static_cast<std::uint32_t>(codepoint(glyph.glyph)));
    }
    for (const TextItem& text : list.texts) {
        if (text.knockout) {
            const double pad = 0.08 * text.size;
            svg += std::format(
                R"(<rect class="knockout" x="{}" y="{}" width="{}" height="{}" fill="white" stroke="none"/>)"
                "\n",
                number(textLeft(text) - pad), number(text.origin.y - capHeight(text.size) - pad),
                number(textWidth(text) + 2 * pad), number(capHeight(text.size) + 2 * pad));
        }
        svg += std::format(
            R"(<text class="t {}" x="{}" y="{}" style="font-size:{}px{}" text-anchor="{}" stroke="none">{}</text>)"
            "\n",
            roleName(text.role), number(text.origin.x), number(text.origin.y), number(text.size),
            faceStyle(text.face), anchor(text.align), escapeAttribute(text.text));
    }
    for (const CurveItem& curve : list.curves) {
        writeCurve(svg, curve);
    }
    for (const PolygonItem& polygon : list.polygons) {
        std::string points;
        for (const Point& p : polygon.points) {
            if (!points.empty()) {
                points += ' ';
            }
            points += point(p);
        }
        svg += std::format(R"(<polygon class="{}" stroke="none" points="{}"/>)"
                           "\n",
                           roleName(polygon.role), points);
    }
    svg += "</g>\n</svg>\n";
    return svg;
}

} // namespace elara::notation
