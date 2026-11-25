// SPDX-License-Identifier: AGPL-3.0-or-later
// Clef changes from a measure on (ADR-0087).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::LineItem;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;

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

// A treble staff of `measures` measures, each a whole note A3, the clef
// changing at the given measures.
Song staffOf(std::size_t measures, const std::vector<std::pair<std::size_t, Clef>>& changes) {
    Song song;
    song.appendMeasures(measures);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < measures; ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = {Beat{
            .duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = *Pitch::parse("A3")}}}};
    }
    for (const auto& [m, clef] : changes) {
        song.track(0).staff(0).measure(m).clef = clef;
    }
    return song;
}

DisplayList layout(const Song& song, const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, 0, bravura(), options);
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

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

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x * item.scale;
}

} // namespace

TEST_CASE("Clef changes: notes from the change on sit by the new clef",
          "[notation][layout][clef]") {
    // A3: two ledger lines under the treble staff, the top line of the bass staff.
    const DisplayList list = layout(staffOf(4, {{1, Clef::Bass}, {3, Clef::Treble}}));
    std::vector<double> ys;
    for (const GlyphItem& head : glyphs(list, Role::Notehead)) {
        ys.push_back(head.origin.y);
    }
    CHECK(ys == std::vector<double>{6.0, 0.0, 0.0, 6.0});
}

TEST_CASE("Clef changes: the new clef comes smaller before the barline of the measure before",
          "[notation][layout][clef]") {
    const DisplayList list = layout(staffOf(3, {{1, Clef::Bass}}));
    const std::vector<GlyphItem> clefs = glyphs(list, Role::Clef);
    REQUIRE(clefs.size() == 2);
    CHECK(clefs[0].glyph == Glyph::GClef);
    CHECK(clefs[0].scale == 1.0);
    CHECK(clefs[1].glyph == Glyph::FClef);
    CHECK(clefs[1].origin.y == 1.0);
    CHECK(clefs[1].scale < 1.0);
    CHECK(clefs[1].measure == 0);
    // After the first measure's note, clear of the barline closing it.
    const std::vector<GlyphItem> heads = glyphs(list, Role::Notehead);
    const std::vector<double> bars = barlines(list);
    REQUIRE(heads.size() == 3);
    REQUIRE_FALSE(bars.empty());
    CHECK(clefs[1].origin.x > heads[0].origin.x);
    CHECK(right(clefs[1]) < bars[0]);
    CHECK(bars[0] < heads[1].origin.x);

    // The measure makes room for it.
    const DisplayList plain = layout(staffOf(3, {}));
    CHECK(barlines(plain)[0] < bars[0]);
}

TEST_CASE("Clef changes: a line starts with the clef in force and the line before warns of it",
          "[notation][layout][clef]") {
    const Song song = staffOf(4, {{2, Clef::Bass}});
    // The line ending before the change shows it at its end.
    const DisplayList before = layout(song, {.firstMeasure = 0, .measureCount = 2});
    const std::vector<GlyphItem> ending = glyphs(before, Role::Clef);
    REQUIRE(ending.size() == 2);
    CHECK(ending[1].glyph == Glyph::FClef);
    CHECK(ending[1].measure == 1);

    // The next line starts in the bass clef, full size, its key signature on
    // the bass staff's lines (F3 on the second line from the top).
    Song keyed = song;
    for (std::size_t m = 0; m < keyed.measureCount(); ++m) {
        keyed.measureHeader(m).keySignature = KeySignature(1, KeyMode::Major);
    }
    const DisplayList after = layout(keyed, {.firstMeasure = 2, .measureCount = 2});
    const std::vector<GlyphItem> header = glyphs(after, Role::Clef);
    REQUIRE(header.size() == 1);
    CHECK(header[0].glyph == Glyph::FClef);
    CHECK(header[0].scale == 1.0);
    const std::vector<GlyphItem> sharps = glyphs(after, Role::KeySignature);
    REQUIRE(sharps.size() == 1);
    CHECK(sharps[0].origin.y == 1.0);
}

TEST_CASE("Clef changes: a change to the clef in force, or on a drum staff, draws nothing",
          "[notation][layout][clef]") {
    CHECK(glyphs(layout(staffOf(3, {{1, Clef::Treble}})), Role::Clef).size() == 1);

    Song drums = staffOf(3, {{1, Clef::Bass}});
    drums.track(0).staff(0).setClef(Clef::Percussion);
    CHECK(glyphs(layout(drums), Role::Clef).size() == 1);
}
