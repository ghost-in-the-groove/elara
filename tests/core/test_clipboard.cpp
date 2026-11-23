// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Clef;
using elara::core::copyMeasures;
using elara::core::Document;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::FretPosition;
using elara::core::InstrumentKind;
using elara::core::KeySignature;
using elara::core::makePaste;
using elara::core::Measure;
using elara::core::MeasureClip;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::PasteMode;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;

namespace {

Track preset(const char* id) {
    return Track::fromPreset(*findInstrumentPreset(id));
}

Measure wholeNote(Note note) {
    Measure measure;
    measure.voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole), .notes = {std::move(note)}});
    return measure;
}

// Track 0: a guitar with E2 open, then G2 on the low string; track 1: a drop D
// guitar; track 2: a flute with C5 in its second measure; track 3: drums with
// a bass drum. Four measures, the second in G major.
Song band() {
    Song song;
    song.addTrack(preset("electric-guitar"));
    const Track guitar = preset("electric-guitar");
    song.addTrack(Track("Drop D", guitar.instrument(),
                        {Staff(Clef::Treble8vb, Tuning::preset("guitar-drop-d"))}));
    song.addTrack(preset("flute"));
    song.addTrack(preset("drum-kit"));
    song.appendMeasures(4);
    song.track(0).staff(0).measure(0) = wholeNote(song.track(0).staff(0).frettedNote(6, 0));
    song.track(0).staff(0).measure(1) = wholeNote(song.track(0).staff(0).frettedNote(6, 3));
    song.track(2).staff(0).measure(1) = wholeNote(Note{.pitch = Pitch::parse("C5").value()});
    song.track(3).staff(0).measure(0) = wholeNote(Note{.pitch = Pitch::fromMidi(36)});
    song.measureHeader(1).keySignature = KeySignature::parse("G major").value();
    return song;
}

std::optional<FretPosition> positionIn(const Song& song, std::size_t track, std::size_t measure) {
    return song.track(track).staff(0).measure(measure).voices[0].beats.at(0).notes.at(0).position;
}

FretPosition at(int string, int fret) {
    return {.string = static_cast<std::int8_t>(string), .fret = static_cast<std::int8_t>(fret)};
}

} // namespace

TEST_CASE("Copied measures keep their content, headers and tunings", "[core][clipboard]") {
    const Song song = band();
    const MeasureClip clip = copyMeasures(song, 0, 2, 0, 1);
    REQUIRE(clip.measureCount() == 2);
    REQUIRE(clip.tracks.size() == 1);
    CHECK(clip.headers[1].keySignature == KeySignature::parse("G major").value());
    CHECK(clip.tracks[0].kind == InstrumentKind::Fretted);
    REQUIRE(clip.tracks[0].staves.size() == 1);
    CHECK(clip.tracks[0].staves[0].tuning == Tuning::standardGuitar());
    CHECK(
        clip.tracks[0].staves[0].measures ==
        std::vector<Measure>{song.track(0).staff(0).measure(0), song.track(0).staff(0).measure(1)});

    CHECK(copyMeasures(song, 1, 3).tracks.size() == 4); // every track
    CHECK(copyMeasures(song, 1, 3, 2).tracks.size() == 2);

    SECTION("only what the song has") {
        CHECK_THROWS_AS(copyMeasures(song, 3, 2), std::out_of_range);
        CHECK_THROWS_AS(copyMeasures(song, 0, 0), std::out_of_range);
        CHECK_THROWS_AS(copyMeasures(song, 0, 1, 4), std::out_of_range);
        CHECK_THROWS_AS(copyMeasures(song, 0, 1, 1, 4), std::out_of_range);
    }
}

