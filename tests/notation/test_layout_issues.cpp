// SPDX-License-Identifier: AGPL-3.0-or-later
// What a laid-out score could not keep (ADR-0150).
#include <elara/core/song.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/layout_issues.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <filesystem>
#include <numeric>
#include <span>
#include <string>
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
using elara::notation::LayoutIssue;
using elara::notation::LayoutIssueKind;
using elara::notation::layoutIssues;
using elara::notation::layoutPages;
using elara::notation::MeasureBox;
using elara::notation::mustNotTouch;
using elara::notation::overlaps;
using elara::notation::PageLayoutOptions;
using elara::notation::Point;
using elara::notation::Role;
using elara::notation::selectTracks;
using elara::notation::StaffSelection;
using elara::notation::SystemBox;
using elara::notation::TextAlign;
using elara::notation::TextItem;
using elara::notation::TrackView;

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

/// A page with one system of two measures, its top staff line at y = 10.
DisplayList page() {
    DisplayList list;
    list.width = 120.0;
    list.bottom = 169.7;
    list.systems.push_back(SystemBox{.firstMeasure = 0,
                                     .measureCount = 2,
                                     .x = 8.6,
                                     .y = 10.0,
                                     .width = 100.0,
                                     .height = 4.0,
                                     .top = 4.0,
                                     .bottom = 20.0});
    list.measures.push_back(
        MeasureBox{.index = 0, .x = 8.6, .width = 50.0, .y = 10.0, .height = 4.0});
    list.measures.push_back(
        MeasureBox{.index = 1, .x = 58.6, .width = 50.0, .y = 10.0, .height = 4.0});
    return list;
}

GlyphItem glyph(Glyph which, Role role, Point origin, std::size_t measure = 0) {
    return GlyphItem{.glyph = which, .origin = origin, .role = role, .measure = measure};
}

TextItem text(std::string words, Role role, Point origin, double size = 1.0) {
    return TextItem{.text = std::move(words),
                    .origin = origin,
                    .size = size,
                    .align = TextAlign::Centre,
                    .role = role};
}

std::vector<LayoutIssue> of(const DisplayList& list) {
    return overlaps(std::span(&list, 1), bravura());
}

Beat sixteenth(std::string_view name) {
    const auto pitch = Pitch::parse(name);
    REQUIRE(pitch.has_value());
    return Beat{.duration = Duration(NoteValue::Sixteenth), .notes = {Note{.pitch = *pitch}}};
}

std::vector<StaffSelection> everyStaff(const Song& song) {
    std::vector<std::size_t> tracks(song.trackCount());
    std::iota(tracks.begin(), tracks.end(), std::size_t{0});
    return selectTracks(song, tracks, TrackView::Both);
}

} // namespace

TEST_CASE("Layout issues: noteheads whose ink overlaps are found", "[notation][issues]") {
    DisplayList list = page();
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.0, 12.0}));
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.5, 12.0}));
    const std::vector<LayoutIssue> found = of(list);
    REQUIRE(found.size() == 1);
    CHECK(found[0].kind == LayoutIssueKind::Overlap);
    CHECK(found[0].page == 0);
    CHECK(found[0].measure == 0);
    CHECK(found[0].first == Role::Notehead);
    CHECK(found[0].second == Role::Notehead);
    // Where they overlap: from the second's left to the first's right.
    CHECK_THAT(found[0].box.x, WithinAbs(20.5, 1e-9));
    CHECK(found[0].box.width > 0.5);
    CHECK(found[0].box.width < 0.8);
}

TEST_CASE("Layout issues: neighbours drawn to touch are not an overlap", "[notation][issues]") {
    DisplayList list = page();
    // The two noteheads of a second, either side of their stem.
    const double width = bravura().metrics(Glyph::NoteheadBlack).box.width();
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.0, 12.0}));
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.0 + width - 0.12, 11.5}));
    // A notehead two voices share is drawn twice in one place.
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {40.0, 12.0}));
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {40.0, 12.0}));
    // A rest on a notehead is not looked at.
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Rest, {50.0, 12.0}));
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {50.0, 12.0}));
    CHECK(of(list).empty());
}

TEST_CASE("Layout issues: an accidental on a notehead is found, its roles from the left",
          "[notation][issues]") {
    DisplayList list = page();
    list.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {60.4, 12.0}, 1));
    list.glyphs.push_back(glyph(Glyph::AccidentalSharp, Role::Accidental, {60.0, 12.0}, 1));
    const std::vector<LayoutIssue> found = of(list);
    REQUIRE(found.size() == 1);
    CHECK(found[0].measure == 1);
    CHECK(found[0].first == Role::Accidental);
    CHECK(found[0].second == Role::Notehead);
    CHECK(mustNotTouch(Role::Accidental, Role::Notehead));
    CHECK(mustNotTouch(Role::Notehead, Role::Accidental));
    CHECK_FALSE(mustNotTouch(Role::Stem, Role::Notehead));
    CHECK_FALSE(mustNotTouch(Role::Lyric, Role::Chord));
}

