// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
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

TEST_CASE("Entry: grace notes lead into the caret's beat", "[core][entry]") {
    Document doc(guitarSong());
    const Duration eighth(NoteValue::Eighth);
    const Duration sixteenth(NoteValue::Sixteenth);
    // The A string's second fret: a whole tone below before it, a semitone
    // above on it (ADR-0079).
    run(doc, addGraceNote(doc.song(), at(0, 1, 5), -2, eighth, false));
    CHECK(doc.undoName() == "Add grace note");
    run(doc, addGraceNote(doc.song(), at(0, 1, 5), 1, sixteenth, true));
    const Staff& staff = doc.song().track(0).staff(0);
    const Beat& beat = firstVoice(doc.song(), 0).beats[1];
    REQUIRE(beat.graces.size() == 2);
    CHECK(beat.graces[0] == Grace{.duration = eighth, .notes = {staff.frettedNote(5, 0)}});
    CHECK(beat.graces[1] ==
          Grace{.duration = sixteenth, .notes = {staff.frettedNote(5, 3)}, .onBeat = true});
    CHECK(graceNotesAt(doc.song(), at(0, 1, 5)) == 2);
    CHECK(graceNotesAt(doc.song(), at(0, 0, 6)) == 0);
    CHECK(graceNotesAt(doc.song(), at(0, 2)) == 0); // the free slot

    // Nothing on a string without a note or the free slot; off the neck throws.
    CHECK(addGraceNote(doc.song(), at(0, 1, 1), -2, eighth, false).command == nullptr);
    CHECK(addGraceNote(doc.song(), at(0, 2, 5), -2, eighth, false).command == nullptr);
    CHECK_THROWS_AS(addGraceNote(doc.song(), at(0, 1, 5), -3, eighth, false), std::out_of_range);

    run(doc, removeGraceNotes(doc.song(), at(0, 1, 5)));
    CHECK(doc.undoName() == "Remove grace notes");
    CHECK(firstVoice(doc.song(), 0).beats[1].graces.empty());
    CHECK(removeGraceNotes(doc.song(), at(0, 1, 5)).command == nullptr);
    doc.undo();
    CHECK(graceNotesAt(doc.song(), at(0, 1, 5)) == 2);
}

TEST_CASE("Entry: chord symbols name the caret's beat, with the diagram its frets make",
          "[core][entry]") {
    constexpr std::int8_t x = ChordDiagram::kMuted;
    // An imported chord, its diagram kept when it is renamed.
    const SharedOptional<ChordSymbol> imported(ChordSymbol{
        .name = "D", .diagram = ChordDiagram{.firstFret = 1, .frets = {2, 3, 2, 0, x, x}}});
    Song song = guitarSong();
    song.track(0).staff(0).measure(1).voices[0].beats[0].chord = imported;
    Document doc(std::move(song));
    // One note: a name without a diagram, spaces around it dropped.
    run(doc, setChordSymbol(doc.song(), at(0, 0), "  G "));
    CHECK(doc.undoName() == "Set chord symbol");
    CHECK(*firstVoice(doc.song(), 0).beats[0].chord == ChordSymbol{.name = "G", .diagram = {}});
    CHECK(chordNameAt(doc.song(), at(0, 0)) == "G");
    CHECK(setChordSymbol(doc.song(), at(0, 0), "G").command == nullptr);

    // Two or more fretted notes make the diagram, from the nut while they
    // stay within four frets, otherwise from the lowest fret.
    Song chords = doc.song();
    Staff& written = chords.track(0).staff(0);
    written.measure(2).voices[0].beats = {
        Beat{.duration = kHalf,
             .notes = {written.frettedNote(5, 0), written.frettedNote(4, 2),
                       written.frettedNote(3, 2), written.frettedNote(2, 1),
                       written.frettedNote(1, 0)}},
        Beat{.duration = kHalf,
             .notes = {written.frettedNote(5, 7), written.frettedNote(4, 9),
                       written.frettedNote(3, 9), written.frettedNote(2, 8)}}};
    Document both(std::move(chords));
    run(both, setChordSymbol(both.song(), at(2, 0), "Am"));
    CHECK(*firstVoice(both.song(), 2).beats[0].chord ==
          ChordSymbol{.name = "Am",
                      .diagram = ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}}});
    run(both, setChordSymbol(both.song(), at(2, 1), "Em/B"));
    CHECK(*firstVoice(both.song(), 2).beats[1].chord ==
          ChordSymbol{.name = "Em/B",
                      .diagram = ChordDiagram{.firstFret = 7, .frets = {x, 8, 9, 9, 7, x}}});

    // Renaming a beat whose notes make no diagram keeps the one it had; a rest
    // takes a name; an empty name takes the symbol away; the free slot never.
    run(doc, setChordSymbol(doc.song(), at(1, 0), "D/F#"));
    CHECK(firstVoice(doc.song(), 1).beats[0].chord->diagram == imported->diagram);
    CHECK(firstVoice(doc.song(), 1).beats[0].chord->name == "D/F#");
    run(both, setChordSymbol(both.song(), at(0, 1), "N.C."));
    run(doc, setChordSymbol(doc.song(), at(0, 0), " "));
    CHECK(doc.undoName() == "Remove chord symbol");
    CHECK_FALSE(firstVoice(doc.song(), 0).beats[0].chord);
    CHECK(chordNameAt(doc.song(), at(0, 0)).empty());
    CHECK(setChordSymbol(doc.song(), at(0, 0), "").command == nullptr);
    CHECK(setChordSymbol(doc.song(), at(0, 2), "C").command == nullptr);
    CHECK(chordNameAt(doc.song(), at(0, 2)).empty());
    doc.undo();
    CHECK(chordNameAt(doc.song(), at(0, 0)) == "G");
}

