// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/caret.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Caret;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Fraction;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::caretAt;
using elara::notation::caretBox;
using elara::notation::ColumnBox;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::layoutPages;
using elara::notation::layoutSystem;
using elara::notation::PageLayoutOptions;
using elara::notation::Point;
using elara::notation::Rect;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::TextItem;

namespace {

constexpr double kEpsilon = 1e-9;
constexpr double kLineSpace = 1.5;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

Beat play(int string, int fret, NoteValue value = NoteValue::Quarter) {
    const Note note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                    .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                             .fret = static_cast<std::int8_t>(fret)}};
    return Beat{.duration = Duration(value), .notes = {note}};
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

Song guitar(const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

DisplayList tab(const Song& song) {
    const StaffSelection selection{.view = StaffView::Tablature};
    return layoutSystem(song, std::span(&selection, 1), bravura());
}

DisplayList notationAndTab(const Song& song) {
    const std::array<StaffSelection, 2> staves{StaffSelection{.view = StaffView::Standard},
                                               StaffSelection{.view = StaffView::Tablature}};
    return layoutSystem(song, staves, bravura());
}

std::vector<ColumnBox> columnsOf(const DisplayList& list, std::size_t measure) {
    std::vector<ColumnBox> found;
    for (const ColumnBox& column : list.columns) {
        if (column.measure == measure) {
            found.push_back(column);
        }
    }
    return found;
}

std::vector<TextItem> fretNumbers(const DisplayList& list) {
    std::vector<TextItem> found;
    for (const TextItem& item : list.texts) {
        if (item.role == Role::FretNumber) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const TextItem& item) { return item.origin.x; });
    return found;
}

Point centreOf(const Rect& box) {
    return {box.x + box.width / 2, box.y + box.height / 2};
}

// Every caret the song has on its first staff: each beat and free slot of
// voice 0, on each string.
std::vector<Caret> everyCaret(const Song& song) {
    std::vector<Caret> carets;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const auto& voice = song.track(0).staff(0).measure(m).voices[0];
        const bool free = voice.length() < song.measureHeader(m).timeSignature.measureLength();
        const std::size_t slots = voice.beats.size() + (free ? 1 : 0);
        for (std::size_t beat = 0; beat < slots; ++beat) {
            for (int string = 1; string <= 6; ++string) {
                carets.push_back(Caret{.measure = m, .beat = beat, .string = string});
            }
        }
    }
    return carets;
}

} // namespace

TEST_CASE("Layout records where each column of a measure starts", "[notation][caret]") {
    const Song song = guitar({{play(1, 0), play(2, 1, NoteValue::Eighth),
                               play(3, 2, NoteValue::Eighth), rest(NoteValue::Half)},
                              {play(4, 3, NoteValue::Whole)}});
    const DisplayList list = tab(song);
    const auto first = columnsOf(list, 0);
    REQUIRE(first.size() == 4);
    CHECK(first[0].start == Fraction(0, 1));
    CHECK(first[1].start == Fraction(1, 4));
    CHECK(first[2].start == Fraction(3, 8));
    CHECK(first[3].start == Fraction(1, 2));
    CHECK(first[0].x < first[1].x);
    CHECK(first[1].x < first[2].x);
    CHECK(first[2].x < first[3].x);
    REQUIRE(columnsOf(list, 1).size() == 1);
    // Columns lie inside their measure.
    for (const ColumnBox& column : list.columns) {
        const auto& box = list.measures[column.measure];
        CHECK(column.x >= box.x);
        CHECK(column.x < box.x + box.width);
    }
}

TEST_CASE("Columns move with their system onto a page", "[notation][caret]") {
    std::vector<std::vector<Beat>> measures(
        24, {play(1, 0, NoteValue::Half), play(2, 1, NoteValue::Half)});
    const Song song = guitar(measures);
    const StaffSelection selection{.view = StaffView::Tablature};
    PageLayoutOptions options;
    options.measuresPerSystem = 4;
    const auto pages = layoutPages(song, std::span(&selection, 1), bravura(), options);
    REQUIRE_FALSE(pages.empty());
    const DisplayList& page = pages.front();
    REQUIRE(page.systems.size() > 1);
    for (const ColumnBox& column : page.columns) {
        const auto box =
            std::ranges::find(page.measures, column.measure, &elara::notation::MeasureBox::index);
        REQUIRE(box != page.measures.end());
        CHECK(column.x >= box->x);
        CHECK(column.x < box->x + box->width);
    }
    CHECK(page.columns.size() == 2 * page.measures.size());
}

