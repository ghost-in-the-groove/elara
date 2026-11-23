// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace elara::core;

namespace {

const Duration kQuarter(NoteValue::Quarter);
const Duration kHalf(NoteValue::Half);

/// A drum kit in 4/4 over two empty measures.
Song drumSong() {
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    return song;
}

/// The lane (1 from the top) of the kit piece with General MIDI `key`.
int lane(int key) {
    const auto pieces = drumPieces();
    const auto found = std::ranges::find(pieces, key, &DrumPiece::key);
    REQUIRE(found != pieces.end());
    return static_cast<int>(found - pieces.begin()) + 1;
}

Caret at(std::size_t measure, std::size_t beat, int string) {
    return Caret{.measure = measure, .beat = beat, .string = string};
}

Note hit(int key) {
    return Note{.pitch = Pitch::fromMidi(key)};
}

const std::vector<Beat>& beats(const Song& song, std::size_t measure) {
    return song.track(0).staff(0).measure(measure).voices[0].beats;
}

Caret run(Document& doc, Entry entry) {
    REQUIRE(entry.command != nullptr);
    doc.execute(std::move(entry.command));
    return entry.caret;
}

constexpr int kCrash = 49;
constexpr int kClosedHiHat = 42;
constexpr int kSnare = 38;
constexpr int kBassDrum = 36;
constexpr int kPedalHiHat = 44;

} // namespace

TEST_CASE("Drum entry: the kit's lanes run from the top of the staff down", "[core][entry]") {
    const auto pieces = drumPieces();
    REQUIRE(pieces.size() == 12);
    CHECK(pieces.front().key == kCrash);
    CHECK(pieces.back().key == kPedalHiHat);
    CHECK(lane(kClosedHiHat) < lane(kSnare));
    CHECK(lane(kSnare) < lane(kBassDrum));
    for (const DrumPiece& piece : pieces) {
        CHECK(std::ranges::count(pieces, piece.key, &DrumPiece::key) == 1);
        CHECK(!piece.name.empty());
    }
}

TEST_CASE("Drum entry: on a percussion staff the caret moves over the lanes", "[core][entry]") {
    const Song song = drumSong();
    CHECK(clampCaret(song, at(0, 0, 99)).string == 12);
    Caret caret = at(0, 0, 11);
    caret = moveCaret(song, caret, CaretMove::StringDown);
    CHECK(caret.string == 12);
    caret = moveCaret(song, caret, CaretMove::StringDown);
    CHECK(caret.string == 12);
    CHECK(moveCaret(song, at(0, 0, 1), CaretMove::StringUp).string == 1);
}

TEST_CASE("Drum entry: a lane's piece is added on the free slot, joins a beat, then goes",
          "[core][entry]") {
    Document doc(drumSong());
    Entry snare = toggleDrum(doc.song(), at(0, 0, lane(kSnare)), kQuarter);
    REQUIRE(snare.command != nullptr);
    CHECK(snare.command->name() == "Enter note");
    CHECK(run(doc, std::move(snare)) == at(0, 0, lane(kSnare)));
    CHECK(beats(doc.song(), 0) ==
          std::vector<Beat>{Beat{.duration = kQuarter, .notes = {hit(kSnare)}}});

    // The hi-hat joins the snare, above it; the lanes keep the kit's order.
    run(doc, toggleDrum(doc.song(), at(0, 0, lane(kClosedHiHat)), kHalf));
    CHECK(beats(doc.song(), 0).at(0) ==
          Beat{.duration = kQuarter, .notes = {hit(kClosedHiHat), hit(kSnare)}});

    // Toggled again, each goes; the last leaves a rest of the beat's duration.
    Entry removed = toggleDrum(doc.song(), at(0, 0, lane(kClosedHiHat)), kQuarter);
    REQUIRE(removed.command != nullptr);
    CHECK(removed.command->name() == "Delete");
    run(doc, std::move(removed));
    run(doc, toggleDrum(doc.song(), at(0, 0, lane(kSnare)), kQuarter));
    CHECK(beats(doc.song(), 0) == std::vector<Beat>{Beat{.duration = kQuarter}});

    // On the rest, a hit takes the rest's duration.
    run(doc, toggleDrum(doc.song(), at(0, 0, lane(kBassDrum)), kHalf));
    CHECK(beats(doc.song(), 0) ==
          std::vector<Beat>{Beat{.duration = kQuarter, .notes = {hit(kBassDrum)}}});
    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(beats(doc.song(), 0).empty());
}

TEST_CASE("Drum entry: delete removes the lane's piece only", "[core][entry]") {
    Document doc(drumSong());
    run(doc, toggleDrum(doc.song(), at(0, 0, lane(kBassDrum)), kQuarter));
    run(doc, toggleDrum(doc.song(), at(0, 0, lane(kCrash)), kQuarter));
    CHECK(deleteAtCaret(doc.song(), at(0, 0, lane(kSnare))).command == nullptr);
    run(doc, deleteAtCaret(doc.song(), at(0, 0, lane(kCrash))));
    CHECK(beats(doc.song(), 0).at(0).notes == std::vector<Note>{hit(kBassDrum)});
}

TEST_CASE("Drum entry needs a percussion staff", "[core][entry]") {
    Song song = drumSong();
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    CHECK_THROWS_AS(toggleDrum(song, Caret{.track = 1}, kQuarter), std::logic_error);
}