TEST_CASE("Entry: chord symbols on staves without frets have no diagram", "[core][entry]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    Document doc(std::move(song));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::E, kQuarter, PitchMode::AddToChord));
    run(doc, setChordSymbol(doc.song(), at(0, 0), "C"));
    CHECK(*firstVoice(doc.song(), 0).beats[0].chord == ChordSymbol{.name = "C", .diagram = {}});
    const Caret drums{.track = 1, .string = 2};
    run(doc, toggleDrum(doc.song(), drums, kQuarter));
    run(doc, setChordSymbol(doc.song(), drums, "C"));
    CHECK_FALSE(doc.song().track(1).staff(0).measure(0).voices[0].beats[0].chord->diagram);
}

TEST_CASE("Entry: a grace note leads from a piano's top note and doubles a drum hit",
          "[core][entry]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    Document doc(std::move(song));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::E, kQuarter, PitchMode::AddToChord));
    run(doc, addGraceNote(doc.song(), at(0, 0), 2, Duration(NoteValue::Eighth), false));
    const Beat& chord = firstVoice(doc.song(), 0).beats[0];
    REQUIRE(chord.graces.size() == 1);
    const int top = std::ranges::max(chord.notes, {}, [](const Note& note) {
                        return note.pitch.midiNumber();
                    }).pitch.midiNumber();
    CHECK(chord.graces[0].notes.front().pitch.midiNumber() == top + 2);

    // On a drum staff the grace note is the lane's hit again: a flam.
    const Caret drums{.track = 1, .string = 2};
    run(doc, toggleDrum(doc.song(), drums, kQuarter));
    run(doc, addGraceNote(doc.song(), drums, 5, Duration(NoteValue::Sixteenth), false));
    const Beat& hit = doc.song().track(1).staff(0).measure(0).voices[0].beats[0];
    REQUIRE(hit.graces.size() == 1);
    CHECK(hit.graces[0].notes == hit.notes);
}

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

// ---- Note names ---------------------------------------------------------------------

namespace {

/// A flute in 4/4 over two empty measures.
Song fluteSong() {
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("flute")));
    return song;
}

} // namespace

TEST_CASE("Entry: a note name on the free slot adds the nearest such note", "[core][entry]") {
    Document doc(fluteSong());
    Entry entry = enterPitch(doc.song(), at(0, 0), Step::C, kQuarter);
    REQUIRE(entry.command != nullptr);
    CHECK(entry.command->name() == "Enter note");
    CHECK(run(doc, std::move(entry)) == at(0, 0));
    // Nothing before it: the C nearest the middle line of the treble staff (B4).
    const Voice& voice = firstVoice(doc.song(), 0);
    REQUIRE(voice.beats.size() == 1);
    CHECK(voice.beats[0] ==
          Beat{.duration = kQuarter, .notes = {Note{.pitch = Pitch(Step::C, 0, 5)}}});
}

