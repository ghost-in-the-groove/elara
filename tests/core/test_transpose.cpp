// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/transpose.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::ChordDiagram;
using elara::core::ChordSymbol;
using elara::core::Document;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::FretPosition;
using elara::core::Grace;
using elara::core::HarmonicKind;
using elara::core::Interval;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::makeTranspose;
using elara::core::Measure;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SharedOptional;
using elara::core::Song;
using elara::core::Step;
using elara::core::Track;
using elara::core::transposed;
using elara::core::transposedChordName;
using elara::core::transposedDiagram;
using elara::core::Transposition;
using elara::core::Tuning;

namespace {

Pitch parsed(const char* text) {
    return Pitch::parse(text).value();
}

KeySignature key(const char* text) {
    return KeySignature::parse(text).value();
}

std::string text(const std::optional<Pitch>& pitch) {
    return pitch ? pitch->toString() : "none";
}

Track preset(const char* id) {
    return Track::fromPreset(*findInstrumentPreset(id));
}

// A guitar, a flute and drums over two measures; the guitar's first measure
// holds one whole-note chord at these (string, fret) positions.
Song band(const std::vector<std::pair<int, int>>& chord) {
    Song song;
    song.addTrack(preset("electric-guitar"));
    song.addTrack(preset("flute"));
    song.addTrack(preset("drum-kit"));
    song.appendMeasures(2);
    Beat beat{.duration = Duration(NoteValue::Whole)};
    for (const auto& [string, fret] : chord) {
        beat.notes.push_back(song.track(0).staff(0).frettedNote(string, fret));
    }
    song.track(0).staff(0).measure(0).voices[0].beats.push_back(beat);
    song.track(1).staff(0).measure(1).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = parsed("C#5")}}});
    song.track(2).staff(0).measure(0).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = parsed("D2")}}});
    return song;
}

const std::vector<Note>& chordOf(const Song& song) {
    return song.track(0).staff(0).measure(0).voices[0].beats.at(0).notes;
}

std::vector<FretPosition> positionsOf(const Song& song) {
    std::vector<FretPosition> result;
    for (const Note& note : chordOf(song)) {
        result.push_back(note.position.value());
    }
    return result;
}

std::vector<std::string> pitchesOf(const Song& song) {
    std::vector<std::string> result;
    for (const Note& note : chordOf(song)) {
        result.push_back(note.pitch.toString());
    }
    return result;
}

// Applies a transposition through a document, so it can be undone.
Document transposedBand(const Song& song, Transposition what) {
    Document doc{Song(song)};
    doc.execute(makeTranspose(doc.song(), what));
    return doc;
}

FretPosition at(int string, int fret) {
    return {.string = static_cast<std::int8_t>(string), .fret = static_cast<std::int8_t>(fret)};
}

} // namespace

TEST_CASE("An interval in semitones goes to the key with the fewest accidentals",
          "[core][transpose]") {
    CHECK(Interval::fromSemitones(2) == Interval{.steps = 1, .semitones = 2});    // C -> D
    CHECK(Interval::fromSemitones(1) == Interval{.steps = 1, .semitones = 1});    // C -> Db
    CHECK(Interval::fromSemitones(6) == Interval{.steps = 4, .semitones = 6});    // C -> Gb
    CHECK(Interval::fromSemitones(-1) == Interval{.steps = -1, .semitones = -1}); // C -> B
    CHECK(Interval::fromSemitones(-2) == Interval{.steps = -1, .semitones = -2}); // C -> Bb
    CHECK(Interval::fromSemitones(5, key("G major")) == Interval{.steps = 3, .semitones = 5});
    CHECK(Interval::fromSemitones(1, key("E minor")) == Interval{.steps = 1, .semitones = 1});
    CHECK(Interval::fromSemitones(14) == Interval{.steps = 8, .semitones = 14});
    CHECK(Interval::fromSemitones(-13) == Interval{.steps = -8, .semitones = -13});

    SECTION("whole octaves keep the key, even one with seven sharps") {
        CHECK(Interval::fromSemitones(0, key("C# major")) == Interval{});
        CHECK(Interval::fromSemitones(12, key("C# major")) ==
              Interval{.steps = 7, .semitones = 12});
        CHECK(Interval::fromSemitones(-24) == Interval{.steps = -14, .semitones = -24});
    }
}

