// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace elara::core;

namespace {

const Duration kQuarter(NoteValue::Quarter);
const Duration kHalf(NoteValue::Half);
const Duration kWhole(NoteValue::Whole);

/// A guitar in 4/4 over three measures: two quarter notes in the first (one
/// short of the measure), a whole note in the second and nothing in the third.
Song guitarSong() {
    Song song;
    song.appendMeasures(3);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    Staff& staff = song.track(0).staff(0);
    staff.measure(0).voices[0].beats = {
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(6, 3)}},
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(5, 2)}}};
    staff.measure(1).voices[0].beats = {
        Beat{.duration = kWhole, .notes = {staff.frettedNote(1, 0)}}};
    return song;
}

Caret at(std::size_t measure, std::size_t beat, int string = 1) {
    return Caret{.measure = measure, .beat = beat, .string = string};
}

const Voice& firstVoice(const Song& song, std::size_t measure) {
    return song.track(0).staff(0).measure(measure).voices[0];
}

bool hasNote(const Beat& beat, const Note& note) {
    return std::ranges::find(beat.notes, note) != beat.notes.end();
}

/// Executes an entry that must change the song and returns its caret.
Caret run(Document& doc, Entry entry) {
    REQUIRE(entry.command != nullptr);
    doc.execute(std::move(entry.command));
    return entry.caret;
}

} // namespace

// ---- Moves --------------------------------------------------------------------------

TEST_CASE("Entry caret: right walks the beats, the free slot of a short measure, then on",
          "[core][entry]") {
    const Song song = guitarSong();
    Caret caret = at(0, 0, 3);
    const auto right = [&] {
        caret = moveCaret(song, caret, CaretMove::NextBeat);
        return caret;
    };
    CHECK(right() == at(0, 1, 3));
    CHECK(right() == at(0, 2, 3)); // the free slot after two quarters
    CHECK(onFreeSlot(song, caret));
    CHECK(right() == at(1, 0, 3));
    // A full measure has no free slot; an empty one has it at 0.
    CHECK(right() == at(2, 0, 3));
    CHECK(onFreeSlot(song, caret));
    CHECK(right() == at(2, 0, 3)); // the end of the song
}

TEST_CASE("Entry caret: left is the reverse of right", "[core][entry]") {
    const Song song = guitarSong();
    Caret caret = at(2, 0);
    const auto left = [&] {
        caret = moveCaret(song, caret, CaretMove::PreviousBeat);
        return caret;
    };
    CHECK(left() == at(1, 0));
    CHECK(left() == at(0, 2));
    CHECK(left() == at(0, 1));
    CHECK(left() == at(0, 0));
    CHECK(left() == at(0, 0)); // the start of the song
}

TEST_CASE("Entry caret: measure and string moves stop at the ends", "[core][entry]") {
    const Song song = guitarSong();
    CHECK(moveCaret(song, at(0, 1, 2), CaretMove::NextMeasure) == at(1, 0, 2));
    CHECK(moveCaret(song, at(2, 0), CaretMove::NextMeasure) == at(2, 0));
    CHECK(moveCaret(song, at(0, 1), CaretMove::PreviousMeasure) == at(0, 0));
    CHECK(moveCaret(song, at(1, 0), CaretMove::PreviousMeasure) == at(0, 0));
    CHECK(moveCaret(song, at(0, 0, 1), CaretMove::StringUp) == at(0, 0, 1));
    CHECK(moveCaret(song, at(0, 0, 1), CaretMove::StringDown) == at(0, 0, 2));
    CHECK(moveCaret(song, at(0, 0, 6), CaretMove::StringDown) == at(0, 0, 6));
    CHECK(moveCaret(song, at(0, 0, 4), CaretMove::StringUp) == at(0, 0, 3));
}