TEST_CASE("Entry: each note name follows the previous note, across measures", "[core][entry]") {
    Document doc(fluteSong());
    Caret caret = at(0, 0);
    const auto type = [&](Step step) {
        caret = run(doc, enterPitch(doc.song(), caret, step, kQuarter));
        const Beat& beat = firstVoice(doc.song(), caret.measure).beats[caret.beat];
        caret = moveCaret(doc.song(), caret, CaretMove::NextBeat);
        return beat.notes.at(0).pitch;
    };
    CHECK(type(Step::C) == Pitch(Step::C, 0, 5));
    CHECK(type(Step::E) == Pitch(Step::E, 0, 5)); // a third up, not a sixth down
    CHECK(type(Step::A) == Pitch(Step::A, 0, 5)); // a fourth up, not a fifth down
    CHECK(type(Step::G) == Pitch(Step::G, 0, 5)); // a step down, not a sixth up
    // The next measure looks back at the last note of this one.
    REQUIRE(caret == at(1, 0));
    CHECK(type(Step::B) == Pitch(Step::B, 0, 5));
    // A rest in between is skipped.
    REQUIRE(run(doc, insertRest(doc.song(), caret, kQuarter)) == at(1, 1));
    caret = at(1, 2);
    CHECK(type(Step::G) == Pitch(Step::G, 0, 5));
}

TEST_CASE("Entry: a note name takes the key's accidental and keeps a rest's duration",
          "[core][entry]") {
    Song song = fluteSong();
    song.measureHeader(0).keySignature = KeySignature(2, KeyMode::Major); // D major
    song.track(0).staff(0).measure(0).voices[0].beats = {Beat{.duration = kHalf}};
    Document doc(std::move(song));
    REQUIRE(run(doc, enterPitch(doc.song(), at(0, 0), Step::F, kQuarter)) == at(0, 0));
    const Beat& beat = firstVoice(doc.song(), 0).beats[0];
    CHECK(beat.duration == kHalf);
    // Three steps below the middle line (B4), not four above.
    CHECK(beat.notes == std::vector<Note>{Note{.pitch = Pitch(Step::F, 1, 4)}});
    // The same name again changes nothing; another replaces the note.
    CHECK(enterPitch(doc.song(), at(0, 0), Step::F, kQuarter).command == nullptr);
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    CHECK(firstVoice(doc.song(), 0).beats[0].notes ==
          std::vector<Note>{Note{.pitch = Pitch(Step::C, 1, 5)}});
}

TEST_CASE("Entry: a transposing instrument takes note names as it is written", "[core][entry]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("trumpet")));
    song.measureHeader(0).keySignature = KeySignature(-1, KeyMode::Major); // F, written in G
    Document doc(std::move(song));
    // A written B (on the middle line) sounds A; a written F sharp sounds E.
    run(doc, enterPitch(doc.song(), at(0, 0), Step::B, kQuarter));
    run(doc, enterPitch(doc.song(), at(0, 1), Step::F, kQuarter));
    const Voice& voice = firstVoice(doc.song(), 0);
    REQUIRE(voice.beats.size() == 2);
    CHECK(voice.beats[0].notes.at(0).pitch == Pitch(Step::A, 0, 4));
    CHECK(voice.beats[1].notes.at(0).pitch == Pitch(Step::E, 0, 4));
}

TEST_CASE("Entry: on tablature a note name goes on the caret's string or the nearest one",
          "[core][entry]") {
    Document doc(guitarSong());
    // The last note before measure 2 is E4 (written E5): A is a fourth up, A4.
    Entry onCaret = enterPitch(doc.song(), at(2, 0, 3), Step::A, kQuarter);
    CHECK(onCaret.caret == at(2, 0, 3));
    run(doc, std::move(onCaret));
    CHECK(firstVoice(doc.song(), 2).beats[0].notes ==
          std::vector<Note>{doc.song().track(0).staff(0).frettedNote(3, 14)});
    // After B2 the A is A2, which the first string cannot reach: the fifth can.
    CHECK(run(doc, enterPitch(doc.song(), at(0, 2, 1), Step::A, kQuarter)) == at(0, 2, 5));
    CHECK(firstVoice(doc.song(), 0).beats[2].notes ==
          std::vector<Note>{doc.song().track(0).staff(0).frettedNote(5, 0)});
}

