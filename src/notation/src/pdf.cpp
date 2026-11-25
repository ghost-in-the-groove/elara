// SPDX-License-Identifier: AGPL-3.0-or-later
// A PDF 1.7 writer for display lists (ADR-0074), from the PDF specification
// (ISO 32000-1): each page's content stream maps staff spaces to points with
// y down, as display lists measure; glyphs are form XObjects of their
// outlines; texts use the standard Type 1 fonts.
#include <elara/notation/pdf.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <utility>
#include <vector>

namespace elara::notation {

namespace {

constexpr double kKnockoutPad = 0.08; // of a text's size, around a knocked-out text

// At most four decimals, no trailing zeros, no "-0".
std::string number(double value) {
    std::string text = std::format("{:.4f}", value);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string point(const Point& p) {
    return number(p.x) + " " + number(p.y);
}

// The next code point of UTF-8 `text` from `at`, moving past it; U+FFFD for a
// malformed sequence.
char32_t nextCodePoint(std::string_view text, std::size_t& at) {
    const auto lead = static_cast<unsigned char>(text[at++]);
    if (lead < 0x80) {
        return lead;
    }
    const std::size_t extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    if (extra == 0) {
        return 0xFFFD;
    }
    char32_t code = lead & (0x3FU >> extra);
    for (std::size_t i = 0; i < extra; ++i) {
        if (at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xC0U) != 0x80U) {
            return 0xFFFD;
        }
        code = (code << 6U) | (static_cast<unsigned char>(text[at++]) & 0x3FU);
    }
    return code;
}

// The byte of the WinAnsi encoding for `code`, '?' for none.
char winAnsi(char32_t code) {
    if ((code >= 0x20 && code <= 0x7E) || (code >= 0xA0 && code <= 0xFF)) {
        return static_cast<char>(code);
    }
    static constexpr std::array<std::pair<char32_t, unsigned char>, 27> kHigh{{
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85},
        {0x2020, 0x86}, {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A},
        {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92},
        {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
        {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C},
        {0x017E, 0x9E}, {0x0178, 0x9F},
    }};
    for (const auto& [from, to] : kHigh) {
        if (from == code) {
            return static_cast<char>(to);
        }
    }
    return '?';
}

// UTF-8 `text` as a PDF literal string in the WinAnsi encoding.
std::string literal(std::string_view text) {
    std::string out = "(";
    for (std::size_t at = 0; at < text.size();) {
        const char c = winAnsi(nextCodePoint(text, at));
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + ")";
}

// UTF-8 `text` as a PDF text string in UTF-16BE, hexadecimal.
std::string textString(std::string_view text) {
    std::string out = "<FEFF";
    for (std::size_t at = 0; at < text.size();) {
        char32_t code = nextCodePoint(text, at);
        if (code > 0x10FFFF) {
            code = 0xFFFD;
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            out += std::format("{:04X}{:04X}", 0xD800 + (code >> 10U), 0xDC00 + (code & 0x3FFU));
        } else {
            out += std::format("{:04X}", static_cast<unsigned>(code));
        }
    }
    return out + ">";
}

// The left edge of a text drawn at its origin with its alignment.
double textLeft(const TextItem& text) {
    const double width = textWidth(text.text, text.size);
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

// The drawing operators of one page, keeping the line state it last set.
class Content {
public:
    void lineWidth(double width) {
        if (width != width_) {
            ops_ += number(width) + " w\n";
            width_ = width;
        }
    }
    void dash(double length) {
        if (length != dash_) {
            ops_ += length > 0.0 ? std::format("[{}] 0 d\n", number(length)) : "[] 0 d\n";
            dash_ = length;
        }
    }
    void roundJoin(bool round) {
        if (round != round_) {
            ops_ += round ? "1 j\n" : "0 j\n";
            round_ = round;
        }
    }
    void polygon(std::span<const Point> points) {
        if (points.empty()) {
            return;
        }
        ops_ += point(points.front()) + " m";
        for (std::size_t i = 1; i < points.size(); ++i) {
            ops_ += " " + point(points[i]) + " l";
        }
        ops_ += " h f\n";
    }
    std::string& ops() noexcept { return ops_; }

private:
    std::string ops_;
    double width_ = -1.0;
    double dash_ = 0.0;
    bool round_ = false;
};

void writeCurve(Content& content, const CurveItem& curve) {
    if (curve.thickness <= curve.endThickness) {
        content.roundJoin(false);
        content.lineWidth(curve.thickness);
        content.ops() += std::format("{} m {} {} {} c S\n", point(curve.from),
                                     point(curve.control1), point(curve.control2), point(curve.to));
        return;
    }
    // A lens, as the SVG writer draws it: the control points moved up and down
    // so that the middle gets `thickness`, stroked with the end thickness.
    const double offset = (curve.thickness - curve.endThickness) / 1.5;
    const auto moved = [](const Point& p, double by) {
        return point(Point{p.x, p.y + by});
    };
    content.roundJoin(true);
    content.lineWidth(curve.endThickness);
    content.ops() += std::format(
        "{} m {} {} {} c {} {} {} c h B\n", point(curve.from), moved(curve.control1, -offset / 2),
        moved(curve.control2, -offset / 2), point(curve.to), moved(curve.control2, offset / 2),
        moved(curve.control1, offset / 2), point(curve.from));
}

// The outline of a glyph as a form XObject's path, in staff spaces, y up.
std::string glyphPath(const std::vector<OutlineStep>& outline, std::array<double, 4>& box) {
    std::string path;
    box = {0.0, 0.0, 0.0, 0.0};
    bool first = true;
    const auto grow = [&](const Point& p) {
        if (first) {
            box = {p.x, p.y, p.x, p.y};
            first = false;
            return;
        }
        box = {std::min(box[0], p.x), std::min(box[1], p.y), std::max(box[2], p.x),
               std::max(box[3], p.y)};
    };
    for (const OutlineStep& step : outline) {
        switch (step.kind) {
        case OutlineStep::Kind::Move:
            path += point(step.to) + " m\n";
            grow(step.to);
            break;
        case OutlineStep::Kind::Line:
            path += point(step.to) + " l\n";
            grow(step.to);
            break;
        case OutlineStep::Kind::Curve:
            path += std::format("{} {} {} c\n", point(step.control1), point(step.control2),
                                point(step.to));
            grow(step.control1);
            grow(step.control2);
            grow(step.to);
            break;
        case OutlineStep::Kind::Close:
            path += "h\n";
            break;
        }
    }
    return path + "f\n";
}

// The objects of a document, numbered from 1, written with their cross-reference table.
class Document {
public:
    explicit Document(const PdfOptions& options) : options_(options) {}

    // Reserves the next object's number.
    std::size_t reserve() {
        objects_.emplace_back();
        return objects_.size();
    }
    void set(std::size_t object, std::string body) { objects_[object - 1] = std::move(body); }
    std::size_t add(std::string body) {
        const std::size_t object = reserve();
        set(object, std::move(body));
        return object;
    }
    // A stream object: `dict` holds the entries besides its length and filter.
    std::string stream(std::string_view dict, std::string_view data) const {
        if (options_.compress) {
            const std::string packed = options_.compress(data);
            return std::format("<< {} /Length {} /Filter /FlateDecode >>\nstream\n{}\nendstream",
                               dict, packed.size(), packed);
        }
        return std::format("<< {} /Length {} >>\nstream\n{}\nendstream", dict, data.size(), data);
    }

    std::string write(std::size_t root, std::size_t info) const {
        std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offsets;
        offsets.reserve(objects_.size());
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::format("{} 0 obj\n{}\nendobj\n", i + 1, objects_[i]);
        }
        const std::size_t xref = pdf.size();
        pdf += std::format("xref\n0 {}\n0000000000 65535 f \n", objects_.size() + 1);
        for (const std::size_t offset : offsets) {
            pdf += std::format("{:010} 00000 n \n", offset);
        }
        pdf +=
            std::format("trailer\n<< /Size {} /Root {} 0 R /Info {} 0 R >>\nstartxref\n{}\n%%EOF\n",
                        objects_.size() + 1, root, info, xref);
        return pdf;
    }

private:
    const PdfOptions& options_;
    std::vector<std::string> objects_;
};

} // namespace

std::string toPdf(std::span<const DisplayList> pages, const FontOutlines& font,
                  const PdfOptions& options) {
    Document document(options);
    const std::size_t catalog = document.reserve();
    const std::size_t pageTree = document.reserve();
    const std::size_t info =
        document.add(std::format("<< /Title {} /Producer (Elara) >>", textString(options.title)));
    const std::size_t regular = document.add(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    const std::size_t italic = document.add("<< /Type /Font /Subtype /Type1 /BaseFont "
                                            "/Helvetica-Oblique /Encoding /WinAnsiEncoding >>");

    // Each glyph used, once.
    std::map<Glyph, std::size_t> glyphs;
    for (const DisplayList& page : pages) {
        for (const GlyphItem& item : page.glyphs) {
            if (!glyphs.contains(item.glyph) && !font.outline(item.glyph).empty()) {
                std::array<double, 4> box{};
                const std::string path = glyphPath(font.outline(item.glyph), box);
                glyphs[item.glyph] = document.add(document.stream(
                    std::format("/Type /XObject /Subtype /Form /BBox [{} {} {} {}]", number(box[0]),
                                number(box[1]), number(box[2]), number(box[3])),
                    path));
            }
        }
    }
    std::string xobjects;
    for (const auto& [glyph, object] : glyphs) {
        xobjects += std::format(" /G{} {} 0 R", static_cast<int>(glyph), object);
    }
    const std::size_t resources = document.add(std::format(
        "<< /Font << /F1 {} 0 R /F2 {} 0 R >> /XObject <<{} >> >>", regular, italic, xobjects));

    std::string kids;
    const double k = options.staffSpace;
    const double m = options.margin;
    for (const DisplayList& page : pages) {
        const double width = (page.width + 2 * m) * k;
        const double height = (page.bottom - page.top + 2 * m) * k;
        Content content;
        std::string& ops = content.ops();
        // Staff spaces, y down from the page's top, to points.
        ops += std::format("{} 0 0 {} {} {} cm\n0 g 0 G 0 J 0 j\n", number(k), number(-k),
                           number(m * k), number(height - (m - page.top) * k));
        for (const LineItem& line : page.lines) {
            content.roundJoin(false);
            content.lineWidth(line.thickness);
            content.dash(line.dash);
            ops += std::format("{} m {} l S\n", point(line.from), point(line.to));
        }
        content.dash(0.0);
        for (const BeamItem& beam : page.beams) {
            const double half = beam.thickness / 2;
            const std::array<Point, 4> corners{
                Point{beam.left.x, beam.left.y - half}, Point{beam.right.x, beam.right.y - half},
                Point{beam.right.x, beam.right.y + half}, Point{beam.left.x, beam.left.y + half}};
            content.polygon(corners);
        }
        for (const GlyphItem& item : page.glyphs) {
            if (const auto found = glyphs.find(item.glyph); found != glyphs.end()) {
                ops += std::format("q {} 0 0 {} {} {} cm /G{} Do Q\n", number(item.scale),
                                   number(-item.scale), number(item.origin.x),
                                   number(item.origin.y), static_cast<int>(item.glyph));
            }
        }
        for (const TextItem& text : page.texts) {
            const double left = textLeft(text);
            if (text.knockout) {
                const double pad = kKnockoutPad * text.size;
                ops += std::format("1 g {} {} {} {} re f 0 g\n", number(left - pad),
                                   number(text.origin.y - capHeight(text.size) - pad),
                                   number(textWidth(text.text, text.size) + 2 * pad),
                                   number(capHeight(text.size) + 2 * pad));
            }
            ops += std::format("BT /{} 1 Tf {} 0 0 {} {} {} Tm {} Tj ET\n",
                               text.italic ? "F2" : "F1", number(text.size), number(-text.size),
                               number(left), number(text.origin.y), literal(text.text));
        }
        for (const CurveItem& curve : page.curves) {
            writeCurve(content, curve);
        }
        for (const PolygonItem& polygon : page.polygons) {
            content.polygon(polygon.points);
        }
        const std::size_t stream = document.add(document.stream("", ops));
        const std::size_t object = document.add(
            std::format("<< /Type /Page /Parent {} 0 R /MediaBox [0 0 {} {}] /Resources {} 0 R "
                        "/Contents {} 0 R >>",
                        pageTree, number(width), number(height), resources, stream));
        kids += std::format("{}{} 0 R", kids.empty() ? "" : " ", object);
    }
    document.set(pageTree,
                 std::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, pages.size()));
    document.set(catalog, std::format("<< /Type /Catalog /Pages {} 0 R >>", pageTree));
    return document.write(catalog, info);
}

} // namespace elara::notation