TEST_CASE("Entry caret: a caret is clamped back into the song", "[core][entry]") {
    Song song = guitarSong();
    CHECK(clampCaret(
              song,
              Caret{.track = 5, .staff = 3, .measure = 9, .voice = 7, .beat = 9, .string = 9}) ==
          Caret{.track = 0, .staff = 0, .measure = 2, .voice = 3, .beat = 0, .string = 6});
    CHECK(clampCaret(song, at(0, 7, 0)) == at(0, 2, 1));
    CHECK(clampCaret(song, at(1, 4)) == at(1, 0)); // a full measure: its last beat
    // A staff without a tuning has one line of entry.
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    CHECK(clampCaret(song, Caret{.track = 1, .staff = 1, .string = 4}) ==
          Caret{.track = 1, .staff = 1, .string = 1});
    CHECK_THROWS_AS(clampCaret(Song{}, Caret{}), std::invalid_argument);
}

// ---- Edits --------------------------------------------------------------------------

TEST_CASE("Entry: a fret on the free slot adds a beat, on a beat it sets the string's note",
          "[core][entry]") {
    Document doc(guitarSong());
    const Staff& staff = doc.song().track(0).staff(0);

    Entry entry = enterFret(doc.song(), at(0, 2, 3), 2, kHalf);
    REQUIRE(entry.command != nullptr);
    CHECK(entry.command->name() == "Enter note");
    CHECK(run(doc, std::move(entry)) == at(0, 2, 3));
    const Voice& voice = firstVoice(doc.song(), 0);
    REQUIRE(voice.beats.size() == 3);
    CHECK(voice.beats[2] == Beat{.duration = kHalf, .notes = {staff.frettedNote(3, 2)}});

    // Another string joins the chord; the same string is replaced, keeping its
    // effects; the beat keeps its duration whatever the input duration is.
    (void)run(doc, enterFret(doc.song(), at(0, 2, 1), 0, kQuarter));
    // Chords keep their notes from the highest string down.
    CHECK(firstVoice(doc.song(), 0).beats[2].notes ==
          std::vector<Note>{staff.frettedNote(1, 0), staff.frettedNote(3, 2)});
    {
        Measure accented = staff.measure(0);
        accented.voices[0].beats[2].notes[1].effects.accent = Accent::Accent;
        doc.execute(std::make_unique<ReplaceMeasure>(0, 0, 0, std::move(accented)));
    }
    (void)run(doc, enterFret(doc.song(), at(0, 2, 3), 4, kQuarter));
    const Beat& chord = firstVoice(doc.song(), 0).beats[2];
    CHECK(chord.duration == kHalf);
    REQUIRE(chord.notes.size() == 2);
    Note accented = staff.frettedNote(3, 4);
    accented.effects.accent = Accent::Accent;
    CHECK(hasNote(chord, accented));
    CHECK(hasNote(chord, staff.frettedNote(1, 0)));

    // Undo goes back one keystroke at a time.
    doc.undo();
    CHECK(firstVoice(doc.song(), 0).beats[2].notes.size() == 2);
    CHECK(hasNote(firstVoice(doc.song(), 0).beats[2], staff.frettedNote(1, 0)));
    doc.undo();
    doc.undo();
    doc.undo();
    CHECK(firstVoice(doc.song(), 0).beats.size() == 2);
}

TEST_CASE("Entry: a fret on a rest makes a note of the rest's duration, spelled in the key",
          "[core][entry]") {
    Song song = guitarSong();
    song.measureHeader(2).keySignature = KeySignature(-1, KeyMode::Major); // F major
    song.track(0).staff(0).measure(2).voices[0].beats = {Beat{.duration = kHalf}};
    Document doc(std::move(song));
    (void)run(doc, enterFret(doc.song(), at(2, 0, 3), 3, kQuarter));
    const Beat& beat = firstVoice(doc.song(), 2).beats[0];
    CHECK(beat.duration == kHalf);
    REQUIRE(beat.notes.size() == 1);
    // G string, third fret: B-flat in F major, not A-sharp.
    CHECK(beat.notes[0].pitch == Pitch(Step::B, -1, 3));
    CHECK(beat.notes[0].position == FretPosition{.string = 3, .fret = 3});
}