TEST_CASE("Layout issues: fret numbers, syllables and chord names are texts",
          "[notation][issues]") {
    DisplayList list = page();
    // Two frets on one string at one place, and two on neighbouring strings.
    list.texts.push_back(text("12", Role::FretNumber, {20.0, 18.0}, 1.2));
    list.texts.push_back(text("3", Role::FretNumber, {20.2, 18.0}, 1.2));
    list.texts.push_back(text("5", Role::FretNumber, {30.0, 18.0}, 1.2));
    list.texts.push_back(text("7", Role::FretNumber, {30.0, 19.0}, 1.2));
    // Two syllables run into each other.
    list.texts.push_back(text("together", Role::Lyric, {40.0, 17.0}));
    list.texts.push_back(text("again", Role::Lyric, {42.0, 17.0}));
    // A chord name over a syllable is not looked at.
    list.texts.push_back(text("Am7", Role::Chord, {70.0, 17.0}));
    list.texts.push_back(text("love", Role::Lyric, {70.0, 17.0}));
    const std::vector<LayoutIssue> found = of(list);
    REQUIRE(found.size() == 2);
    CHECK(found[0].first == Role::FretNumber);
    CHECK(found[0].second == Role::FretNumber);
    CHECK(found[1].first == Role::Lyric);
    CHECK(found[1].second == Role::Lyric);
}

TEST_CASE("Layout issues: dynamics under a staff never touch", "[notation][issues]") {
    DisplayList list = page();
    list.glyphs.push_back(glyph(Glyph::DynamicForte, Role::Dynamic, {30.0, 18.0}));
    list.glyphs.push_back(glyph(Glyph::DynamicPiano, Role::Dynamic, {30.6, 18.0}));
    list.glyphs.push_back(glyph(Glyph::DynamicForte, Role::Dynamic, {40.0, 18.0}));
    list.glyphs.push_back(glyph(Glyph::DynamicPiano, Role::Dynamic, {43.0, 18.0}));
    const std::vector<LayoutIssue> found = of(list);
    REQUIRE(found.size() == 1);
    CHECK(found[0].first == Role::Dynamic);
    CHECK(found[0].box.x < 31.5);
}

TEST_CASE("Layout issues: the corpus laid out on pages has none", "[notation][issues]") {
    // What the survey of ADR-0150 found: every file clean, as notation and
    // tablature together, so that a change that makes ink collide is caught.
    const std::filesystem::path corpus(ELARA_CORPUS_DIR);
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus)) {
        const std::string extension = entry.path().extension().string();
        if (extension != ".gp" && extension != ".gp3" && extension != ".gp4" &&
            extension != ".gp5" && extension != ".gpx" && extension != ".mid" &&
            extension != ".kar" && extension != ".musicxml") {
            continue;
        }
        INFO(entry.path().filename().string());
        auto opened = elara::io::openFile(entry.path());
        REQUIRE(opened.has_value());
        const Song& song = opened->song;
        const std::vector<StaffSelection> staves = everyStaff(song);
        const PageLayoutOptions options;
        const std::vector<DisplayList> pages = layoutPages(song, staves, bravura(), options);
        const std::vector<LayoutIssue> found = layoutIssues(pages, bravura(), options);
        CHECK(found.empty());
        ++files;
    }
    CHECK(files >= 20);
}

TEST_CASE("Layout issues: ink is compared within its own system", "[notation][issues]") {
    DisplayList list = page();
    // Ink outside every system (a credit's place) is not looked at.
    list.texts.push_back(text("Am", Role::Chord, {20.0, 1.0}));
    list.texts.push_back(text("Am", Role::Chord, {20.2, 1.0}));
    CHECK(of(list).empty());

    // Two pages are numbered apart.
    const std::vector<DisplayList> pages{
        page(), [] {
            DisplayList second = page();
            second.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.0, 12.0}));
            second.glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.3, 12.0}));
            return second;
        }()};
    const std::vector<LayoutIssue> found = overlaps(pages, bravura());
    REQUIRE(found.size() == 1);
    CHECK(found[0].page == 1);
}

TEST_CASE("Layout issues: a measure wider than the page is found", "[notation][issues]") {
    DisplayList list = page();
    list.systems[0].measureCount = 1;
    list.systems[0].firstMeasure = 3;
    list.systems[0].width = 110.0;
    list.measures = {MeasureBox{.index = 3, .x = 8.6, .width = 110.0, .y = 10.0, .height = 4.0}};
    const PageLayoutOptions options;
    const std::vector<LayoutIssue> found = layoutIssues(std::span(&list, 1), bravura(), options);
    REQUIRE(found.size() == 1);
    CHECK(found[0].kind == LayoutIssueKind::MeasureTooWide);
    CHECK(found[0].measure == 3);
    CHECK_THAT(found[0].box.x, WithinAbs(options.width - options.margin, 1e-9));
    CHECK_THAT(found[0].box.width, WithinAbs(8.6 + 110.0 - (120.0 - 8.6), 1e-9));

    // On a real page: sixteenths that need more room than a narrow page has.
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < 2; ++m) {
        auto& beats = song.track(0).staff(0).measure(m).voices[0].beats;
        for (std::size_t b = 0; b < 16; ++b) {
            beats.push_back(sixteenth(b % 2 == 0 ? "C#5" : "Eb4"));
        }
    }
    const PageLayoutOptions narrow{.width = 22.0, .height = 0.0, .margin = 5.0, .credits = false};
    const StaffSelection standard{};
    const std::vector<DisplayList> laid =
        layoutPages(song, std::span(&standard, 1), bravura(), narrow);
    const std::vector<LayoutIssue> wide = layoutIssues(laid, bravura(), narrow);
    REQUIRE(wide.size() == 2);
    CHECK(wide[0].kind == LayoutIssueKind::MeasureTooWide);
    CHECK(wide[0].measure == 0);
    CHECK(wide[1].measure == 1);
    // The same measures on a page wide enough fit.
    const PageLayoutOptions wider{.height = 0.0, .credits = false};
    CHECK(
        layoutIssues(layoutPages(song, std::span(&standard, 1), bravura(), wider), bravura(), wider)
            .empty());
}
