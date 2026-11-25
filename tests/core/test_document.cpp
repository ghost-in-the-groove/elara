// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::CommandGroup;
using elara::core::Document;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::Fraction;
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
using elara::core::SetTrackMix;
using elara::core::Song;
using elara::core::SongInfo;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::TrackMix;

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

    doc.execute(std::make_unique<SetTrackMix>(1, TrackMix{.volume = 0.5, .pan = 0.25}));
    CHECK(doc.song().track(1).mix() == TrackMix{.volume = 0.5, .pan = 0.25});
    CHECK(doc.undoName() == "Change mix");
    CHECK_THROWS_AS(doc.execute(std::make_unique<SetTrackMix>(2, TrackMix{})), std::out_of_range);
    CHECK_THROWS_AS(doc.execute(std::make_unique<SetTrackMix>(0, TrackMix{.volume = 2.0})),
                    std::invalid_argument);
    CHECK(doc.undoName() == "Change mix"); // refused commands are not recorded

    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song() == original);
}

TEST_CASE("Header edits are named for what they change", "[core][document]") {
    Document doc(smallSong());
    const auto edit = [&doc](auto change) {
        MeasureHeader header = doc.song().measureHeader(1);
        change(header);
        doc.execute(std::make_unique<SetMeasureHeader>(1, header));
        return doc.undoName();
    };
    CHECK(edit([](MeasureHeader& h) { h.repeatStart = true; }) == "Change repeats");
    CHECK(edit([](MeasureHeader& h) { h.repeatTimes = 3; }) == "Change repeats");
    CHECK(edit([](MeasureHeader& h) { h.alternateEndings = 0b011; }) == "Change repeats");
    CHECK(edit([](MeasureHeader& h) { h.segno = true; }) == "Change navigation marks");
    CHECK(edit([](MeasureHeader& h) { h.keySignature = KeySignature(2, KeyMode::Major); }) ==
          "Change key signature");
    // Repeats and something else at once: a general name.
    CHECK(edit([](MeasureHeader& h) {
              h.repeatTimes = 0;
              h.coda = true;
          }) == "Change measure header");
}

