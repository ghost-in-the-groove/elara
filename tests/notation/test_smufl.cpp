// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/smufl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using elara::notation::Anchor;
using elara::notation::anchorName;
using elara::notation::BoundingBox;
using elara::notation::codepoint;
using elara::notation::EngravingDefaults;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::glyphName;
using elara::notation::glyphNamed;
using elara::notation::kGlyphCount;
using elara::notation::Point;

namespace {

std::string errorOf(std::string_view json) {
    const auto font = EngravingFont::fromMetadata(json);
    REQUIRE_FALSE(font.has_value());
    return font.error().message;
}

} // namespace

TEST_CASE("SMuFL: glyphs carry the specification's names and code points", "[notation][smufl]") {
    CHECK(glyphName(Glyph::NoteheadBlack) == "noteheadBlack");
    CHECK(codepoint(Glyph::NoteheadBlack) == U'');
    CHECK(codepoint(Glyph::GClef) == U'');
    CHECK(glyphName(Glyph::TabClef6) == "6stringTabClef");
    CHECK(codepoint(Glyph::TabClef6) == U'');
    CHECK(codepoint(Glyph::Rest8th) == U'');
    CHECK(codepoint(Glyph::Coda) == U'');
    CHECK(anchorName(Anchor::StemUpSE) == "stemUpSE");
    CHECK_FALSE(glyphNamed("noSuchGlyph").has_value());

    // Every glyph: a unique name that finds it again, a unique code point in
    // the private use area SMuFL fills.
    std::set<std::string_view> names;
    std::set<char32_t> points;
    for (std::size_t i = 0; i < kGlyphCount; ++i) {
        const auto glyph = static_cast<Glyph>(i);
        CHECK(glyphNamed(glyphName(glyph)) == glyph);
        CHECK(codepoint(glyph) >= U'');
        CHECK(codepoint(glyph) <= U'');
        names.insert(glyphName(glyph));
        points.insert(codepoint(glyph));
    }
    CHECK(names.size() == kGlyphCount);
    CHECK(points.size() == kGlyphCount);
}

TEST_CASE("SMuFL: metadata gives boxes, advances, anchors and engraving defaults",
          "[notation][smufl]") {
    const auto font = EngravingFont::fromMetadata(R"({
        "fontName": "Test", "fontVersion": 2.5,
        "engravingDefaults": {"stemThickness": 0.2, "textFontFamily": ["serif"]},
        "glyphBBoxes": {
            "noteheadBlack": {"bBoxSW": [0, -0.5], "bBoxNE": [1.2, 0.5]},
            "gClef": {"bBoxSW": [0, -2.6], "bBoxNE": [2.7, 4.4]},
            "accidentalSharp": {"bBoxSW": [0, -1.4]},
            "someOtherGlyph": {"bBoxSW": [0, 0], "bBoxNE": [1, 1]}
        },
        "glyphAdvanceWidths": {"noteheadBlack": 1.25},
        "glyphsWithAnchors": {
            "noteheadBlack": {"stemUpSE": [1.2, 0.2], "stemDownNW": [0, -0.2], "other": [1, 1]}
        }
    })");
    REQUIRE(font.has_value());
    CHECK(font->name() == "Test");
    CHECK(font->version() == "2.5");

    const auto& notehead = font->metrics(Glyph::NoteheadBlack);
    CHECK(notehead.box == BoundingBox{.southWest = {0, -0.5}, .northEast = {1.2, 0.5}});
    CHECK(notehead.box.height() == 1.0);
    CHECK(notehead.advanceWidth == 1.25);
    CHECK(notehead.anchor(Anchor::StemUpSE) == Point{1.2, 0.2});
    CHECK(notehead.anchor(Anchor::StemDownNW) == Point{0, -0.2});
    CHECK_FALSE(notehead.anchor(Anchor::CutOutNE).has_value());
    // Without an advance width, the glyph advances to the right of its box.
    CHECK(font->metrics(Glyph::GClef).advanceWidth == 2.7);

    // A box with one corner does not describe the glyph.
    const std::vector<Glyph> missing = font->missingGlyphs();
    CHECK(missing.size() == kGlyphCount - 2);
    CHECK(std::ranges::find(missing, Glyph::AccidentalSharp) != missing.end());
    CHECK(font->metrics(Glyph::AccidentalSharp).box == BoundingBox{});

    // Defaults the font gives replace Bravura's; the others stay.
    EngravingDefaults expected;
    expected.stemThickness = 0.2;
    CHECK(font->defaults() == expected);
}

TEST_CASE("SMuFL: unreadable metadata is an error with a reason", "[notation][smufl]") {
    CHECK(errorOf("not json") == "SMuFL metadata: not JSON");
    CHECK(errorOf("[1, 2]") == "SMuFL metadata: not a JSON object");
    CHECK(errorOf("{}") == "SMuFL metadata: no glyphBBoxes");
    CHECK(errorOf(R"({"glyphBBoxes": {"someOtherGlyph": {}}})") ==
          "SMuFL metadata: it describes none of the glyphs Elara draws");
    const auto missingFile = EngravingFont::fromMetadataFile("no/such/metadata.json");
    REQUIRE_FALSE(missingFile.has_value());
    CHECK(missingFile.error().message.starts_with("SMuFL metadata: cannot read "));
}

TEST_CASE("SMuFL: Bravura describes every glyph Elara draws", "[notation][smufl]") {
    const auto font =
        EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
    if (!font) {
        FAIL(font.error().message);
    }
    CHECK(font->name() == "Bravura");
    CHECK(font->version() == "1.482");
    CHECK(font->missingGlyphs().empty());

    const auto& black = font->metrics(Glyph::NoteheadBlack);
    CHECK(black.box == BoundingBox{.southWest = {0, -0.5}, .northEast = {1.18, 0.5}});
    CHECK(black.anchor(Anchor::StemUpSE) == Point{1.18, 0.168});
    CHECK(black.anchor(Anchor::StemDownNW) == Point{0, -0.168});
    CHECK(font->metrics(Glyph::Flag8thUp).anchor(Anchor::StemUpNW) == Point{0, -0.04});
    CHECK(font->metrics(Glyph::Flag8thDown).anchor(Anchor::StemDownSW) == Point{0, 0.132});
    CHECK(font->metrics(Glyph::AccidentalSharp).anchor(Anchor::CutOutNE) == Point{0.84, 0.896});
    CHECK(font->metrics(Glyph::DynamicForte).anchor(Anchor::OpticalCenter) == Point{1.256, 0});
    CHECK(font->defaults().staffLineThickness == 0.13);
    CHECK(font->defaults().stemThickness == 0.12);
    CHECK(font->defaults().beamThickness == 0.5);
}