TEST_CASE("A transposed pitch keeps the degree of its spelling", "[core][transpose]") {
    const Interval majorSecond{.steps = 1, .semitones = 2};
    const Interval minorSecond{.steps = 1, .semitones = 1};
    CHECK(text(transposed(parsed("C#4"), majorSecond)) == "D#4");
    CHECK(text(transposed(parsed("Bb3"), majorSecond)) == "C4");
    CHECK(text(transposed(parsed("B3"), minorSecond)) == "C4");
    CHECK(text(transposed(parsed("E4"), minorSecond)) == "F4");
    CHECK(text(transposed(parsed("F#4"), minorSecond)) == "G4");
    CHECK(text(transposed(parsed("B#4"), majorSecond)) == "C##5");
    CHECK(text(transposed(parsed("D4"), Interval{.steps = -1, .semitones = -2})) == "C4");
    CHECK(text(transposed(parsed("C4"), Interval{.steps = -8, .semitones = -14})) == "Bb2");

    SECTION("beyond a double sharp, the MIDI number is spelled instead") {
        CHECK(text(transposed(parsed("C##4"), Interval{.steps = 1, .semitones = 3})) == "F4");
        CHECK(text(transposed(parsed("Dbb4"), Interval{.steps = -1, .semitones = -3})) == "A3");
    }
    SECTION("outside the MIDI range there is no pitch") {
        CHECK(text(transposed(parsed("G9"), majorSecond)) == "none");
        CHECK(text(transposed(parsed("C-1"), Interval{.steps = -1, .semitones = -1})) == "none");
    }
}

TEST_CASE("A transposed key moves its tonic", "[core][transpose]") {
    const Interval majorSecond{.steps = 1, .semitones = 2};
    CHECK(transposed(key("C major"), majorSecond) == key("D major"));
    CHECK(transposed(key("A minor"), majorSecond) == key("B minor"));
    CHECK(transposed(key("F major"), Interval{.steps = -1, .semitones = -2}) == key("Eb major"));
    CHECK(transposed(key("B major"), Interval{.steps = 1, .semitones = 1}) == key("C major"));
    CHECK(transposed(key("Eb minor"), Interval{.steps = 4, .semitones = 7}) == key("Bb minor"));

    SECTION("past seven accidentals, its enharmonic twin") {
        // C# major up an augmented unison would be C## major: D major instead.
        CHECK(transposed(key("C# major"), Interval{.steps = 0, .semitones = 1}) == key("D major"));
        CHECK(transposed(key("Cb major"), Interval{.steps = 0, .semitones = -1}) ==
              key("Bb major"));
    }
}

TEST_CASE("Transposing a song moves its pitched notes and keys as one undo step",
          "[core][transpose]") {
    // G2 on the low E string, D3 on the A string, E4 open.
    const Song song = band({{6, 3}, {5, 5}, {1, 0}});
    Document doc = transposedBand(song, {.semitones = 2});

    CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"A2", "E3", "F#4"});
    CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 5), at(5, 7), at(1, 2)});
    CHECK(doc.song().track(1).staff(0).measure(1).voices[0].beats[0].notes[0].pitch ==
          parsed("D#5"));
    CHECK(doc.song().track(2).staff(0).measure(0).voices[0].beats[0].notes[0].pitch ==
          parsed("D2")); // drums do not move
    CHECK(doc.song().measureHeader(0).keySignature == key("D major"));
    CHECK(doc.song().measureHeader(1).keySignature == key("D major"));
    CHECK(doc.undoName() == "Transpose");
    CHECK(doc.undoCount() == 1);

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("A transposed chord name moves its root and its bass", "[core][transpose]") {
    const Interval second{.steps = 1, .semitones = 2};
    CHECK(transposedChordName("G7", second) == "A7");
    CHECK(transposedChordName("Am", second) == "Bm");
    CHECK(transposedChordName("Bb7/F", second) == "C7/G");
    CHECK(transposedChordName("C6/9", second) == "D6/9"); // 9 is no bass
    CHECK(transposedChordName("F#m7b5", second) == "G#m7b5");
    CHECK(transposedChordName("N.C.", second) == "N.C.");
    CHECK(transposedChordName("", second).empty());

    // A diminished fifth: G flat would become D double flat, written C.
    const Interval fifth{.steps = 4, .semitones = 6};
    CHECK(transposedChordName("G7", fifth) == "Db7");
    CHECK(transposedChordName("Gb/Bb", fifth) == "C/E");
    CHECK(transposedChordName("Bb/D", fifth) == "Fb/Ab"); // spelled as the interval says
    // An augmented fourth from D sharp would be G double sharp.
    const Interval fourth{.steps = 3, .semitones = 6};
    CHECK(transposedChordName("D#m", fourth) == "Am"); // G double sharp, written A
}

