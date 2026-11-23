// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::CommandGroup;
using elara::core::Document;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::InsertMeasures;
using elara::core::InsertTrack;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Measure;
using elara::core::MeasureHeader;
using elara::core::NoteValue;
using elara::core::RemoveMeasures;
using elara::core::RemoveTrack;
using elara::core::ReplaceMeasure;
using elara::core::SetMeasureHeader;
using elara::core::SetSongInfo;
using elara::core::SetStartTempo;
using elara::core::Song;
using elara::core::SongInfo;
using elara::core::Tempo;
using elara::core::TimeSignature;
using elara::core::Track;

namespace {

Track guitar() {
    return Track::fromPreset(*findInstrumentPreset("electric-guitar"));
}

// A measure with one whole note on the given fret of the low E string.
Measure wholeNote(const Song& song, std::size_t track, int fret) {
    Measure measure;
    measure.voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole),
             .notes = {song.track(track).staff(0).frettedNote(6, fret)}});
    return measure;
}

// Two guitar tracks, four measures, a different note in every measure.
Song smallSong() {
    Song song;
    song.addTrack(guitar());
    song.addTrack(guitar());
    song.appendMeasures(4);
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            song.track(t).staff(0).measure(m) = wholeNote(song, t, static_cast<int>(m + (t * 5)));
        }
    }
    song.measureHeader(2).timeSignature = TimeSignature(3, 4);
    return song;
}

} // namespace

TEST_CASE("A document applies commands and undoes and redoes them", "[core][document]") {
    Document doc;
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.canRedo());

    doc.execute(std::make_unique<InsertTrack>(0, guitar()));
    doc.execute(std::make_unique<InsertMeasures>(0, 4));
    CHECK(doc.song().trackCount() == 1);
    CHECK(doc.song().measureCount() == 4);
    CHECK(doc.undoCount() == 2);
    CHECK(doc.undoName() == "Insert measures");

    doc.undo();
    CHECK(doc.song().measureCount() == 0);
    CHECK(doc.redoName() == "Insert measures");
    CHECK(doc.undoName() == "Add track");

    doc.redo();
    CHECK(doc.song().measureCount() == 4);
    CHECK(doc.redoCount() == 0);
    CHECK(doc.redoName().empty());

    doc.undo();
    doc.undo();
    CHECK(doc.song() == Song{});
    CHECK_FALSE(doc.canUndo());
    CHECK(doc.redoCount() == 2);
}

TEST_CASE("A new command discards what could be redone", "[core][document]") {
    Document doc(smallSong());
    doc.execute(std::make_unique<RemoveMeasures>(0, 1));
    doc.undo();
    REQUIRE(doc.canRedo());
    doc.execute(std::make_unique<InsertMeasures>(4, 2));
    CHECK_FALSE(doc.canRedo());
    CHECK(doc.song().measureCount() == 6);
}

TEST_CASE("Undo restores removed content exactly", "[core][document]") {
    const Song original = smallSong();
    Document doc(original);

    doc.execute(std::make_unique<RemoveMeasures>(1, 2)); // includes the 3/4 measure
    CHECK(doc.song().measureCount() == 2);
    doc.execute(std::make_unique<RemoveTrack>(0));
    CHECK(doc.song().trackCount() == 1);
    CHECK(doc.undoName() == "Remove track");

    doc.undo();
    doc.undo();
    CHECK(doc.song() == original);
    doc.redo();
    doc.redo();
    CHECK(doc.song().trackCount() == 1);
    CHECK(doc.song().track(0).staff(0).measure(1) == original.track(1).staff(0).measure(3));
}

TEST_CASE("Content, header, info and tempo edits are undoable", "[core][document]") {
    const Song original = smallSong();
    Document doc(original);

    doc.execute(std::make_unique<ReplaceMeasure>(1, 0, 3, Measure{}));
    CHECK(doc.song().track(1).staff(0).measure(3).isEmpty());

    MeasureHeader header = doc.song().measureHeader(0);
    header.keySignature = KeySignature(-3, KeyMode::Major);
    doc.execute(std::make_unique<SetMeasureHeader>(0, header));
    CHECK(doc.song().measureHeader(0).keySignature.fifths() == -3);

    doc.execute(std::make_unique<SetSongInfo>(SongInfo{.title = "Etude"}));
    CHECK(doc.song().info().title == "Etude");

    doc.execute(std::make_unique<SetStartTempo>(Tempo(90)));
    CHECK(doc.song().startTempo().bpm() == 90);
    CHECK(doc.undoName() == "Change tempo");

    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song() == original);
}

TEST_CASE("A group is one undo step", "[core][document]") {
    const Song original = smallSong();
    Document doc(original);

    std::vector<std::unique_ptr<elara::core::Command>> steps;
    steps.push_back(std::make_unique<InsertMeasures>(4, 1));
    steps.push_back(std::make_unique<ReplaceMeasure>(0, 0, 4, wholeNote(original, 0, 7)));
    doc.execute(std::make_unique<CommandGroup>("Add a measure with a note", std::move(steps)));

    CHECK(doc.undoCount() == 1);
    CHECK(doc.undoName() == "Add a measure with a note");
    CHECK_FALSE(doc.song().track(0).staff(0).measure(4).isEmpty());
    doc.undo();
    CHECK(doc.song() == original);
}

