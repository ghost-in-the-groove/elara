// SPDX-License-Identifier: AGPL-3.0-or-later
// A PDF 1.7 writer for display lists (ADR-0074), from the PDF specification
// (ISO 32000-1): each page's content stream maps staff spaces to points with
// y down, as display lists measure; glyphs are form XObjects of their
// outlines; texts use the standard Type 1 fonts.
#include <elara/core/parallel.hpp>
#include <elara/notation/pdf.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::notation {

namespace {

constexpr double kKnockoutPad = 0.08; // of a text's size, around a knocked-out text

// Appends `value` with at most four decimals, no trailing zeros and no "-0".
// Pages hold hundreds of thousands of numbers, so this avoids std::format.
void put(std::string& out, double value) {
    const double scaled = std::round(value * 10000.0);
    if (!(std::abs(scaled) < 1e15)) { // not a number, or far beyond any page
        out += '0';
        return;
    }
    auto units = static_cast<long long>(scaled);
    if (units < 0) {
        out += '-';
        units = -units;
    }
    std::array<char, 20> whole{};
    const auto written = std::to_chars(whole.data(), whole.data() + whole.size(), units / 10000);
    out.append(whole.data(), written.ptr);
    if (const auto fraction = static_cast<int>(units % 10000); fraction != 0) {
        std::array<char, 5> text{'.', static_cast<char>('0' + fraction / 1000),
                                 static_cast<char>('0' + fraction / 100 % 10),
                                 static_cast<char>('0' + fraction / 10 % 10),
                                 static_cast<char>('0' + fraction % 10)};
        std::size_t length = text.size();
        while (text[length - 1] == '0') {
            --length;
        }
        out.append(text.data(), length);
    }
}

void put(std::string& out, const Point& p) {
    put(out, p.x);
    out += ' ';
    put(out, p.y);
}

std::string number(double value) {
    std::string text;
    put(text, value);
    return text;
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

// The resource name of the standard font a face is written in: Helvetica,
// Helvetica-Oblique or Helvetica-Bold.
std::string_view fontName(TextFace face) {
    switch (face) {
    case TextFace::Italic:
        return "F2";
    case TextFace::Bold:
        return "F3";
    case TextFace::Regular:
        break;
    }
    return "F1";
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

// The drawing operators of one page, keeping the line state it last set.
class Content {
public:
    void lineWidth(double width) {
        if (width != width_) {
            put(ops_, width);
            ops_ += " w\n";
            width_ = width;
        }
    }
    void dash(double length) {
        if (length != dash_) {
            if (length > 0.0) {
                ops_ += '[';
                put(ops_, length);
                ops_ += "] 0 d\n";
            } else {
                ops_ += "[] 0 d\n";
            }
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
        put(ops_, points.front());
        ops_ += " m";
        for (std::size_t i = 1; i < points.size(); ++i) {
            ops_ += ' ';
            put(ops_, points[i]);
            ops_ += " l";
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

// Appends the points of a path, each followed by a space, then `op`.
void putPath(std::string& out, std::initializer_list<Point> points, std::string_view op) {
    for (const Point& p : points) {
        put(out, p);
        out += ' ';
    }
    out += op;
}

void writeCurve(Content& content, const CurveItem& curve) {
    std::string& ops = content.ops();
    if (curve.thickness <= curve.endThickness) {
        content.roundJoin(false);
        content.lineWidth(curve.thickness);
        putPath(ops, {curve.from}, "m ");
        putPath(ops, {curve.control1, curve.control2, curve.to}, "c S\n");
        return;
    }
    // A lens, as the SVG writer draws it: the control points moved up and down
    // so that the middle gets `thickness`, stroked with the end thickness.
    const double offset = (curve.thickness - curve.endThickness) / 1.5;
    const auto moved = [](const Point& p, double by) {
        return Point{p.x, p.y + by};
    };
    content.roundJoin(true);
    content.lineWidth(curve.endThickness);
    putPath(ops, {curve.from}, "m ");
    putPath(ops, {moved(curve.control1, -offset / 2), moved(curve.control2, -offset / 2), curve.to},
            "c ");
    putPath(ops, {moved(curve.control2, offset / 2), moved(curve.control1, offset / 2), curve.from},
            "c h B\n");
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
            putPath(path, {step.to}, "m\n");
            grow(step.to);
            break;
        case OutlineStep::Kind::Line:
            putPath(path, {step.to}, "l\n");
            grow(step.to);
            break;
        case OutlineStep::Kind::Curve:
            putPath(path, {step.control1, step.control2, step.to}, "c\n");
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

// The content stream of one page: staff spaces, y down from the top of the
// page, `m` of them around it, mapped to `k` points each.
std::string pageContent(const DisplayList& page, const std::map<Glyph, std::size_t>& glyphs,
                        double k, double m) {
    Content content;
    std::string& ops = content.ops();
    ops.reserve(48 * (page.lines.size() + page.glyphs.size() + page.texts.size()) + 256);
    put(ops, k);
    ops += " 0 0 ";
    put(ops, -k);
    ops += ' ';
    put(ops, Point{m * k, (page.bottom - page.top + 2 * m) * k - (m - page.top) * k});
    ops += " cm\n0 g 0 G 0 J 0 j\n";
    for (const LineItem& line : page.lines) {
        content.roundJoin(false);
        content.lineWidth(line.thickness);
        content.dash(line.dash);
        putPath(ops, {line.from}, "m ");
        putPath(ops, {line.to}, "l S\n");
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
        if (glyphs.contains(item.glyph)) {
            ops += "q ";
            put(ops, item.scale);
            ops += " 0 0 ";
            put(ops, -item.scale);
            ops += ' ';
            put(ops, item.origin);
            ops += " cm /G";
            ops += std::to_string(static_cast<int>(item.glyph));
            ops += " Do Q\n";
        }
    }
    for (const TextItem& text : page.texts) {
        const double left = textLeft(text);
        if (text.knockout) {
            const double pad = kKnockoutPad * text.size;
            ops += std::format("1 g {} {} {} {} re f 0 g\n", number(left - pad),
                               number(text.origin.y - capHeight(text.size) - pad),
                               number(textWidth(text) + 2 * pad),
                               number(capHeight(text.size) + 2 * pad));
        }
        ops += std::format("BT /{} 1 Tf {} 0 0 {} {} {} Tm {} Tj ET\n", fontName(text.face),
                           number(text.size), number(-text.size), number(left),
                           number(text.origin.y), literal(text.text));
    }
    for (const CurveItem& curve : page.curves) {
        writeCurve(content, curve);
    }
    for (const PolygonItem& polygon : page.polygons) {
        content.polygon(polygon.points);
    }
    return std::move(ops);
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
    // Safe to call from several threads.
    std::string stream(std::string_view dict, std::string_view data) const {
        std::string packed;
        if (options_.compress) {
            packed = options_.compress(data);
            data = packed;
        }
        std::string object = std::format("<< {} /Length {}{} >>\nstream\n", dict, data.size(),
                                         options_.compress ? " /Filter /FlateDecode" : "");
        object.reserve(object.size() + data.size() + 10);
        object += data;
        object += "\nendstream";
        return object;
    }

    std::string write(std::size_t root, std::size_t info) const {
        std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offsets;
        offsets.reserve(objects_.size());
        std::size_t size = pdf.size() + 64 * (objects_.size() + 2);
        for (const std::string& object : objects_) {
            size += object.size() + 32;
        }
        pdf.reserve(size);
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::format("{} 0 obj\n", i + 1);
            pdf += objects_[i];
            pdf += "\nendobj\n";
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
    const std::size_t bold = document.add(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");

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
    const std::size_t resources = document.add(
        std::format("<< /Font << /F1 {} 0 R /F2 {} 0 R /F3 {} 0 R >> /XObject <<{} >> >>", regular,
                    italic, bold, xobjects));

    // Each page's content, written and compressed on every core.
    const double k = options.staffSpace;
    const double m = options.margin;
    std::vector<std::string> contents(pages.size());
    core::forEachIndex(0, pages.size(),
                       core::threadsFor(options.threads, pages.size(), 2, pages.size()),
                       [&](std::size_t p) {
                           contents[p] = document.stream("", pageContent(pages[p], glyphs, k, m));
                       });
    std::string kids;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const DisplayList& page = pages[p];
        const double width = (page.width + 2 * m) * k;
        const double height = (page.bottom - page.top + 2 * m) * k;
        const std::size_t stream = document.add(std::move(contents[p]));
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