TEST_CASE("A transposed chord diagram moves along the neck", "[core][transpose]") {
    constexpr std::int8_t x = ChordDiagram::kMuted;
    const ChordDiagram am{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}};

    CHECK(transposedDiagram(am, 2) == ChordDiagram{.firstFret = 1, .frets = {2, 3, 4, 4, 2, x}});
    CHECK(transposedDiagram(am, 5) == ChordDiagram{.firstFret = 5, .frets = {5, 6, 7, 7, 5, x}});
    CHECK(transposedDiagram(am, 12) ==
          ChordDiagram{.firstFret = 12, .frets = {12, 13, 14, 14, 12, x}});
    // Below the nut, an octave up instead.
    CHECK(transposedDiagram(am, -2) ==
          ChordDiagram{.firstFret = 10, .frets = {10, 11, 12, 12, 10, x}});

    // Past the last fret, an octave down; a capo leaves fewer frets.
    const ChordDiagram high{.firstFret = 26, .frets = {x, 27, 28, 28, 26, x}};
    CHECK(transposedDiagram(high, 1) ==
          ChordDiagram{.firstFret = 27, .frets = {x, 28, 29, 29, 27, x}});
    CHECK(transposedDiagram(high, 1, Tuning::kMaxFret - 3) ==
          ChordDiagram{.firstFret = 15, .frets = {x, 16, 17, 17, 15, x}});
    CHECK(transposedDiagram(high, 5) ==
          ChordDiagram{.firstFret = 19, .frets = {x, 20, 21, 21, 19, x}});

    // Only muted strings: nothing to move.
    const ChordDiagram muted{.firstFret = 1, .frets = {x, x, x, x, x, x}};
    CHECK(transposedDiagram(muted, 3) == muted);
}

TEST_CASE("Chord symbols move with the notes", "[core][transpose]") {
    constexpr std::int8_t x = ChordDiagram::kMuted;
    // A minor: the open A string, frets 2, 2 and 1, the open high E.
    Song song = band({{5, 0}, {4, 2}, {3, 2}, {2, 1}, {1, 0}});
    song.track(0).staff(0).measure(0).voices[0].beats[0].chord = SharedOptional(ChordSymbol{
        .name = "Am", .diagram = ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}}});
    // A flute's chord symbol has no diagram.
    song.track(1).staff(0).measure(1).voices[0].beats[0].chord =
        SharedOptional(ChordSymbol{.name = "C#dim", .diagram = std::nullopt});

    Document doc = transposedBand(song, {.semitones = 2});
    const Beat& guitar = doc.song().track(0).staff(0).measure(0).voices[0].beats[0];
    REQUIRE(guitar.chord);
    CHECK(guitar.chord->name == "Bm");
    CHECK(guitar.chord->diagram == ChordDiagram{.firstFret = 1, .frets = {2, 3, 4, 4, 2, x}});
    CHECK(positionsOf(doc.song()) ==
          std::vector<FretPosition>{at(5, 2), at(4, 4), at(3, 4), at(2, 3), at(1, 2)});
    const Beat& flute = doc.song().track(1).staff(0).measure(1).voices[0].beats[0];
    REQUIRE(flute.chord);
    CHECK(flute.chord->name == "D#dim");
    CHECK_FALSE(flute.chord->diagram);
    CHECK(doc.undoCount() == 1);

    SECTION("not on the tracks that stay") {
        Document part = transposedBand(song, {.semitones = 2, .tracks = {1}});
        CHECK(part.song().track(0).staff(0).measure(0).voices[0].beats[0].chord->name == "Am");
        CHECK(part.song().track(1).staff(0).measure(1).voices[0].beats[0].chord->name == "D#dim");
    }

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("Grace notes move with the notes they lead into", "[core][transpose]") {
    // A slide from fret 5 into G3 on the D string (ADR-0079).
    Song song = band({{4, 5}});
    Beat& beat = song.track(0).staff(0).measure(0).voices[0].beats[0];
    beat.graces = {Grace{.duration = Duration(NoteValue::Eighth),
                         .notes = {song.track(0).staff(0).frettedNote(4, 3)}}};
    Document doc = transposedBand(song, {.semitones = 2});

    const Grace& grace = doc.song().track(0).staff(0).measure(0).voices[0].beats[0].graces.at(0);
    CHECK(grace.notes.at(0).pitch == parsed("G3"));
    CHECK(grace.notes.at(0).position == at(4, 5));
    CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(4, 7)});
}

