// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/retune.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Document;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::FretPosition;
using elara::core::HarmonicKind;
using elara::core::KeySignature;
using elara::core::makeRetune;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Retune;
using elara::core::Retuning;
using elara::core::Song;
using elara::core::Track;
using elara::core::Tuning;

namespace {

Tuning tuning(const char* name) {
    return Tuning::preset(name).value();
}

Track preset(const char* id) {
    return Track::fromPreset(*findInstrumentPreset(id));
}

// A guitar (or the instrument `id`) and a flute over two measures; each of the
// guitar's measures holds one whole-note chord at these (string, fret) positions.
Song band(const std::vector<std::vector<std::pair<int, int>>>& chords,
          const char* id = "electric-guitar") {
    Song song;
    song.addTrack(preset(id));
    song.addTrack(preset("flute"));
    song.appendMeasures(chords.size());
    for (std::size_t m = 0; m < chords.size(); ++m) {
        Beat beat{.duration = Duration(NoteValue::Whole)};
        for (const auto& [string, fret] : chords[m]) {
            beat.notes.push_back(song.track(0).staff(0).frettedNote(string, fret));
        }
        song.track(0).staff(0).measure(m).voices[0].beats.push_back(beat);
    }
    return song;
}

const std::vector<Note>& chordOf(const Song& song, std::size_t measure = 0) {
    return song.track(0).staff(0).measure(measure).voices[0].beats.at(0).notes;
}

std::vector<FretPosition> positionsOf(const Song& song, std::size_t measure = 0) {
    std::vector<FretPosition> result;
    for (const Note& note : chordOf(song, measure)) {
        result.push_back(note.position.value());
    }
    return result;
}

std::vector<std::string> pitchesOf(const Song& song, std::size_t measure = 0) {
    std::vector<std::string> result;
    for (const Note& note : chordOf(song, measure)) {
        result.push_back(note.pitch.toString());
    }
    return result;
}

FretPosition at(int string, int fret) {
    return {.string = static_cast<std::int8_t>(string), .fret = static_cast<std::int8_t>(fret)};
}

// Applies a retuning through a document, so it can be undone.
Document retuned(const Song& song, const Retuning& what) {
    Document doc{Song(song)};
    doc.execute(makeRetune(doc.song(), what));
    return doc;
}

} // namespace

TEST_CASE("Retuning a staff keeps its pitches and moves its frets as one undo step",
          "[core][retune]") {
    // A2 at the fifth fret of the low E string, E2 open, D3 on the A string.
    const Song song = band({{{6, 5}, {5, 5}}, {{6, 0}}});
    Document doc = retuned(song, {.track = 0, .tuning = tuning("guitar-drop-d")});

    CHECK(doc.song().track(0).staff(0).tuning() == tuning("guitar-drop-d"));
    CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"A2", "D3"});
    CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 7), at(5, 5)});
    CHECK(positionsOf(doc.song(), 1) == std::vector<FretPosition>{at(6, 2)});
    CHECK(doc.undoName() == "Change tuning");
    CHECK(doc.undoCount() == 1);

    doc.undo();
    CHECK(doc.song() == song);
}

TEST_CASE("A capo keeps the pitches by playing them lower on the neck", "[core][retune]") {
    const Song song = band({{{6, 5}, {1, 3}}});
    Document doc = retuned(song, {.track = 0, .tuning = tuning("guitar"), .capo = 2});

    CHECK(doc.song().track(0).staff(0).capo() == 2);
    CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"A2", "G4"});
    CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 3), at(1, 1)});

    SECTION("and taking it off plays them higher again") {
        doc.execute(makeRetune(doc.song(), {.track = 0, .tuning = tuning("guitar")}));
        CHECK(doc.song() == song);
    }
}

TEST_CASE("Retuning moves notes off missing or unreachable strings", "[core][retune]") {
    SECTION("a string the new tuning lacks") {
        // E2 at the fifth fret of a seven-string's low B is the six-string's open E.
        Document doc = retuned(band({{{7, 5}, {1, 0}}}, "seven-string-guitar"),
                               {.track = 0, .tuning = tuning("guitar")});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"E2", "E4"});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 0), at(1, 0)});
    }
    SECTION("a note below its string's new open pitch") {
        // Open E4 under a capo at the first fret goes to the B string.
        Document doc =
            retuned(band({{{1, 0}}}), {.track = 0, .tuning = tuning("guitar"), .capo = 1});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(2, 4)});
    }
    SECTION("a note no string can play rejects the retuning") {
        // Nothing plays the open low E under a capo.
        CHECK_THROWS_AS(
            makeRetune(band({{{6, 0}}}), {.track = 0, .tuning = tuning("guitar"), .capo = 2}),
            std::invalid_argument);
    }
}