TEST_CASE("The caret sits on its string over the fret number", "[notation][caret]") {
    const Song song = guitar({{play(1, 0), play(2, 10, NoteValue::Eighth),
                               play(3, 2, NoteValue::Eighth), rest(NoteValue::Half)}});
    const DisplayList list = tab(song);
    const auto frets = fretNumbers(list);
    REQUIRE(frets.size() == 3);
    for (std::size_t beat = 0; beat < 3; ++beat) {
        const int string = static_cast<int>(beat) + 1;
        const auto box = caretBox(list, song, Caret{.beat = beat, .string = string}, bravura());
        REQUIRE(box.has_value());
        const Point centre = centreOf(*box);
        CHECK_THAT(centre.x, WithinAbs(frets[beat].origin.x, kEpsilon));
        CHECK_THAT(centre.y, WithinAbs(kLineSpace * (string - 1), kEpsilon));
        CHECK(box->height <= kLineSpace);
        CHECK(box->width >= 1.0); // room for a two-digit fret
    }
    // Another string of the same beat: same x, that string's line.
    const auto low = caretBox(list, song, Caret{.beat = 0, .string = 6}, bravura());
    REQUIRE(low.has_value());
    CHECK_THAT(centreOf(*low).x, WithinAbs(frets[0].origin.x, kEpsilon));
    CHECK_THAT(centreOf(*low).y, WithinAbs(kLineSpace * 5, kEpsilon));
}

TEST_CASE("The free slot sits after the last beat, inside the measure", "[notation][caret]") {
    const Song song = guitar({{play(1, 0, NoteValue::Half)}, {}});
    const DisplayList list = tab(song);
    const auto& first = list.measures[0];
    const auto beat = caretBox(list, song, Caret{.beat = 0}, bravura());
    const auto slot = caretBox(list, song, Caret{.beat = 1}, bravura());
    REQUIRE(beat.has_value());
    REQUIRE(slot.has_value());
    CHECK(slot->x > beat->x + beat->width);
    CHECK(slot->x + slot->width < first.x + first.width);

    // An empty measure: the slot is near its start.
    const auto& second = list.measures[1];
    const auto empty = caretBox(list, song, Caret{.measure = 1, .beat = 0}, bravura());
    REQUIRE(empty.has_value());
    CHECK(empty->x >= second.x);
    CHECK(empty->x + empty->width < second.x + second.width / 2 + 1.0);
}

TEST_CASE("The free slot keeps a note's room after the last beat", "[notation][caret]") {
    // Three eighths of a 4/4 measure: spaced for themselves, not for the
    // missing time, so the slot is not where its time would fall.
    const Song song = guitar({{play(1, 0, NoteValue::Eighth), play(2, 1, NoteValue::Eighth),
                               play(3, 2, NoteValue::Eighth)}});
    const DisplayList list = tab(song);
    const auto last = caretBox(list, song, Caret{.beat = 2}, bravura());
    const auto slot = caretBox(list, song, Caret{.beat = 3}, bravura());
    REQUIRE(last.has_value());
    REQUIRE(slot.has_value());
    CHECK(centreOf(*slot).x - centreOf(*last).x >= 2.5 - kEpsilon);
    CHECK(slot->x + slot->width <= list.measures[0].x + list.measures[0].width + kEpsilon);
}

TEST_CASE("On standard notation the caret spans the staff", "[notation][caret]") {
    const Song song = guitar({{play(1, 0), play(2, 1)}});
    const DisplayList list = elara::notation::layoutStaff(song, 0, 0, bravura());
    const auto box = caretBox(list, song, Caret{.beat = 1, .string = 3}, bravura());
    REQUIRE(box.has_value());
    CHECK(box->y < 0.0);
    CHECK(box->y + box->height > 4.0);

    // With both, the caret goes on the tablature.
    const DisplayList both = notationAndTab(song);
    REQUIRE(both.staves.size() == 2);
    const auto onTab = caretBox(both, song, Caret{.beat = 1, .string = 3}, bravura());
    REQUIRE(onTab.has_value());
    const auto& tabStaff = both.staves[1];
    CHECK_THAT(centreOf(*onTab).y, WithinAbs(tabStaff.y + kLineSpace * 2, kEpsilon));
}