TEST_CASE("Pasting over measures is one undo step", "[core][clipboard]") {
    const Song song = band();
    Document doc{Song(song)};
    doc.execute(makePaste(doc.song(), copyMeasures(song, 0, 2, 0, 1), 2));

    CHECK(doc.song().track(0).staff(0).measure(2) == song.track(0).staff(0).measure(0));
    CHECK(doc.song().track(0).staff(0).measure(3) == song.track(0).staff(0).measure(1));
    CHECK(doc.song().measureHeader(3) == song.measureHeader(3)); // the headers stay
    CHECK(doc.song().track(2).staff(0).measure(3) == song.track(2).staff(0).measure(3));
    CHECK(doc.undoName() == "Paste");
    CHECK(doc.undoCount() == 1);

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("Pasting past the end appends measures", "[core][clipboard]") {
    const Song song = band();
    Document doc{Song(song)};
    doc.execute(makePaste(doc.song(), copyMeasures(song, 0, 2, 0, 1), 3));

    REQUIRE(doc.song().measureCount() == 5);
    CHECK(doc.song().track(0).staff(0).measure(3) == song.track(0).staff(0).measure(0));
    CHECK(doc.song().track(0).staff(0).measure(4) == song.track(0).staff(0).measure(1));
    CHECK(doc.song().track(2).staff(0).measure(4).isEmpty());

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("Pasting into new measures brings their headers", "[core][clipboard]") {
    const Song song = band();
    Document doc{Song(song)};
    doc.execute(makePaste(doc.song(), copyMeasures(song, 0, 2, 0, 1), 1, 0, PasteMode::Insert));

    REQUIRE(doc.song().measureCount() == 6);
    CHECK(doc.song().track(0).staff(0).measure(1) == song.track(0).staff(0).measure(0));
    CHECK(doc.song().track(0).staff(0).measure(2) == song.track(0).staff(0).measure(1));
    CHECK(doc.song().track(0).staff(0).measure(3) == song.track(0).staff(0).measure(1));
    CHECK(doc.song().measureHeader(1) == song.measureHeader(0));
    CHECK(doc.song().measureHeader(2) == song.measureHeader(1));
    CHECK(doc.song().track(2).staff(0).measure(1).isEmpty()); // other tracks: rests to fill
    CHECK(doc.song().track(2).staff(0).measure(3) == song.track(2).staff(0).measure(1));

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("Pasted frets follow the target staff", "[core][clipboard]") {
    const Song song = band();
    const MeasureClip clip = copyMeasures(song, 0, 2, 0, 1);

    SECTION("another tuning places the notes again") {
        // Drop D: E2 is fret 2 on the low string, G2 fret 5.
        Document doc{Song(song)};
        doc.execute(makePaste(doc.song(), clip, 0, 1));
        CHECK(positionIn(doc.song(), 1, 0) == at(6, 2));
        CHECK(positionIn(doc.song(), 1, 1) == at(6, 5));
    }
    SECTION("no tuning, no positions") {
        Document doc{Song(song)};
        doc.execute(makePaste(doc.song(), clip, 0, 2));
        CHECK(positionIn(doc.song(), 2, 0) == std::nullopt);
        CHECK(doc.song().track(2).staff(0).measure(0).voices[0].beats[0].notes[0].pitch ==
              Pitch::parse("E2").value());
    }
    SECTION("a note no string of the target can play") {
        Song small;
        small.addTrack(preset("electric-guitar"));
        small.addTrack(preset("ukulele"));
        small.appendMeasures(1);
        small.track(0).staff(0).measure(0) = wholeNote(small.track(0).staff(0).frettedNote(6, 0));
        CHECK_THROWS_AS(makePaste(small, copyMeasures(small, 0, 1, 0, 1), 0, 1),
                        std::invalid_argument);
    }
}

TEST_CASE("Pasting goes only where it fits", "[core][clipboard]") {
    const Song song = band();

    SECTION("drums stay drums, pitches stay pitches") {
        CHECK_THROWS_AS(makePaste(song, copyMeasures(song, 0, 1, 3, 1), 0, 0),
                        std::invalid_argument);
        CHECK_THROWS_AS(makePaste(song, copyMeasures(song, 0, 1, 0, 1), 0, 3),
                        std::invalid_argument);
    }
    SECTION("every copied track needs a track to go to") {
        CHECK_THROWS_AS(makePaste(song, copyMeasures(song, 0, 1), 0, 1), std::out_of_range);
    }
    SECTION("at a measure the song has, or its end") {
        CHECK_THROWS_AS(makePaste(song, copyMeasures(song, 0, 1, 0, 1), 5), std::out_of_range);
        CHECK_THROWS_AS(makePaste(song, MeasureClip{}, 0), std::out_of_range);
    }
    SECTION("a staff the target lacks is left behind") {
        Song keys;
        keys.addTrack(preset("piano"));
        keys.addTrack(preset("flute"));
        keys.appendMeasures(1);
        keys.track(0).staff(0).measure(0) = wholeNote(Note{.pitch = Pitch::parse("E5").value()});
        keys.track(0).staff(1).measure(0) = wholeNote(Note{.pitch = Pitch::parse("C3").value()});
        Document doc{Song(keys)};
        doc.execute(makePaste(doc.song(), copyMeasures(keys, 0, 1, 0, 1), 0, 1));
        CHECK(doc.song().track(1).staff(0).measure(0) == keys.track(0).staff(0).measure(0));
    }
}