TEST_CASE("Retuning can keep the frets and change the pitches", "[core][retune]") {
    // G2 and B2 on the two lowest strings; in drop D the low string plays F2.
    const Song song = band({{{6, 3}, {5, 2}}, {{6, 4}}});

    SECTION("spelled in the key") {
        Document doc =
            retuned(song, {.track = 0, .tuning = tuning("guitar-drop-d"), .keep = Retune::Frets});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 3), at(5, 2)});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"F2", "B2"});
        CHECK(pitchesOf(doc.song(), 1) == std::vector<std::string>{"F#2"});
        CHECK(doc.undoCount() == 1);
        doc.undo();
        CHECK(doc.song() == song);
    }
    SECTION("in a flat key, with flats") {
        Song flat = song;
        flat.measureHeader(1).keySignature = KeySignature::parse("Bb major").value();
        Document doc =
            retuned(flat, {.track = 0, .tuning = tuning("guitar-drop-d"), .keep = Retune::Frets});
        CHECK(pitchesOf(doc.song(), 1) == std::vector<std::string>{"Gb2"});
    }
    SECTION("a capo raises every note") {
        Document doc = retuned(
            song, {.track = 0, .tuning = tuning("guitar"), .capo = 2, .keep = Retune::Frets});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 3), at(5, 2)});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"A2", "C#3"});
    }
    SECTION("a natural harmonic stays on its node") {
        Song harmonic = band({{{6, 12}}});
        Note& note = harmonic.track(0).staff(0).measure(0).voices[0].beats[0].notes[0];
        note.effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = 120};
        Document doc = retuned(
            harmonic, {.track = 0, .tuning = tuning("guitar-drop-d"), .keep = Retune::Frets});
        CHECK(positionsOf(doc.song()) == std::vector<FretPosition>{at(6, 12)});
        CHECK(pitchesOf(doc.song()) == std::vector<std::string>{"D3"});
        CHECK(chordOf(doc.song())[0].effects.harmonic.kind == HarmonicKind::Natural);
    }
    SECTION("a note on a string the new tuning lacks rejects it") {
        CHECK_THROWS_AS(makeRetune(band({{{7, 0}}}, "seven-string-guitar"),
                                   {.track = 0, .tuning = tuning("guitar"), .keep = Retune::Frets}),
                        std::invalid_argument);
    }
    SECTION("a fret past the neck under the capo rejects it") {
        CHECK_THROWS_AS(
            makeRetune(band({{{1, 29}}}),
                       {.track = 0, .tuning = tuning("guitar"), .capo = 2, .keep = Retune::Frets}),
            std::invalid_argument);
    }
}

TEST_CASE("A retuning that changes nothing is no edit", "[core][retune]") {
    const Song song = band({{{6, 3}}});
    CHECK(makeRetune(song, {.track = 0, .tuning = tuning("guitar")}) == nullptr);
    CHECK(makeRetune(song, {.track = 0, .tuning = tuning("guitar"), .keep = Retune::Frets}) ==
          nullptr);
}

TEST_CASE("Only an existing fretted staff with a capo on the neck can be retuned",
          "[core][retune]") {
    const Song song = band({{{6, 3}}});
    CHECK_THROWS_AS(makeRetune(song, {.track = 1, .tuning = tuning("guitar")}),
                    std::invalid_argument); // the flute has no strings
    CHECK_THROWS_AS(makeRetune(song, {.track = 2, .tuning = tuning("guitar")}), std::out_of_range);
    CHECK_THROWS_AS(makeRetune(song, {.track = 0, .staff = 1, .tuning = tuning("guitar")}),
                    std::out_of_range);
    CHECK_THROWS_AS(makeRetune(song, {.track = 0, .tuning = tuning("guitar"), .capo = 31}),
                    std::invalid_argument);
    CHECK_THROWS_AS(makeRetune(song, {.track = 0, .tuning = tuning("guitar"), .capo = -1}),
                    std::invalid_argument);
}