TEST_CASE("The triplet feel changes from a measure until the next change",
          "[core][document][feel]") {
    // ADR-0104, as the signatures change.
    using elara::core::TripletFeel;
    Song song = smallSong();
    song.measureHeader(2).tripletFeel = TripletFeel::Sixteenth;
    Document doc(song);
    doc.execute(elara::core::changeTripletFeel(doc.song(), 0, TripletFeel::Eighth));
    const auto feels = [&doc] {
        std::vector<TripletFeel> all;
        for (std::size_t m = 0; m < doc.song().measureCount(); ++m) {
            all.push_back(doc.song().measureHeader(m).tripletFeel);
        }
        return all;
    };
    CHECK(feels() == std::vector{TripletFeel::Eighth, TripletFeel::Eighth, TripletFeel::Sixteenth,
                                 TripletFeel::None});
    CHECK(doc.undoCount() == 1);
    CHECK(doc.undoName() == "Change triplet feel");
    CHECK(elara::core::changeTripletFeel(doc.song(), 1, TripletFeel::Eighth) == nullptr);
    CHECK_THROWS_AS(elara::core::changeTripletFeel(doc.song(), 4, TripletFeel::None),
                    std::out_of_range);
    doc.undo();
    CHECK(doc.song() == song);
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

TEST_CASE("A time signature change runs until the next change", "[core][document]") {
    // Measures 0-1 are in 4/4, 2-3 in 3/4 (smallSong()).
    const Song original = smallSong();
    Document doc(original);

    doc.execute(elara::core::changeTimeSignature(doc.song(), 0, TimeSignature(6, 8)));
    CHECK(doc.song().measureHeader(0).timeSignature == TimeSignature(6, 8));
    CHECK(doc.song().measureHeader(1).timeSignature == TimeSignature(6, 8));
    CHECK(doc.song().measureHeader(2).timeSignature == TimeSignature(3, 4));
    CHECK(doc.undoCount() == 1);
    CHECK(doc.undoName() == "Change time signature");

    // From the middle of a run: the measures before it keep theirs.
    doc.execute(elara::core::changeTimeSignature(doc.song(), 3, TimeSignature(2, 4)));
    CHECK(doc.song().measureHeader(2).timeSignature == TimeSignature(3, 4));
    CHECK(doc.song().measureHeader(3).timeSignature == TimeSignature(2, 4));

    doc.undo();
    doc.undo();
    CHECK(doc.song() == original);

    // Nothing to change, and a measure the song does not have.
    CHECK(elara::core::changeTimeSignature(original, 2, TimeSignature(3, 4)) == nullptr);
    CHECK_THROWS_AS(elara::core::changeTimeSignature(original, 4, TimeSignature(3, 4)),
                    std::out_of_range);
}

TEST_CASE("A tempo change can be made gradual and sudden again", "[core][document]") {
    // ADR-0082: a measure without a change starts one at the tempo in force.
    const Song original = smallSong();
    Document doc(original);
    doc.execute(elara::core::changeTempoGradual(doc.song(), 1, true));
    CHECK(
        doc.song().measureHeader(1).tempoChanges ==
        std::vector<TempoChange>{TempoChange{.offset = {}, .tempo = Tempo(120), .gradual = true}});
    CHECK(doc.undoName() == "Change tempo");
    CHECK(elara::core::changeTempoGradual(doc.song(), 1, true) == nullptr);

    // Another tempo keeps it gradual; it can be sudden again.
    doc.execute(elara::core::changeTempo(doc.song(), 1, 90));
    CHECK(doc.song().measureHeader(1).tempoChanges.front() ==
          TempoChange{.offset = {}, .tempo = Tempo(90), .gradual = true});
    doc.execute(elara::core::changeTempoGradual(doc.song(), 1, false));
    CHECK_FALSE(doc.song().measureHeader(1).tempoChanges.front().gradual);
    CHECK(doc.song().tempoAt(1) == Tempo(90));
    CHECK(elara::core::changeTempoGradual(doc.song(), 2, false) == nullptr);
    doc.undo();
    doc.undo();
    doc.undo();
    CHECK(doc.song() == original);

    CHECK_THROWS_AS(elara::core::changeTempoGradual(original, 0, true), std::invalid_argument);
    CHECK_THROWS_AS(elara::core::changeTempoGradual(original, 4, true), std::out_of_range);
}

TEST_CASE("A tempo change starts a measure and holds until the next one", "[core][document]") {
    Song original = smallSong();
    // A change halfway through measure 1 is not the one that starts it.
    original.measureHeader(1).tempoChanges.push_back(
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(140)});
    Document doc(original);

    doc.execute(elara::core::changeTempo(doc.song(), 1, 90));
    REQUIRE(doc.song().measureHeader(1).tempoChanges.size() == 2);
    CHECK(doc.song().measureHeader(1).tempoChanges.front() ==
          TempoChange{.offset = {}, .tempo = Tempo(90)});
    CHECK(doc.song().tempoAt(1).bpm() == 90.0);
    CHECK(doc.song().tempoAt(1, Fraction(1, 2)).bpm() == 140.0);
    CHECK(doc.song().tempoAt(0).bpm() == 120.0);
    CHECK(doc.undoName() == "Change tempo");

    // Another tempo replaces it, and none takes it away.
    doc.execute(elara::core::changeTempo(doc.song(), 1, 100));
    CHECK(doc.song().measureHeader(1).tempoChanges.size() == 2);
    CHECK(doc.song().tempoAt(1).bpm() == 100.0);
    doc.execute(elara::core::changeTempo(doc.song(), 1, std::nullopt));
    CHECK(doc.song().measureHeader(1) == original.measureHeader(1));
    CHECK(doc.undoCount() == 3);
    doc.undo();
    doc.undo();
    doc.undo();
    CHECK(doc.song() == original);

    // Nothing to change: no tempo to take away, or the same one again.
    CHECK(elara::core::changeTempo(original, 2, std::nullopt) == nullptr);
    doc.execute(elara::core::changeTempo(doc.song(), 2, 75));
    CHECK(elara::core::changeTempo(doc.song(), 2, 75) == nullptr);

    // The first measure plays at the song's tempo; tempos out of range and
    // measures the song does not have are refused.
    CHECK_THROWS_AS(elara::core::changeTempo(original, 0, 90), std::invalid_argument);
    CHECK_THROWS_AS(elara::core::changeTempo(original, 1, 5), std::invalid_argument);
    CHECK_THROWS_AS(elara::core::changeTempo(original, 4, 90), std::out_of_range);
}

