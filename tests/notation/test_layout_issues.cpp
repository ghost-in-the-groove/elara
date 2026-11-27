// SPDX-License-Identifier: AGPL-3.0-or-later
// What a laid-out score could not keep (ADR-0150).
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/layout_issues.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include "song_edits.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <random>
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
using elara::notation::PageLayoutCache;
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

// Whether `path` is a song of the corpus that openFile() reads.
bool isCorpusSong(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".gp" || extension == ".gp3" || extension == ".gp4" ||
           extension == ".gp5" || extension == ".gpx" || extension == ".mid" ||
           extension == ".kar" || extension == ".musicxml";
}

// An issue of a page, with the page's index among all the pages.
struct PageIssue {
    std::size_t page = 0;
    LayoutIssue issue;
};

// The issues of the pages of `after` that are not the same page in `before`:
// what an edit can have broken, as the app paints again only those.
std::vector<PageIssue> issuesOfChangedPages(std::span<const DisplayList> before,
                                            std::span<const DisplayList> after,
                                            const PageLayoutOptions& options) {
    std::vector<PageIssue> found;
    for (std::size_t p = 0; p < after.size(); ++p) {
        if (p < before.size() && after[p] == before[p]) {
            continue;
        }
        for (const LayoutIssue& issue : layoutIssues(after.subspan(p, 1), bravura(), options)) {
            found.push_back(PageIssue{.page = p, .issue = issue});
        }
    }
    return found;
}

std::string_view viewName(TrackView view) {
    switch (view) {
    case TrackView::Standard:
        return "standard";
    case TrackView::Tablature:
        return "tab";
    case TrackView::Both:
        break;
    }
    return "both";
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
        if (!isCorpusSong(entry.path())) {
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

TEST_CASE("Layout issues: the corpus stays clean through random edits",
          "[notation][issues][incremental]") {
    // The engine keeps the score legible by itself (ADR-0150): every song of
    // the corpus, in each view, laid out again after each of 50 random edits
    // (notes pasted or cleared, ties, effects, dynamics, lyrics, chord
    // symbols, a second voice, tempos, repeats, endings, clefs and keys) has
    // no overlap and no measure too wide. Only the pages an edit changed are
    // checked, as the app paints only those again.
    constexpr std::uint32_t kSeed = 11;
    constexpr int kEdits = 50;
    const PageLayoutOptions options;
    std::size_t files = 0;
    std::size_t checked = 0;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(std::filesystem::path(ELARA_CORPUS_DIR))) {
        if (!isCorpusSong(entry.path())) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        auto opened = elara::io::openFile(entry.path());
        REQUIRE(opened.has_value());
        ++files;
        std::vector<std::size_t> tracks(opened->song.trackCount());
        std::iota(tracks.begin(), tracks.end(), std::size_t{0});
        for (const TrackView view : {TrackView::Standard, TrackView::Tablature, TrackView::Both}) {
            auto song = std::make_shared<const Song>(opened->song);
            const std::vector<StaffSelection> staves = selectTracks(*song, tracks, view);
            PageLayoutCache cache;
            std::vector<DisplayList> pages = cache.layout(song, staves, bravura(), options);
            INFO(std::format("{} in the {} view", name, viewName(view)));
            REQUIRE(layoutIssues(pages, bravura(), options).empty());
            std::mt19937 random(kSeed);
            for (int step = 0; step < kEdits; ++step) {
                Song next = *song;
                const std::string what = elara::notation::test::edit(next, random);
                song = std::make_shared<const Song>(std::move(next));
                std::vector<DisplayList> again = cache.layout(song, staves, bravura(), options);
                const std::vector<PageIssue> found = issuesOfChangedPages(pages, again, options);
                checked += again.size();
                if (!found.empty()) {
                    // The song as edited, to see it: elara-cli render FILE
                    // --tracks all --view VIEW --layout page --issues.
                    const std::filesystem::path saved =
                        std::filesystem::temp_directory_path() /
                        std::format("elara-issues-{}-{}-{}.elara", entry.path().stem().string(),
                                    viewName(view), step);
                    elara::io::saveNative(*song, saved);
                    const LayoutIssue& issue = found.front().issue;
                    FAIL_CHECK(std::format(
                        "seed {}, edit {} ({}): {} on page {}, measure {}, {} and {}; the song is "
                        "{}",
                        kSeed, step, what, issueKindName(issue.kind), found.front().page + 1,
                        issue.measure + 1, roleName(issue.first), roleName(issue.second),
                        saved.string()));
                    break;
                }
                pages = std::move(again);
            }
        }
    }
    CHECK(files >= 20);
    CHECK(checked > 0);
}

TEST_CASE("Layout issues: a changed page that overlaps fails the edits test",
          "[notation][issues]") {
    // The oracle of the test above: it finds an overlap made on purpose on
    // a page that changed, and does not look at pages that did not.
    const std::vector<DisplayList> before{page(), page()};
    std::vector<DisplayList> after = before;
    after[1].glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.0, 12.0}));
    after[1].glyphs.push_back(glyph(Glyph::NoteheadBlack, Role::Notehead, {20.5, 12.0}));
    const PageLayoutOptions options;
    const std::vector<PageIssue> found = issuesOfChangedPages(before, after, options);
    REQUIRE(found.size() == 1);
    CHECK(found[0].page == 1);
    CHECK(found[0].issue.kind == LayoutIssueKind::Overlap);
    CHECK(found[0].issue.first == Role::Notehead);
    // The same overlap on a page the edit left alone is not looked at.
    CHECK(issuesOfChangedPages(after, after, options).empty());
}