TEST_CASE("A failing command changes nothing and is not recorded", "[core][document]") {
    const Song original = smallSong();
    Document doc(original);

    CHECK_THROWS_AS(doc.execute(std::make_unique<InsertMeasures>(9, 1)), std::out_of_range);
    CHECK_THROWS_AS(doc.execute(std::make_unique<RemoveTrack>(2)), std::out_of_range);
    CHECK_THROWS_AS(doc.execute(std::make_unique<ReplaceMeasure>(0, 1, 0, Measure{})),
                    std::out_of_range); // a guitar has one staff
    CHECK_THROWS_AS(doc.execute(std::make_unique<SetMeasureHeader>(4, MeasureHeader{})),
                    std::out_of_range);

    // The second step fails after the first one was applied: the group rolls back.
    std::vector<std::unique_ptr<elara::core::Command>> steps;
    steps.push_back(std::make_unique<InsertMeasures>(0, 2));
    steps.push_back(std::make_unique<RemoveMeasures>(5, 4));
    CHECK_THROWS_AS(doc.execute(std::make_unique<CommandGroup>("Broken", std::move(steps))),
                    std::out_of_range);

    CHECK(doc.song() == original);
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("The history keeps a bounded number of steps", "[core][document]") {
    Document doc(Song{}, 3);
    for (int i = 0; i < 5; ++i) {
        doc.execute(std::make_unique<InsertMeasures>(0, 1));
    }
    CHECK(doc.undoCount() == 3);
    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song().measureCount() == 2); // the two oldest steps cannot be undone
}

TEST_CASE("The modified flag follows the save point", "[core][document]") {
    Document doc(smallSong(), 2);
    CHECK_FALSE(doc.isModified());

    doc.execute(std::make_unique<InsertMeasures>(0, 1));
    CHECK(doc.isModified());
    doc.markSaved();
    CHECK_FALSE(doc.isModified());

    doc.undo();
    CHECK(doc.isModified());
    doc.redo();
    CHECK_FALSE(doc.isModified()); // back at the saved state

    doc.undo();
    doc.execute(std::make_unique<InsertMeasures>(0, 2)); // the saved state is gone
    doc.undo();
    CHECK(doc.isModified());

    doc.redo();
    doc.markSaved();                                     // history: B, saved after B
    doc.execute(std::make_unique<InsertMeasures>(0, 1)); // B C
    doc.execute(std::make_unique<InsertMeasures>(0, 1)); // C D: B dropped
    CHECK(doc.undoCount() == 2);
    doc.undo();
    doc.undo();
    CHECK_FALSE(doc.isModified()); // the saved state is still the oldest one
    doc.redo();
    doc.redo();
    doc.execute(std::make_unique<InsertMeasures>(0, 1)); // D E: now it is out of reach
    doc.undo();
    doc.undo();
    CHECK(doc.isModified());
}

TEST_CASE("Undoing any sequence of edits restores the original song", "[core][document]") {
    const Song original = smallSong();
    Document doc(original, 10'000);
    std::mt19937 rng(20261123); // fixed seed: reproducible
    const auto pick = [&rng](std::size_t bound) {
        return std::uniform_int_distribution<std::size_t>(0, bound)(rng);
    };

    for (int step = 0; step < 300; ++step) {
        const Song& song = doc.song();
        switch (pick(5)) {
        case 0:
            doc.execute(std::make_unique<InsertMeasures>(pick(song.measureCount()), 1 + pick(2)));
            break;
        case 1:
            if (song.measureCount() > 1) {
                const std::size_t at = pick(song.measureCount() - 1);
                doc.execute(std::make_unique<RemoveMeasures>(at, 1));
            }
            break;
        case 2:
            if (song.trackCount() < 4) {
                doc.execute(std::make_unique<InsertTrack>(pick(song.trackCount()), guitar()));
            }
            break;
        case 3:
            if (song.trackCount() > 1) {
                doc.execute(std::make_unique<RemoveTrack>(pick(song.trackCount() - 1)));
            }
            break;
        case 4:
            if (song.measureCount() > 0) {
                const std::size_t t = pick(song.trackCount() - 1);
                const std::size_t m = pick(song.measureCount() - 1);
                doc.execute(std::make_unique<ReplaceMeasure>(
                    t, 0, m, wholeNote(song, t, static_cast<int>(pick(12)))));
            }
            break;
        default: {
            if (song.measureCount() > 0) {
                const std::size_t m = pick(song.measureCount() - 1);
                MeasureHeader header = song.measureHeader(m);
                header.timeSignature = TimeSignature(static_cast<int>(2 + pick(5)), 4);
                doc.execute(std::make_unique<SetMeasureHeader>(m, header));
            }
            break;
        }
        }
    }
    const Song edited = doc.song();
    REQUIRE(doc.canUndo());

    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song() == original);
    while (doc.canRedo()) {
        doc.redo();
    }
    CHECK(doc.song() == edited);
}