TEST_CASE("A tempo change counts in the beat the song counts in there", "[core][document]") {
    Song original = smallSong();
    const Duration dottedQuarter(NoteValue::Quarter, 1);
    original.setStartTempo(Tempo(60, dottedQuarter));
    Document doc(original);
    doc.execute(elara::core::changeTempo(doc.song(), 3, 80));
    CHECK(doc.song().tempoAt(3) == Tempo(80, dottedQuarter));
}

TEST_CASE("A key signature change runs until the next change", "[core][document]") {
    Song original = smallSong();
    original.measureHeader(3).keySignature = KeySignature(-1, KeyMode::Minor);
    Document doc(original);

    doc.execute(elara::core::changeKeySignature(doc.song(), 1, KeySignature(2, KeyMode::Major)));
    CHECK(doc.song().measureHeader(0).keySignature == KeySignature());
    CHECK(doc.song().measureHeader(1).keySignature == KeySignature(2, KeyMode::Major));
    CHECK(doc.song().measureHeader(2).keySignature == KeySignature(2, KeyMode::Major));
    CHECK(doc.song().measureHeader(3).keySignature == KeySignature(-1, KeyMode::Minor));
    CHECK(doc.undoName() == "Change key signature");
    // The time signatures are left alone.
    CHECK(doc.song().measureHeader(2).timeSignature == TimeSignature(3, 4));

    doc.undo();
    CHECK(doc.song() == original);
    CHECK(elara::core::changeKeySignature(original, 0, KeySignature()) == nullptr);
}

TEST_CASE("Each line of the credits is set on its own as one undo step", "[core][document]") {
    using elara::core::Credit;
    Song original = smallSong();
    original.info().title = "Etude";
    Document doc(original);

    doc.execute(elara::core::changeCredit(doc.song(), Credit::Album, "Studies"));
    doc.execute(elara::core::changeCredit(doc.song(), Credit::Words, "A. Poet"));
    doc.execute(
        elara::core::changeCredit(doc.song(), Credit::Notes, "Play it slowly.\nThen faster."));
    const SongInfo& info = doc.song().info();
    CHECK(info.title == "Etude");
    CHECK(info.album == "Studies");
    CHECK(info.words == "A. Poet");
    CHECK(info.notes == "Play it slowly.\nThen faster.");
    CHECK(elara::core::creditOf(info, Credit::Words) == "A. Poet");
    CHECK(elara::core::creditOf(info, Credit::Copyright).empty());
    CHECK(doc.undoName() == "Edit song info");
    CHECK(doc.undoCount() == 3);

    // A credit that reads the text already needs no edit; an empty one clears it.
    CHECK(elara::core::changeCredit(doc.song(), Credit::Album, "Studies") == nullptr);
    doc.execute(elara::core::changeCredit(doc.song(), Credit::Title, ""));
    CHECK(doc.song().info().title.empty());

    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song() == original);
}

TEST_CASE("A section is named at the measure it starts and only there", "[core][document]") {
    Song original = smallSong();
    Document doc(original);

    doc.execute(elara::core::changeSection(doc.song(), 0, "Intro"));
    doc.execute(elara::core::changeSection(doc.song(), 2, "Chorus"));
    CHECK(doc.song().measureHeader(0).section == "Intro");
    CHECK(doc.song().measureHeader(1).section.empty());
    CHECK(doc.song().measureHeader(2).section == "Chorus");
    CHECK(doc.undoName() == "Change section");
    // Signatures and tempo are left alone, and a new measure starts no section.
    CHECK(doc.song().measureHeader(2).timeSignature == original.measureHeader(2).timeSignature);
    CHECK(doc.song().measureHeader(0).continuation().section.empty());

    // The name it has already needs no edit; an empty name takes it off.
    CHECK(elara::core::changeSection(doc.song(), 2, "Chorus") == nullptr);
    doc.execute(elara::core::changeSection(doc.song(), 0, ""));
    CHECK(doc.song().measureHeader(0).section.empty());

    while (doc.canUndo()) {
        doc.undo();
    }
    CHECK(doc.song() == original);
    CHECK_THROWS_AS(elara::core::changeSection(original, 4, "Outro"), std::out_of_range);
}