TEST_CASE("No caret box for a measure or staff the list does not show", "[notation][caret]") {
    const Song song = guitar({{play(1, 0)}, {play(1, 0)}});
    const StaffSelection selection{.view = StaffView::Tablature};
    const DisplayList first = layoutSystem(song, std::span(&selection, 1), bravura(),
                                           {.firstMeasure = 0, .measureCount = 1});
    CHECK_FALSE(caretBox(first, song, Caret{.measure = 1}, bravura()).has_value());
    CHECK(caretBox(first, song, Caret{.measure = 0}, bravura()).has_value());
    CHECK_FALSE(caretBox(first, song, Caret{.track = 1}, bravura()).has_value());
}

TEST_CASE("A click at a caret box gives that caret back", "[notation][caret]") {
    const Song song = guitar({{play(1, 0), play(2, 1, NoteValue::Eighth),
                               play(3, 2, NoteValue::Eighth), rest(NoteValue::Quarter)},
                              {play(4, 3, NoteValue::Half)},
                              {}});
    for (const DisplayList& list : {tab(song), notationAndTab(song)}) {
        for (const Caret& caret : everyCaret(song)) {
            const auto box = caretBox(list, song, caret, bravura());
            REQUIRE(box.has_value());
            const auto clicked = caretAt(list, song, centreOf(*box), Caret{}, bravura());
            REQUIRE(clicked.has_value());
            CHECK(*clicked == caret);
        }
    }
}

TEST_CASE("A click picks the staff, string and beat nearest to it", "[notation][caret]") {
    const Song song = guitar({{play(1, 0), play(2, 1), play(3, 2), play(4, 3)}});
    const DisplayList both = notationAndTab(song);
    const auto& notation = both.staves[0];
    const auto& tablature = both.staves[1];
    const auto beat2 = caretBox(both, song, Caret{.beat = 2, .string = 1}, bravura());
    REQUIRE(beat2.has_value());
    const double x = centreOf(*beat2).x + 0.3;

    // Between two strings, nearer the lower one.
    auto clicked = caretAt(both, song, {x, tablature.y + kLineSpace * 3.7}, Caret{}, bravura());
    REQUIRE(clicked.has_value());
    CHECK(clicked->beat == 2);
    CHECK(clicked->string == 5);

    // Below the last string: the last string.
    clicked = caretAt(both, song, {x, tablature.y + tablature.height + 0.6}, Caret{}, bravura());
    REQUIRE(clicked.has_value());
    CHECK(clicked->string == 6);

    // On the standard staff: the beat, keeping the string.
    clicked = caretAt(both, song, {x, notation.y + 2.0}, Caret{.string = 4}, bravura());
    REQUIRE(clicked.has_value());
    CHECK(clicked->beat == 2);
    CHECK(clicked->string == 4);

    // Far from any measure: nothing.
    CHECK_FALSE(caretAt(both, song, {x, tablature.y + 40.0}, Caret{}, bravura()).has_value());
    CHECK_FALSE(caretAt(both, song, {both.width + 5.0, 0.0}, Caret{}, bravura()).has_value());
}

TEST_CASE("A click on a page finds the system under it", "[notation][caret]") {
    std::vector<std::vector<Beat>> measures(12, {play(1, 0, NoteValue::Whole)});
    const Song song = guitar(measures);
    const StaffSelection selection{.view = StaffView::Tablature};
    PageLayoutOptions options;
    options.measuresPerSystem = 4;
    const auto pages = layoutPages(song, std::span(&selection, 1), bravura(), options);
    REQUIRE(pages.size() == 1);
    for (std::size_t m = 0; m < 12; ++m) {
        const Caret caret{.measure = m, .beat = 0, .string = 2};
        const auto box = caretBox(pages[0], song, caret, bravura());
        REQUIRE(box.has_value());
        const auto clicked = caretAt(pages[0], song, centreOf(*box), Caret{}, bravura());
        REQUIRE(clicked.has_value());
        CHECK(*clicked == caret);
    }
}
