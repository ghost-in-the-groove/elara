// SPDX-License-Identifier: AGPL-3.0-or-later
// Two voices on one staff whose notes start together (ADR-0151).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/layout_issues.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
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
using elara::notation::layoutIssues;
using elara::notation::layoutPages;
using elara::notation::layoutSystem;
using elara::notation::LineItem;
using elara::notation::PageLayoutOptions;
using elara::notation::Role;
using elara::notation::StaffSelection;

namespace {

constexpr double kEpsilon = 1e-6;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

Beat beat(std::initializer_list<std::string_view> names, NoteValue value = NoteValue::Quarter,
          int dots = 0) {
    Beat made{.duration = Duration(value, dots)};
    for (const std::string_view name : names) {
        const auto pitch = Pitch::parse(name);
        REQUIRE(pitch.has_value());
        made.notes.push_back(Note{.pitch = *pitch});
    }
    return made;
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

/// One measure of a treble staff: voice 1 (stems up) and voice 2 (stems down).
Song twoVoices(std::vector<Beat> upper, std::vector<Beat> lower) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    auto& measure = song.track(0).staff(0).measure(0);
    measure.voices[0].beats = std::move(upper);
    measure.voices[1].beats = std::move(lower);
    return song;
}

DisplayList laid(const Song& song) {
    const StaffSelection standard{};
    return layoutSystem(song, std::span(&standard, 1), bravura());
}

/// The noteheads drawn at height y (a staff position: F5 on the top line is
/// 0, each step down 0.5), from the left.
std::vector<GlyphItem> headsAt(const DisplayList& list, double y) {
    std::vector<GlyphItem> found;
    for (const GlyphItem& glyph : list.glyphs) {
        if (glyph.role == Role::Notehead && std::abs(glyph.origin.y - y) < kEpsilon) {
            found.push_back(glyph);
        }
    }
    std::ranges::sort(found, {}, [](const GlyphItem& glyph) { return glyph.origin.x; });
    return found;
}

/// The x of the stems, from the left.
std::vector<double> stems(const DisplayList& list) {
    std::vector<double> found;
    for (const LineItem& line : list.lines) {
        if (line.role == Role::Stem) {
            found.push_back(line.from.x);
        }
    }
    std::ranges::sort(found);
    return found;
}

double width(Glyph glyph) {
    return bravura().metrics(glyph).box.width();
}

/// No layout issue when the song is laid out on a page.
bool clean(const Song& song) {
    const StaffSelection standard{};
    const PageLayoutOptions options{.height = 0.0, .credits = false};
    return layoutIssues(layoutPages(song, std::span(&standard, 1), bravura(), options), bravura(),
                        options)
        .empty();
}

constexpr double kF5 = 0.0;
constexpr double kE5 = 0.5;
constexpr double kD5 = 1.0;
constexpr double kC5 = 1.5;
constexpr double kB4 = 2.0;

} // namespace

TEST_CASE("Voices: a second between voices puts the lower note on the right, stems in one line",
          "[notation][voices]") {
    const Song song = twoVoices({beat({"D5"})}, {beat({"C5"})});
    const DisplayList list = laid(song);
    const auto upper = headsAt(list, kD5);
    const auto lower = headsAt(list, kC5);
    REQUIRE(upper.size() == 1);
    REQUIRE(lower.size() == 1);
    CHECK(lower[0].origin.x > upper[0].origin.x);
    CHECK_THAT(lower[0].origin.x - upper[0].origin.x,
               WithinAbs(width(Glyph::NoteheadBlack) - bravura().defaults().stemThickness, 0.05));
    const auto x = stems(list);
    REQUIRE(x.size() == 2);
    CHECK_THAT(x[1] - x[0], WithinAbs(0.0, 0.05));
    CHECK(clean(song));
}

TEST_CASE("Voices: a unison of the same notehead and dots is one notehead", "[notation][voices]") {
    const Song song = twoVoices({beat({"E5"}), beat({"E5"}, NoteValue::Quarter, 1)},
                                {beat({"E5"}), beat({"E5"}, NoteValue::Quarter, 1)});
    const auto heads = headsAt(laid(song), kE5);
    REQUIRE(heads.size() == 4);
    CHECK(heads[0].origin == heads[1].origin);
    CHECK(heads[2].origin == heads[3].origin);
    CHECK(clean(song));
}

TEST_CASE("Voices: any other unison puts the lower note right of the upper one's ink",
          "[notation][voices]") {
    // A quarter against a half: a black notehead and a white one.
    const Song halves = twoVoices({beat({"F5"}), beat({"F5"})}, {beat({"F5"}, NoteValue::Half)});
    const auto heads = headsAt(laid(halves), kF5);
    REQUIRE(heads.size() == 3);
    CHECK(heads[1].glyph == Glyph::NoteheadHalf);
    CHECK(heads[1].origin.x >= heads[0].origin.x + width(Glyph::NoteheadBlack) - kEpsilon);
    CHECK(clean(halves));

    // Only the upper one dotted: the lower note goes right of the dot.
    const Song dotted =
        twoVoices({beat({"B4"}, NoteValue::Quarter, 1)}, {beat({"B4"}, NoteValue::Quarter)});
    const DisplayList list = laid(dotted);
    const auto pair = headsAt(list, kB4);
    REQUIRE(pair.size() == 2);
    const auto dot = std::ranges::find(list.glyphs, Role::Dot, &GlyphItem::role);
    REQUIRE(dot != list.glyphs.end());
    CHECK(pair[1].origin.x >= dot->origin.x + width(Glyph::AugmentationDot) - kEpsilon);
    CHECK(clean(dotted));
}

TEST_CASE("Voices: chords of both voices a second apart", "[notation][voices]") {
    const Song song = twoVoices({beat({"G5", "E5"})}, {beat({"D5", "B4"})});
    const DisplayList list = laid(song);
    const auto e = headsAt(list, kE5);
    const auto d = headsAt(list, kD5);
    const auto b = headsAt(list, kB4);
    REQUIRE(e.size() == 1);
    REQUIRE(d.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(d[0].origin.x > e[0].origin.x);
    CHECK_THAT(b[0].origin.x, WithinAbs(d[0].origin.x, kEpsilon));
    CHECK(clean(song));
}

TEST_CASE("Voices: the note that moves takes its accidental left of the other note",
          "[notation][voices]") {
    const Song song = twoVoices({beat({"C5"})}, {beat({"Bb4"})});
    const DisplayList list = laid(song);
    const auto upper = headsAt(list, kC5);
    const auto lower = headsAt(list, kB4);
    REQUIRE(upper.size() == 1);
    REQUIRE(lower.size() == 1);
    CHECK(lower[0].origin.x > upper[0].origin.x);
    const auto flat = std::ranges::find(list.glyphs, Role::Accidental, &GlyphItem::role);
    REQUIRE(flat != list.glyphs.end());
    const double right = flat->origin.x + bravura().metrics(flat->glyph).box.northEast.x;
    CHECK(right < upper[0].origin.x);
    CHECK(clean(song));
}

TEST_CASE("Voices: notes a third apart, or a rest, stay in their column", "[notation][voices]") {
    const Song song = twoVoices({beat({"E5"}), beat({"E5"})}, {beat({"C5"}), rest()});
    const DisplayList list = laid(song);
    const auto upper = headsAt(list, kE5);
    const auto lower = headsAt(list, kC5);
    REQUIRE(upper.size() == 2);
    REQUIRE(lower.size() == 1);
    CHECK_THAT(lower[0].origin.x, WithinAbs(upper[0].origin.x, kEpsilon));
    CHECK(clean(song));
}