TEST_CASE("Entry: a note name added to a chord goes above its highest note", "[core][entry]") {
    Document doc(fluteSong());
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::E, kQuarter, PitchMode::AddToChord));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter, PitchMode::AddToChord));
    CHECK(firstVoice(doc.song(), 0).beats[0].notes ==
          std::vector<Note>{Note{.pitch = Pitch(Step::C, 0, 6)},
                            Note{.pitch = Pitch(Step::E, 0, 5)},
                            Note{.pitch = Pitch(Step::C, 0, 5)}});
    // On the free slot a chord note starts a beat.
    run(doc, enterPitch(doc.song(), at(0, 1), Step::G, kQuarter, PitchMode::AddToChord));
    CHECK(firstVoice(doc.song(), 0).beats[1].notes.size() == 1);

    // On tablature it takes the nearest string without a note: G4 over E4.
    Document guitar(guitarSong());
    CHECK(run(guitar, enterPitch(guitar.song(), at(1, 0, 1), Step::G, kQuarter,
                                 PitchMode::AddToChord)) == at(1, 0, 2));
    const Staff& staff = guitar.song().track(0).staff(0);
    CHECK(firstVoice(guitar.song(), 1).beats[0].notes ==
          std::vector<Note>{staff.frettedNote(1, 0), staff.frettedNote(2, 8)});
}

TEST_CASE("Entry: note names need a pitched staff and the MIDI range", "[core][entry]") {
    Song song = fluteSong();
    song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    CHECK_THROWS_AS(enterPitch(song, Caret{.track = 1}, Step::C, kQuarter), std::logic_error);
    // Up and up in a chord until the notes leave MIDI 127.
    Document doc(std::move(song));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    const auto addC = [&] {
        doc.execute(
            enterPitch(doc.song(), at(0, 0), Step::C, kQuarter, PitchMode::AddToChord).command);
    };
    addC(); // C6
    addC(); // C7
    addC(); // C8
    addC(); // C9 (MIDI 120)
    CHECK_THROWS_AS(addC(), std::out_of_range);
}

TEST_CASE("Entry: a pitch moves by semitones, a fretted note along its string", "[core][entry]") {
    Document doc(fluteSong());
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kQuarter));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::E, kQuarter, PitchMode::AddToChord));
    Entry up = shiftPitch(doc.song(), at(0, 0), 2);
    REQUIRE(up.command != nullptr);
    CHECK(up.command->name() == "Change pitch");
    run(doc, std::move(up));
    CHECK(firstVoice(doc.song(), 0).beats[0].notes ==
          std::vector<Note>{Note{.pitch = Pitch(Step::F, 1, 5)},
                            Note{.pitch = Pitch(Step::D, 0, 5)}});
    run(doc, shiftPitch(doc.song(), at(0, 0), -12));
    CHECK(firstVoice(doc.song(), 0).beats[0].notes.at(1).pitch == Pitch(Step::D, 0, 4));
    CHECK_THROWS_AS(shiftPitch(doc.song(), at(0, 0), 100), std::out_of_range);
    CHECK(shiftPitch(doc.song(), at(0, 1), 1).command == nullptr); // the free slot

    Song song = guitarSong();
    song.track(0).staff(0).measure(1).voices[0].beats[0].notes[0].effects.letRing = true;
    Document guitar(std::move(song));
    run(guitar, shiftPitch(guitar.song(), at(1, 0, 1), 3));
    Note expected = guitar.song().track(0).staff(0).frettedNote(1, 3);
    expected.effects.letRing = true;
    CHECK(firstVoice(guitar.song(), 1).beats[0].notes == std::vector<Note>{expected});
    CHECK(shiftPitch(guitar.song(), at(1, 0, 2), 1).command == nullptr); // no note on string 2
    CHECK_THROWS_AS(shiftPitch(guitar.song(), at(1, 0, 1), -4), std::out_of_range);
}

TEST_CASE("Entry: delete on a staff without strings leaves a rest", "[core][entry]") {
    Document doc(fluteSong());
    run(doc, enterPitch(doc.song(), at(0, 0), Step::C, kHalf));
    run(doc, enterPitch(doc.song(), at(0, 0), Step::E, kQuarter, PitchMode::AddToChord));
    run(doc, deleteAtCaret(doc.song(), at(0, 0)));
    CHECK(firstVoice(doc.song(), 0).beats == std::vector<Beat>{Beat{.duration = kHalf}});
}
