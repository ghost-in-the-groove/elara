// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/measure_layout.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using elara::core::approximateBytes;
using elara::core::Break;
using elara::core::changeBreaks;
using elara::core::Document;
using elara::core::findInstrumentPreset;
using elara::core::InsertMeasures;
using elara::core::InsertTrack;
using elara::core::LayoutScope;
using elara::core::MeasureLayout;
using elara::core::MeasureLayouts;
using elara::core::RemoveMeasures;
using elara::core::RemoveTrack;
using elara::core::Song;
using elara::core::Track;

namespace {

using Breaks = std::vector<std::pair<std::size_t, Break>>;

Track guitar() {
    return Track::fromPreset(*findInstrumentPreset("electric-guitar"));
}

// `measures` empty measures in `tracks` guitar tracks.
Song songOf(std::size_t measures, std::size_t tracks = 2) {
    Song song;
    song.insertMeasures(0, measures);
    for (std::size_t t = 0; t < tracks; ++t) {
        song.addTrack(guitar());
    }
    return song;
}

void setBreaks(Document& doc, LayoutScope scope, const Breaks& breaks) {
    auto command = changeBreaks(doc.song(), scope, breaks);
    REQUIRE(command);
    doc.execute(std::move(command));
}

// The measures of `scope` with a break other than Auto, and their breaks.
Breaks breaksOf(const Song& song, LayoutScope scope) {
    Breaks breaks;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        if (const Break kind = song.measureLayout(scope, m).breakBefore; kind != Break::Auto) {
            breaks.emplace_back(m, kind);
        }
    }
    return breaks;
}

const LayoutScope kSong = LayoutScope::song();
const LayoutScope kFirst = LayoutScope::ofTrack(0);

} // namespace

TEST_CASE("A measure layout takes two bytes", "[core][measure_layout]") {
    STATIC_CHECK(sizeof(MeasureLayout) == 2);
    CHECK(MeasureLayout{}.isDefault());
    CHECK_FALSE(MeasureLayout{.breakBefore = Break::Keep}.isDefault());
    CHECK_FALSE(MeasureLayout{.spacing = 120}.isDefault());
}

TEST_CASE("A song without adjustments holds no measure layouts", "[core][measure_layout]") {
    const Song song = songOf(8);
    CHECK_FALSE(song.measureLayouts(kSong));
    CHECK_FALSE(song.measureLayouts(kFirst));
    CHECK(song.measureLayout(kSong, 7) == MeasureLayout{});
    CHECK_THROWS_AS(song.measureLayout(kSong, 8), std::out_of_range);
    CHECK_THROWS_AS(song.measureLayouts(LayoutScope::ofTrack(2)), std::out_of_range);
}

TEST_CASE("Breaks are set in one scope as one undo step that does not change the sound",
          "[core][measure_layout]") {
    Document doc(songOf(8));
    const Song before = doc.song();
    setBreaks(doc, kFirst, {{2, Break::System}, {5, Break::Page}, {6, Break::Keep}});

    CHECK(breaksOf(doc.song(), kFirst) ==
          Breaks{{2, Break::System}, {5, Break::Page}, {6, Break::Keep}});
    CHECK(breaksOf(doc.song(), kSong).empty()); // the other scopes keep theirs
    CHECK_FALSE(doc.song().measureLayouts(LayoutScope::ofTrack(1)));
    CHECK(doc.undoName() == "Change breaks");
    CHECK(doc.lastChanges().notation);
    CHECK_FALSE(doc.lastChanges().sound);
    CHECK(doc.song().measureLayouts(kFirst)->size() == 8); // one per measure

    // A10: undo and redo.
    doc.undo();
    CHECK(doc.song() == before);
    CHECK_FALSE(doc.song().measureLayouts(kFirst));
    doc.redo();
    CHECK(breaksOf(doc.song(), kFirst).size() == 3);
}

TEST_CASE("Breaks that are already there change nothing", "[core][measure_layout]") {
    Document doc(songOf(4));
    CHECK_FALSE(changeBreaks(doc.song(), kSong, Breaks{{1, Break::Auto}}));
    setBreaks(doc, kSong, {{1, Break::System}});
    CHECK_FALSE(changeBreaks(doc.song(), kSong, Breaks{{1, Break::System}}));
    CHECK_FALSE(changeBreaks(doc.song(), kSong, Breaks{}));
    // A later entry for the same measure wins.
    setBreaks(doc, kSong, {{2, Break::Page}, {2, Break::Keep}});
    CHECK(doc.song().measureLayout(kSong, 2).breakBefore == Break::Keep);
}

TEST_CASE("Breaks are checked before anything changes", "[core][measure_layout]") {
    const Song song = songOf(4);
    CHECK_THROWS_AS(changeBreaks(song, kSong, Breaks{{4, Break::System}}), std::out_of_range);
    CHECK_THROWS_AS(changeBreaks(song, LayoutScope::ofTrack(2), Breaks{{0, Break::System}}),
                    std::out_of_range);
    CHECK_THROWS_AS(changeBreaks(song, kSong, Breaks{{0, static_cast<Break>(9)}}),
                    std::invalid_argument);
}