TEST_CASE("Entry: frets need a tuning and a neck", "[core][entry]") {
    Song song = guitarSong();
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    CHECK_THROWS_AS(enterFret(song, Caret{.track = 1}, 3, kQuarter), std::logic_error);
    CHECK_THROWS_AS(enterFret(song, at(0, 0, 1), 99, kQuarter), std::out_of_range);
    CHECK_THROWS_AS(enterFret(song, at(0, 0, 1), -1, kQuarter), std::out_of_range);
}

TEST_CASE("Entry: a beat's duration changes, the free slot has none", "[core][entry]") {
    Document doc(guitarSong());
    Entry entry = setBeatDuration(doc.song(), at(0, 1), kHalf);
    REQUIRE(entry.command != nullptr);
    CHECK(entry.command->name() == "Change duration");
    CHECK(run(doc, std::move(entry)) == at(0, 1));
    CHECK(firstVoice(doc.song(), 0).beats[1].duration == kHalf);
    // The same duration changes nothing, and the free slot has none.
    CHECK(setBeatDuration(doc.song(), at(0, 1), kHalf).command == nullptr);
    CHECK(setBeatDuration(doc.song(), at(2, 0), kHalf).command == nullptr);
}

TEST_CASE("Entry: delete removes the string's note, then the rest", "[core][entry]") {
    Document doc(guitarSong());
    // No note on string 1 of the second beat: nothing to delete.
    CHECK(deleteAtCaret(doc.song(), at(0, 1, 1)).command == nullptr);
    Entry entry = deleteAtCaret(doc.song(), at(0, 1, 5));
    REQUIRE(entry.command != nullptr);
    CHECK(entry.command->name() == "Delete");
    CHECK(run(doc, std::move(entry)) == at(0, 1, 5));
    CHECK(firstVoice(doc.song(), 0).beats[1] == Beat{.duration = kQuarter});
    // On the rest, the beat goes and the caret is on the free slot.
    CHECK(run(doc, deleteAtCaret(doc.song(), at(0, 1, 5))) == at(0, 1, 5));
    CHECK(firstVoice(doc.song(), 0).beats.size() == 1);
    CHECK(onFreeSlot(doc.song(), at(0, 1, 5)));
    CHECK(deleteAtCaret(doc.song(), at(0, 1, 5)).command == nullptr);
}

TEST_CASE("Entry: a whole beat goes, and a rest is inserted before the caret", "[core][entry]") {
    Document doc(guitarSong());
    Entry removed = deleteBeat(doc.song(), at(0, 0, 2));
    REQUIRE(removed.command != nullptr);
    CHECK(removed.command->name() == "Delete beat");
    CHECK(run(doc, std::move(removed)) == at(0, 0, 2));
    REQUIRE(firstVoice(doc.song(), 0).beats.size() == 1);
    CHECK(deleteBeat(doc.song(), at(0, 1)).command == nullptr); // the free slot

    Entry inserted = insertRest(doc.song(), at(0, 0, 2), kHalf);
    REQUIRE(inserted.command != nullptr);
    CHECK(inserted.command->name() == "Insert rest");
    CHECK(run(doc, std::move(inserted)) == at(0, 0, 2));
    const Voice& voice = firstVoice(doc.song(), 0);
    REQUIRE(voice.beats.size() == 2);
    CHECK(voice.beats[0] == Beat{.duration = kHalf});
    CHECK(voice.beats[1].notes.size() == 1);
    // On the free slot the rest is added at the end.
    CHECK(run(doc, insertRest(doc.song(), at(0, 2), kQuarter)) == at(0, 2));
    CHECK(firstVoice(doc.song(), 0).beats.size() == 3);
}