namespace {

using elara::core::FretPosition;
using elara::core::Grace;
using elara::core::HarmonicKind;
using elara::core::Tuning;
using elara::notation::StaffView;

// A whole note of each of `names`, as a chord.
Beat whole(std::initializer_list<std::string_view> names) {
    Beat beat{.duration = Duration(NoteValue::Whole)};
    for (const std::string_view name : names) {
        const auto pitch = Pitch::parse(name);
        REQUIRE(pitch.has_value());
        beat.notes.push_back(Note{.pitch = *pitch});
    }
    return beat;
}

// The note at `fret` on `string` of a guitar in standard tuning.
Note fretted(int string, int fret) {
    return Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                         .fret = static_cast<std::int8_t>(fret)}};
}

// One measure of a guitar staff whose voices hold `beats`, one list each.
Song oneMeasure(const std::vector<std::vector<Beat>>& beats) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .midiProgram = 25},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    for (std::size_t v = 0; v < beats.size(); ++v) {
        song.track(0).staff(0).measure(0).voices[v].beats = beats[v];
    }
    return song;
}

// The overlaps of `song` laid out on pages in `view`.
std::vector<LayoutIssue> overlapsIn(const Song& song, StaffView view) {
    const StaffSelection staff{.view = view};
    return overlaps(layoutPages(song, std::span(&staff, 1), bravura()), bravura());
}

std::size_t countOf(const Song& song, StaffView view, Role role) {
    const StaffSelection staff{.view = view};
    std::size_t count = 0;
    for (const DisplayList& list : layoutPages(song, std::span(&staff, 1), bravura())) {
        count += static_cast<std::size_t>(std::ranges::count(list.glyphs, role, &GlyphItem::role));
    }
    return count;
}

} // namespace

TEST_CASE("Layout issues: two voices a third apart keep their accidentals apart",
          "[notation][issues][voices]") {
    // Neither chord moves aside, so their sharps would share a place: they
    // stand in columns left of both chords (ADR-0151).
    const Song song = oneMeasure({{whole({"C#5"})}, {whole({"A#4"})}});
    CHECK(countOf(song, StaffView::Standard, Role::Accidental) == 2);
    CHECK(overlapsIn(song, StaffView::Standard).empty());
    // A chord of each voice, with three sharps between them.
    const Song chords = oneMeasure({{whole({"C#5", "E5"})}, {whole({"F#4", "A#4"})}});
    CHECK(countOf(chords, StaffView::Standard, Role::Accidental) == 3);
    CHECK(overlapsIn(chords, StaffView::Standard).empty());
}

TEST_CASE("Layout issues: a grace note clears the other voice's accidentals",
          "[notation][issues][voices]") {
    // The sharps of both voices stand left of the chords; the grace note
    // before the upper voice moves left of them too.
    Beat upper = whole({"C#5"});
    const auto pitch = Pitch::parse("D5");
    REQUIRE(pitch.has_value());
    upper.graces.push_back(
        Grace{.duration = Duration(NoteValue::Eighth), .notes = {Note{.pitch = *pitch}}});
    const Song song = oneMeasure({{upper}, {whole({"A#4"})}});
    CHECK(overlapsIn(song, StaffView::Standard).empty());
}

TEST_CASE("Layout issues: fret numbers on neighbouring strings stay apart", "[notation][issues]") {
    // Tied notes are written in parentheses, smaller, and natural harmonics
    // in angle brackets, which reach no higher than the digits.
    std::vector<Beat> beats;
    beats.push_back(
        Beat{.duration = Duration(NoteValue::Half), .notes = {fretted(2, 5), fretted(3, 5)}});
    Beat tied{.duration = Duration(NoteValue::Quarter), .notes = {fretted(2, 5), fretted(3, 5)}};
    for (Note& note : tied.notes) {
        note.effects.tiedFromPrevious = true;
    }
    beats.push_back(tied);
    Beat harmonics{.duration = Duration(NoteValue::Quarter),
                   .notes = {fretted(1, 12), fretted(2, 12), fretted(3, 12)}};
    for (Note& note : harmonics.notes) {
        note.effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = 120};
    }
    beats.push_back(harmonics);
    const Song song = oneMeasure({beats});
    CHECK(overlapsIn(song, StaffView::Tablature).empty());
    CHECK(overlaps(layoutPages(song, everyStaff(song), bravura()), bravura()).empty());
}
