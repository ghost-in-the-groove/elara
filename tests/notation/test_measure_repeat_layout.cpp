// SPDX-License-Identifier: AGPL-3.0-or-later
// Measure repeat signs (ADR-0102).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::MeasureRepeat;
using elara::core::NoteValue;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::LineItem;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;
using elara::notation::StaffSelection;
using elara::notation::StaffView;

namespace {

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// A guitar playing a whole-note C in its first two measures, then the signs.
Song guitarWith(const std::vector<MeasureRepeat>& signs) {
    Song song;
    song.appendMeasures(2 + signs.size());
    song.addTrack(Track(
        "Gtr", Instrument{.name = "Gtr", .kind = InstrumentKind::Fretted, .writtenOffset = 12},
        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    Staff& staff = song.track(0).staff(0);
    for (std::size_t m = 0; m < 2; ++m) {
        staff.measure(m).voices[0].beats = {
            Beat{.duration = Duration(NoteValue::Whole), .notes = {staff.frettedNote(5, 3)}}};
    }
    for (std::size_t s = 0; s < signs.size(); ++s) {
        staff.measure(2 + s).repeat = signs[s];
    }
    return song;
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Glyph which) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [which](const GlyphItem& item) { return item.glyph == which; });
    return found;
}

// The x of each barline, in order.
std::vector<double> barlines(const DisplayList& list) {
    std::vector<double> xs;
    for (const LineItem& line : list.lines) {
        if (line.role == Role::Barline) {
            xs.push_back(line.from.x);
        }
    }
    std::ranges::sort(xs);
    return xs;
}

// The centre of a glyph's box.
std::pair<double, double> centre(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return {item.origin.x + (box.southWest.x + box.northEast.x) / 2,
            item.origin.y - (box.southWest.y + box.northEast.y) / 2};
}

} // namespace

TEST_CASE("Measure repeat: one measure's sign sits in its middle instead of a rest",
          "[notation][layout][repeat]") {
    const DisplayList list = layoutStaff(guitarWith({MeasureRepeat::One}), 0, 0, bravura());
    const std::vector<GlyphItem> signs = glyphs(list, Glyph::Repeat1Bar);
    REQUIRE(signs.size() == 1);
    CHECK(signs[0].role == Role::Repeat);
    CHECK(signs[0].measure == 2);
    CHECK(glyphs(list, Glyph::RestWhole).empty());
    const std::vector<double> xs = barlines(list);
    REQUIRE(xs.size() >= 3);
    const auto [x, y] = centre(signs[0]);
    CHECK_THAT(x, WithinAbs((xs[1] + xs[2]) / 2, 0.5));
    CHECK(x > xs[1]);
    CHECK(x < xs[2]);
    CHECK_THAT(y, WithinAbs(2.0, 1e-9)); // the middle line
}

TEST_CASE("Measure repeat: a two-measure sign stands over the barline between its measures",
          "[notation][layout][repeat]") {
    const Song song = guitarWith({MeasureRepeat::TwoFirst, MeasureRepeat::TwoSecond});
    const DisplayList list = layoutStaff(song, 0, 0, bravura());
    const std::vector<GlyphItem> signs = glyphs(list, Glyph::Repeat2Bars);
    REQUIRE(signs.size() == 1);
    CHECK(signs[0].measure == 3);
    const std::vector<double> xs = barlines(list);
    REQUIRE(xs.size() >= 4);
    CHECK_THAT(centre(signs[0]).first, WithinAbs(xs[2], 0.2));
    CHECK(glyphs(list, Glyph::RestWhole).empty());

    // A line ending between them: each half shows the sign in its middle.
    const DisplayList first = layoutStaff(song, 0, 0, bravura(),
                                          StaffLayoutOptions{.firstMeasure = 0, .measureCount = 3});
    const std::vector<GlyphItem> before = glyphs(first, Glyph::Repeat2Bars);
    REQUIRE(before.size() == 1);
    CHECK(before[0].measure == 2);
    const std::vector<double> firstBars = barlines(first);
    REQUIRE(firstBars.size() >= 3);
    CHECK(centre(before[0]).first > firstBars[1]);
    CHECK(centre(before[0]).first < firstBars[2]);
    const DisplayList second =
        layoutStaff(song, 0, 0, bravura(), StaffLayoutOptions{.firstMeasure = 3});
    const std::vector<GlyphItem> after = glyphs(second, Glyph::Repeat2Bars);
    REQUIRE(after.size() == 1);
    CHECK(after[0].measure == 3);
    CHECK(centre(after[0]).first < barlines(second).back());
}

TEST_CASE("Measure repeat: tablature shows the sign too", "[notation][layout][repeat]") {
    const Song song = guitarWith({MeasureRepeat::One});
    const std::array alone{StaffSelection{.view = StaffView::Tablature}};
    const DisplayList tab = layoutSystem(song, alone, bravura());
    const std::vector<GlyphItem> signs = glyphs(tab, Glyph::Repeat1Bar);
    REQUIRE(signs.size() == 1);
    CHECK(glyphs(tab, Glyph::RestWhole).empty());
    REQUIRE(tab.staves.size() == 1);
    CHECK_THAT(centre(signs[0]).second,
               WithinAbs(tab.staves[0].y + tab.staves[0].height / 2, 1e-9));

    const std::array both{StaffSelection{}, StaffSelection{.view = StaffView::Tablature}};
    CHECK(glyphs(layoutSystem(song, both, bravura()), Glyph::Repeat1Bar).size() == 2);
}

TEST_CASE("Measure repeat: a sign with nothing to repeat leaves a measure rest",
          "[notation][layout][repeat]") {
    Song song = guitarWith({});
    song.track(0).staff(0).measure(0).voices[0].beats.clear();
    song.track(0).staff(0).measure(0).repeat = MeasureRepeat::One;
    const DisplayList list =
        layoutStaff(song, 0, 0, bravura(), StaffLayoutOptions{.measureCount = 1});
    CHECK(glyphs(list, Glyph::Repeat1Bar).empty());
    CHECK(glyphs(list, Glyph::RestWhole).size() == 1);
}