TEST_CASE("A scope whose breaks are all back to auto holds none", "[core][measure_layout]") {
    Document doc(songOf(6));
    setBreaks(doc, kSong, {{3, Break::System}});
    REQUIRE(doc.song().measureLayouts(kSong));
    setBreaks(doc, kSong, {{3, Break::Auto}});
    CHECK_FALSE(doc.song().measureLayouts(kSong));
    CHECK(doc.historyBytes() > 0); // the step before holds the old vector

    Song song = songOf(3);
    song.setMeasureLayouts(kSong, MeasureLayouts(std::vector<MeasureLayout>(3)));
    CHECK_FALSE(song.measureLayouts(kSong));
    CHECK_THROWS_AS(song.setMeasureLayouts(kSong, MeasureLayouts(std::vector<MeasureLayout>(2))),
                    std::invalid_argument);
}

TEST_CASE("A break stays on its measure as measures are inserted and removed before it",
          "[core][measure_layout]") {
    // A3.
    Document doc(songOf(10));
    setBreaks(doc, kSong, {{6, Break::System}});
    setBreaks(doc, kFirst, {{6, Break::Page}});
    const Song before = doc.song();

    doc.execute(std::make_unique<InsertMeasures>(2, 3));
    CHECK(breaksOf(doc.song(), kSong) == Breaks{{9, Break::System}});
    CHECK(breaksOf(doc.song(), kFirst) == Breaks{{9, Break::Page}});
    CHECK(doc.song().measureLayouts(kSong)->size() == 13);
    CHECK_FALSE(doc.song().measureLayouts(LayoutScope::ofTrack(1)));

    doc.execute(std::make_unique<RemoveMeasures>(0, 4));
    CHECK(breaksOf(doc.song(), kSong) == Breaks{{5, Break::System}});
    CHECK(breaksOf(doc.song(), kFirst) == Breaks{{5, Break::Page}});

    // Measures inserted after it get the default.
    doc.execute(std::make_unique<InsertMeasures>(9, 2));
    CHECK(breaksOf(doc.song(), kSong) == Breaks{{5, Break::System}});

    doc.undo();
    doc.undo();
    doc.undo();
    CHECK(doc.song() == before);
}

TEST_CASE("Removing the measure that holds a break removes it, and undo brings it back",
          "[core][measure_layout]") {
    // A3.
    Document doc(songOf(10));
    setBreaks(doc, kSong, {{6, Break::System}});
    setBreaks(doc, kFirst, {{2, Break::Keep}, {6, Break::Page}});
    const Song before = doc.song();

    doc.execute(std::make_unique<RemoveMeasures>(5, 3));
    CHECK_FALSE(doc.song().measureLayouts(kSong)); // its only break went with it
    CHECK(breaksOf(doc.song(), kFirst) == Breaks{{2, Break::Keep}});
    CHECK(doc.historyBytes() > 0);

    doc.undo();
    CHECK(doc.song() == before);
    CHECK(breaksOf(doc.song(), kSong) == Breaks{{6, Break::System}});
    CHECK(breaksOf(doc.song(), kFirst) == Breaks{{2, Break::Keep}, {6, Break::Page}});
    doc.redo();
    CHECK(breaksOf(doc.song(), kFirst) == Breaks{{2, Break::Keep}});
}

TEST_CASE("A track takes its own breaks when it is removed and brings them back on undo",
          "[core][measure_layout]") {
    Document doc(songOf(6));
    setBreaks(doc, kFirst, {{3, Break::System}});
    setBreaks(doc, kSong, {{4, Break::Page}});
    const Song before = doc.song();

    doc.execute(std::make_unique<RemoveTrack>(0));
    // The track that was second is first, without breaks; the song keeps its own.
    CHECK_FALSE(doc.song().measureLayouts(kFirst));
    CHECK(breaksOf(doc.song(), kSong) == Breaks{{4, Break::Page}});
    doc.undo();
    CHECK(doc.song() == before);

    // A track whose breaks are not one per measure of the song does not go in.
    Song other = songOf(9, 1);
    other.setMeasureLayouts(kFirst, MeasureLayouts(std::vector<MeasureLayout>(
                                        9, MeasureLayout{.breakBefore = Break::Keep})));
    Track withBreaks = other.takeTrack(0);
    for (std::size_t s = 0; s < withBreaks.staffCount(); ++s) {
        CHECK(withBreaks.staff(s).measureCount() == 9);
    }
    Song song = songOf(9, 0);
    CHECK_NOTHROW(song.addTrack(withBreaks));
    Song shorter = songOf(6, 0);
    CHECK_THROWS_AS(shorter.addTrack(withBreaks), std::invalid_argument);
    CHECK_NOTHROW(Document(songOf(9, 0)).execute(std::make_unique<InsertTrack>(0, withBreaks)));
}

TEST_CASE("A track's breaks count in what it holds", "[core][measure_layout]") {
    Song song = songOf(500, 1);
    const std::size_t without = approximateBytes(song.track(0));
    song.setMeasureLayouts(kFirst, MeasureLayouts(std::vector<MeasureLayout>(
                                       500, MeasureLayout{.breakBefore = Break::System})));
    CHECK(approximateBytes(song.track(0)) >= without + (500 * sizeof(MeasureLayout)));
}

TEST_CASE("Copies of a song share its measure layouts until one changes",
          "[core][measure_layout]") {
    Document doc(songOf(4));
    setBreaks(doc, kSong, {{1, Break::System}});
    const Song copy = doc.song();
    CHECK(&*copy.measureLayouts(kSong) == &*doc.song().measureLayouts(kSong));
    setBreaks(doc, kSong, {{2, Break::Page}});
    CHECK(breaksOf(copy, kSong) == Breaks{{1, Break::System}});
    CHECK(copy != doc.song());
}
