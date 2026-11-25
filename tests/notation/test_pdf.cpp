// SPDX-License-Identifier: AGPL-3.0-or-later
// The music font's glyph outlines and the PDF writer (ADR-0074).
#include <elara/io/compression.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/font_outlines.hpp>
#include <elara/notation/pdf.hpp>
#include <elara/notation/smufl.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::notation::BeamItem;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::FontOutlines;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::LineItem;
using elara::notation::OutlineStep;
using elara::notation::PdfOptions;
using elara::notation::Point;
using elara::notation::PolygonItem;
using elara::notation::Role;
using elara::notation::TextAlign;
using elara::notation::TextItem;

namespace {

const FontOutlines& outlines() {
    static const FontOutlines font = [] {
        auto loaded = FontOutlines::fromFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.otf");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// The box around an outline: its points, and its curves sampled.
std::array<double, 4> boxOf(const std::vector<OutlineStep>& outline) {
    std::array<double, 4> box{1e9, 1e9, -1e9, -1e9};
    const auto grow = [&box](Point p) {
        box = {std::min(box[0], p.x), std::min(box[1], p.y), std::max(box[2], p.x),
               std::max(box[3], p.y)};
    };
    Point from;
    for (const OutlineStep& step : outline) {
        if (step.kind == OutlineStep::Kind::Curve) {
            for (int i = 1; i < 32; ++i) {
                const double t = i / 32.0;
                const double u = 1.0 - t;
                grow(Point{u * u * u * from.x + 3 * u * u * t * step.control1.x +
                               3 * u * t * t * step.control2.x + t * t * t * step.to.x,
                           u * u * u * from.y + 3 * u * u * t * step.control1.y +
                               3 * u * t * t * step.control2.y + t * t * t * step.to.y});
            }
        }
        if (step.kind != OutlineStep::Kind::Close) {
            grow(step.to);
            from = step.to;
        }
    }
    return box;
}

std::size_t count(std::string_view text, std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos;
         at = text.find(part, at + part.size())) {
        ++found;
    }
    return found;
}

std::size_t numberAt(std::string_view text, std::size_t at) {
    std::size_t value = 0;
    std::from_chars(text.data() + at, text.data() + text.size(), value);
    return value;
}

// Every object of `pdf` sits where its cross-reference table says; returns how
// many objects it has.
std::size_t checkCrossReferences(std::string_view pdf) {
    const std::size_t startxref = pdf.rfind("startxref\n");
    REQUIRE(startxref != std::string_view::npos);
    const std::size_t xref = numberAt(pdf, startxref + 10);
    REQUIRE(pdf.substr(xref, 7) == "xref\n0 ");
    const std::size_t size = numberAt(pdf, xref + 7);
    const std::size_t entries = pdf.find('\n', xref + 7) + 1;
    REQUIRE(pdf.substr(entries, 20) == "0000000000 65535 f \n");
    for (std::size_t object = 1; object < size; ++object) {
        const std::string_view entry = pdf.substr(entries + 20 * object, 20);
        REQUIRE(entry.substr(10) == " 00000 n \n");
        const std::size_t offset = numberAt(entry, 0);
        CHECK(pdf.substr(offset, std::format("{} 0 obj\n", object).size()) ==
              std::format("{} 0 obj\n", object));
    }
    CHECK(pdf.find(std::format("trailer\n<< /Size {} ", size)) != std::string_view::npos);
    return size - 1;
}

std::vector<DisplayList> twoPages() {
    DisplayList first;
    first.width = 100.0;
    first.top = 0.0;
    first.bottom = 140.0;
    first.lines.push_back(LineItem{
        .from = {10.0, 20.0}, .to = {90.0, 20.0}, .thickness = 0.13, .role = Role::StaffLine});
    first.lines.push_back(LineItem{.from = {10.0, 30.0},
                                   .to = {50.0, 30.0},
                                   .thickness = 0.16,
                                   .role = Role::Technique,
                                   .dash = 0.5});
    first.glyphs.push_back(
        GlyphItem{.glyph = Glyph::NoteheadBlack, .origin = {20.0, 22.0}, .role = Role::Notehead});
    first.beams.push_back(BeamItem{.left = {20.0, 15.0}, .right = {30.0, 14.0}, .thickness = 0.5});
    first.texts.push_back(TextItem{.text = "Título (1)",
                                   .origin = {50.0, 10.0},
                                   .size = 2.0,
                                   .align = TextAlign::Centre,
                                   .italic = true,
                                   .role = Role::Credit});
    first.curves.push_back(CurveItem{.from = {20.0, 25.0},
                                     .control1 = {22.0, 27.0},
                                     .control2 = {28.0, 27.0},
                                     .to = {30.0, 25.0},
                                     .thickness = 0.22,
                                     .endThickness = 0.1,
                                     .role = Role::Tie});
    first.polygons.push_back(PolygonItem{.points = {{40.0, 40.0}, {42.0, 40.0}, {41.0, 42.0}},
                                         .role = Role::Articulation});
    DisplayList second = first;
    second.glyphs.push_back(GlyphItem{
        .glyph = Glyph::GClef, .origin = {12.0, 23.0}, .role = Role::Clef, .scale = 0.75});
    return {first, second};
}

} // namespace

TEST_CASE("Font outlines: every glyph fills the box the font's metadata gives it",
          "[notation][pdf]") {
    for (std::size_t g = 0; g < elara::notation::kGlyphCount; ++g) {
        const auto glyph = static_cast<Glyph>(g);
        INFO(elara::notation::glyphName(glyph));
        const std::vector<OutlineStep>& outline = outlines().outline(glyph);
        REQUIRE(!outline.empty());
        CHECK(outline.front().kind == OutlineStep::Kind::Move);
        CHECK(outline.back().kind == OutlineStep::Kind::Close);
        const auto box = boxOf(outline);
        const auto& expected = bravura().metrics(glyph).box;
        CHECK_THAT(box[0], WithinAbs(expected.southWest.x, 0.02));
        CHECK_THAT(box[1], WithinAbs(expected.southWest.y, 0.02));
        CHECK_THAT(box[2], WithinAbs(expected.northEast.x, 0.02));
        CHECK_THAT(box[3], WithinAbs(expected.northEast.y, 0.02));
    }
}

TEST_CASE("Font outlines: files that are no OpenType font with CFF outlines are refused",
          "[notation][pdf]") {
    const std::array<std::uint8_t, 5> text{'h', 'e', 'l', 'l', 'o'};
    CHECK(!FontOutlines::fromBytes(text).has_value());
    CHECK(!FontOutlines::fromBytes({}).has_value());
    const auto json =
        FontOutlines::fromFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
    REQUIRE(!json.has_value());
    CHECK(json.error().find("Bravura.json") != std::string::npos);
    CHECK(!FontOutlines::fromFile(std::filesystem::path(ELARA_FONT_DIR) / "missing.otf"));
}

TEST_CASE("PDF: a page per display list, each object where the cross-reference table says",
          "[notation][pdf]") {
    const std::vector<DisplayList> pages = twoPages();
    const std::string pdf =
        elara::notation::toPdf(pages, outlines(), {.staffSpace = 5.0, .title = "Song"});
    CHECK(pdf.starts_with("%PDF-1.7\n"));
    CHECK(pdf.ends_with("%%EOF\n"));
    checkCrossReferences(pdf);
    CHECK(count(pdf, "/Type /Page ") == 2);
    CHECK(pdf.find("/Type /Pages /Kids [") != std::string::npos);
    CHECK(pdf.find("/Count 2 >>") != std::string::npos);
    CHECK(count(pdf, "/MediaBox [0 0 500 700]") == 2);
    // Each glyph once, placed where the lists put it; the clef smaller.
    CHECK(count(pdf, "/Subtype /Form") == 2);
    CHECK(count(pdf, std::format("/G{} Do", static_cast<int>(Glyph::NoteheadBlack))) == 2);
    CHECK(pdf.find(std::format("q 0.75 0 0 -0.75 12 23 cm /G{} Do Q",
                               static_cast<int>(Glyph::GClef))) != std::string::npos);
    // Staff spaces to points, y down from the top of the page.
    CHECK(pdf.find("5 0 0 -5 0 700 cm") != std::string::npos);
    // Lines, a dashed one, the beam, the lens of the tie and the polygon.
    CHECK(pdf.find("10 20 m 90 20 l S") != std::string::npos);
    CHECK(pdf.find("[0.5] 0 d") != std::string::npos);
    CHECK(pdf.find("20 14.75 m 30 13.75 l 30 14.25 l 20 15.25 l h f") != std::string::npos);
    CHECK(pdf.find("c h B") != std::string::npos);
    CHECK(pdf.find("40 40 m 42 40 l 41 42 l h f") != std::string::npos);
    // The text in Helvetica-Oblique, in WinAnsi, centred on its origin.
    CHECK(pdf.find("/BaseFont /Helvetica-Oblique /Encoding /WinAnsiEncoding") != std::string::npos);
    const std::string_view start = "BT /F2 1 Tf 2 0 0 -2 ";
    const std::size_t text = pdf.find(start);
    REQUIRE(text != std::string::npos);
    const std::string_view rest = std::string_view(pdf).substr(text + start.size());
    const std::size_t space = rest.find(' ');
    REQUIRE(space != std::string_view::npos);
    const double left = std::stod(std::string(rest.substr(0, space)));
    CHECK_THAT(left, WithinAbs(50.0 - elara::notation::textWidth("Título (1)", 2.0) / 2, 1e-3));
    const std::string_view after = " 10 Tm (T\xEDtulo \\(1\\)) Tj ET";
    CHECK(rest.substr(space, after.size()) == after);
    CHECK(pdf.find("/Title <FEFF0053006F006E0067>") != std::string::npos);
}

TEST_CASE("PDF: numbers have at most four decimals and no negative zero", "[notation][pdf]") {
    DisplayList page;
    page.width = 10.0;
    page.bottom = 10.0;
    page.lines.push_back(LineItem{.from = {-0.00004, 1.99996},
                                  .to = {-12.34567, 0.5},
                                  .thickness = 0.1,
                                  .role = Role::StaffLine});
    const std::string pdf = elara::notation::toPdf(std::span(&page, 1), outlines());
    CHECK(pdf.find("0 2 m -12.3457 0.5 l S") != std::string::npos);
}

TEST_CASE("PDF: pages written on every core make the same document as on one thread",
          "[notation][pdf]") {
    std::vector<DisplayList> pages;
    for (int copy = 0; copy < 6; ++copy) {
        for (const DisplayList& page : twoPages()) {
            pages.push_back(page);
        }
    }
    PdfOptions one{.title = "Song", .threads = 1};
    PdfOptions every{.title = "Song", .threads = 4};
    CHECK(elara::notation::toPdf(pages, outlines(), one) ==
          elara::notation::toPdf(pages, outlines(), every));
    const auto compress = [](std::string_view data) {
        return elara::io::zlibCompress(data);
    };
    one.compress = compress;
    every.compress = compress;
    const std::string packed = elara::notation::toPdf(pages, outlines(), every);
    CHECK(elara::notation::toPdf(pages, outlines(), one) == packed);
    CHECK(count(packed, "/Type /Page ") == 12);
    checkCrossReferences(packed);
}

TEST_CASE("PDF: compressed streams inflate to the plain ones", "[notation][pdf]") {
    const std::vector<DisplayList> pages = twoPages();
    const std::string plain = elara::notation::toPdf(pages, outlines(), {.title = "Song"});
    const std::string packed = elara::notation::toPdf(
        pages, outlines(), {.title = "Song", .compress = [](std::string_view data) {
                                return elara::io::zlibCompress(data);
                            }});
    CHECK(packed.size() < plain.size());
    checkCrossReferences(packed);
    CHECK(count(packed, "/Filter /FlateDecode") == 4); // two glyphs, two pages
    // The first page's content, inflated, is the plain document's.
    const std::size_t plainStart = plain.find("stream\n", plain.find("/Type /Page ") - 600);
    REQUIRE(plainStart != std::string::npos);
    const std::size_t at =
        packed.rfind("/Filter /FlateDecode >>\nstream\n", packed.find("/Type /Page "));
    REQUIRE(at != std::string::npos);
    const std::size_t lengthAt = packed.rfind("/Length ", at) + 8;
    const std::size_t length = numberAt(packed, lengthAt);
    const std::size_t data = at + std::string_view("/Filter /FlateDecode >>\nstream\n").size();
    const auto inflated =
        elara::io::zlibDecompress(std::string_view(packed).substr(data, length), 1U << 20U);
    REQUIRE(inflated.has_value());
    CHECK(inflated->starts_with("5 0 0 -5") == false); // 1.75 mm staff spaces by default
    CHECK(plain.find(*inflated) != std::string::npos);
}