TEST_CASE("Transposing part of a song", "[core][transpose]") {
    const Song song = band({{6, 3}});

    SECTION("some measures") {
        Document doc = transposedBand(song, {.semitones = -1, .first = 1, .count = 1});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"G2"});
        CHECK(doc.song().track(1).staff(0).measure(1).voices[0].beats[0].notes[0].pitch ==
              parsed("B#4"));
        CHECK(doc.song().measureHeader(0).keySignature == key("C major"));
        CHECK(doc.song().measureHeader(1).keySignature == key("B major"));
    }
    SECTION("some tracks leave the keys alone") {
        Document doc = transposedBand(song, {.semitones = 2, .tracks = {0}});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"A2"});
        CHECK(doc.song().track(1).staff(0).measure(1).voices[0].beats[0].notes[0].pitch ==
              parsed("C#5"));
        CHECK(doc.song().measureHeader(0).keySignature == key("C major"));
    }
    SECTION("every pitched track still moves the keys") {
        Document doc = transposedBand(song, {.semitones = 2, .tracks = {1, 0}});
        CHECK(doc.song().measureHeader(0).keySignature == key("D major"));
    }
    SECTION("a missing track or measure is refused") {
        CHECK_THROWS_AS(makeTranspose(song, {.semitones = 2, .tracks = {3}}), std::out_of_range);
        CHECK_THROWS_AS(makeTranspose(song, {.semitones = 2, .first = 2}), std::out_of_range);
    }
}

TEST_CASE("A fretted note off the neck moves to the nearest free string", "[core][transpose]") {
    SECTION("below the open string, to a lower one") {
        // E4 open down a semitone is D#4: fret 4 on the B string.
        Document doc = transposedBand(band({{1, 0}}), {.semitones = -1});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"D#4"});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(2, 4)});
    }
    SECTION("past a string its chord holds, to the next") {
        // With the B string taken, D#4 goes to the G string, fret 8.
        Document doc = transposedBand(band({{1, 0}, {2, 3}}), {.semitones = -1});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"D#4", "C#4"});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(3, 8), at(2, 2)});
    }
    SECTION("above the last fret, to a higher one") {
        Document doc = transposedBand(band({{2, 29}}), {.semitones = 3});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(1, 27)});
    }
    SECTION("no string can play it") {
        CHECK_THROWS_AS(makeTranspose(band({{6, 0}}), {.semitones = -1}), std::invalid_argument);
        CHECK_THROWS_AS(makeTranspose(band({{1, 29}}), {.semitones = 3}), std::invalid_argument);
    }
}

TEST_CASE("A tied or hammered note follows the string its note moved to", "[core][transpose]") {
    Song song = band({{1, 0}});
    Note tied = song.track(0).staff(0).frettedNote(1, 0);
    tied.effects.tiedFromPrevious = true;
    song.track(0).staff(0).measure(1).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Half), .notes = {tied}});
    Note hammered = song.track(0).staff(0).frettedNote(1, 2);
    hammered.effects.legatoFromPrevious = true;
    song.track(0).staff(0).measure(1).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Half), .notes = {hammered}});

    // Down a semitone the open E moves to the B string; the tie and the
    // hammer-on stay with it, though F#4 down a semitone fits on its own string.
    Document doc = transposedBand(song, {.semitones = -1});
    const auto& beats = doc.song().track(0).staff(0).measure(1).voices[0].beats;
    CHECK(beats[0].notes[0].position == at(2, 4));
    CHECK(beats[1].notes[0].position == at(2, 6));
    CHECK(beats[1].notes[0].pitch == parsed("E#4"));
}

TEST_CASE("A natural harmonic keeps ringing at its node", "[core][transpose]") {
    Song song = band({{6, 12}});
    Note& harmonic = song.track(0).staff(0).measure(0).voices[0].beats[0].notes[0];
    harmonic.effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = 120};

    SECTION("on the string that many semitones above, when there is one") {
        Document doc = transposedBand(song, {.semitones = 5});
        const Note& note = chordOf(doc.song())[0];
        CHECK(note.position == at(5, 12));
        CHECK(note.pitch == parsed("A3"));
        CHECK(note.effects.harmonic.kind == HarmonicKind::Natural);
    }
    SECTION("otherwise as an artificial harmonic over the fretted note") {
        // E2's octave harmonic up a whole tone: F#3, an octave over F#2 on
        // fret 2 of the same string.
        Document doc = transposedBand(song, {.semitones = 2});
        const Note& note = chordOf(doc.song())[0];
        CHECK(note.position == at(6, 2));
        CHECK(note.pitch.midiNumber() == parsed("F#2").midiNumber());
        CHECK(note.effects.harmonic.kind == HarmonicKind::Artificial);
        CHECK(note.effects.harmonic.fretTenths == 120);
    }
}
