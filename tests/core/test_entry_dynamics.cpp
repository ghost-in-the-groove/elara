// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace elara::core;

namespace {

const Duration kQuarter(NoteValue::Quarter);
const Duration kHalf(NoteValue::Half);

Note note(const char* pitch, Dynamic dynamic = Dynamic::MF, bool tied = false) {
    Note result{.pitch = Pitch::parse(pitch).value()};
    result.effects.dynamic = dynamic;
    result.effects.tiedFromPrevious = tied;
    return result;
}

Beat quarter(Note played) {
    return Beat{.duration = kQuarter, .notes = {std::move(played)}};
}

/// A flute over two measures of four quarter notes, at these dynamics.
Song flute(const std::vector<Dynamic>& first, const std::vector<Dynamic>& second) {
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("flute")));
    const std::vector<const char*> pitches{"C5", "D5", "E5", "F5"};
    for (std::size_t m = 0; m < 2; ++m) {
        const auto& dynamics = m == 0 ? first : second;
        for (std::size_t b = 0; b < dynamics.size(); ++b) {
            song.track(0).staff(0).measure(m).voices[0].beats.push_back(
                quarter(note(pitches[b], dynamics[b])));
        }
    }
    return song;
}

Caret at(std::size_t measure, std::size_t beat, std::size_t voice = 0) {
    return Caret{.measure = measure, .voice = voice, .beat = beat};
}

// The dynamic of every note of a voice of a measure, beat by beat.
std::vector<std::string> dynamicsOf(const Song& song, std::size_t measure, std::size_t voice = 0) {
    constexpr std::array<const char*, 8> kNames{"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
    std::vector<std::string> result;
    for (const Beat& beat : song.track(0).staff(0).measure(measure).voices.at(voice).beats) {
        std::string names = beat.isRest() ? "rest" : "";
        for (const Note& played : beat.notes) {
            names += (names.empty() ? "" : " ");
            names += kNames[static_cast<std::size_t>(played.effects.dynamic)];
        }
        result.push_back(names);
    }
    return result;
}

using Names = std::vector<std::string>;

Document applied(const Song& song, Entry entry) {
    Document doc{Song(song)};
    REQUIRE(entry.command != nullptr);
    doc.execute(std::move(entry.command));
    return doc;
}

} // namespace

TEST_CASE("Dynamics: a dynamic holds from the caret's beat until the next change",
          "[core][entry][dynamics]") {
    using enum Dynamic;
    const Song song = flute({MF, MF, MF, MF}, {MF, F, F, MF});

    SECTION("inside a passage, a new one starts at the caret") {
        Entry entry = setDynamic(song, at(0, 2), P);
        CHECK(entry.caret == at(0, 2));
        Document doc = applied(song, std::move(entry));
        CHECK(dynamicsOf(doc.song(), 0) == Names{"mf", "mf", "p", "p"});
        CHECK(dynamicsOf(doc.song(), 1) == Names{"p", "f", "f", "mf"});
        CHECK(doc.undoName() == "Change dynamic");
        CHECK(doc.undoCount() == 1);
        doc.undo();
        CHECK(doc.song() == song);
    }
    SECTION("where one starts, it changes that passage") {
        Document doc = applied(song, setDynamic(song, at(1, 1), FF));
        CHECK(dynamicsOf(doc.song(), 0) == Names{"mf", "mf", "mf", "mf"});
        CHECK(dynamicsOf(doc.song(), 1) == Names{"mf", "ff", "ff", "mf"});
    }
    SECTION("to the end of the song when nothing changes after it") {
        Document doc = applied(song, setDynamic(song, at(1, 3), PPP));
        CHECK(dynamicsOf(doc.song(), 1) == Names{"mf", "f", "f", "ppp"});
    }
}

TEST_CASE("Dynamics: on a rest, the notes after it", "[core][entry][dynamics]") {
    using enum Dynamic;
    Song song = flute({MF, MF, MF, MF}, {MF});
    song.track(0).staff(0).measure(0).voices[0].beats[1].notes.clear();
    Document doc = applied(song, setDynamic(song, at(0, 1), F));
    CHECK(dynamicsOf(doc.song(), 0) == Names{"mf", "rest", "f", "f"});
    CHECK(dynamicsOf(doc.song(), 1) == Names{"f"});
}

TEST_CASE("Dynamics: every voice of the staff, tied notes with the note they continue",
          "[core][entry][dynamics]") {
    using enum Dynamic;
    // Voice 2 holds a half note tied to another; the passage is mf throughout.
    Song song = flute({MF, MF, MF, MF}, {MF});
    song.track(0).staff(0).measure(0).voices[1].beats = {
        Beat{.duration = kHalf, .notes = {note("C4")}},
        Beat{.duration = kHalf, .notes = {note("C4", MF, true)}}};

    SECTION("from the start, both voices and the tie") {
        Document doc = applied(song, setDynamic(song, at(0, 0), F));
        CHECK(dynamicsOf(doc.song(), 0, 0) == Names{"f", "f", "f", "f"});
        CHECK(dynamicsOf(doc.song(), 0, 1) == Names{"f", "f"});
        CHECK(dynamicsOf(doc.song(), 1) == Names{"f"});
    }
    SECTION("a tie continuing a note before the caret keeps its dynamic") {
        Document doc = applied(song, setDynamic(song, at(0, 2), PP));
        CHECK(dynamicsOf(doc.song(), 0, 0) == Names{"mf", "mf", "pp", "pp"});
        CHECK(dynamicsOf(doc.song(), 0, 1) == Names{"mf", "mf"});
        CHECK(dynamicsOf(doc.song(), 1) == Names{"pp"});
    }
    SECTION("from the other voice's caret") {
        Document doc = applied(song, setDynamic(song, at(0, 1, 1), FF));
        CHECK(dynamicsOf(doc.song(), 0, 0) == Names{"mf", "mf", "ff", "ff"});
        CHECK(dynamicsOf(doc.song(), 0, 1) == Names{"mf", "mf"});
    }
    SECTION("a note held past the next change keeps the dynamic it was struck with") {
        // The passage from beat 1 ends where voice 1 plays f on beat 3; the
        // tied half note under it began inside the passage.
        Song held = song;
        auto& beats = held.track(0).staff(0).measure(0).voices[0].beats;
        beats[3].notes[0].effects.dynamic = F;
        auto& lower = held.track(0).staff(0).measure(0).voices[1].beats;
        lower = {Beat{.duration = kQuarter}, Beat{.duration = kHalf, .notes = {note("C4")}},
                 Beat{.duration = kQuarter, .notes = {note("C4", MF, true)}}};
        Document doc = applied(held, setDynamic(held, at(0, 1), P));
        CHECK(dynamicsOf(doc.song(), 0, 0) == Names{"mf", "p", "p", "f"});
        CHECK(dynamicsOf(doc.song(), 0, 1) == Names{"rest", "p", "p"});
        CHECK(dynamicsOf(doc.song(), 1) == Names{"mf"});
    }
}

TEST_CASE("Dynamics: nothing changes when the passage has it or no note follows",
          "[core][entry][dynamics]") {
    using enum Dynamic;
    const Song song = flute({MF, MF, F, F}, {});
    CHECK(setDynamic(song, at(0, 0), MF).command == nullptr);
    CHECK_FALSE(setDynamic(song, at(0, 0), MF).refused); // so already
    CHECK(setDynamic(song, at(0, 3), F).command == nullptr);
    CHECK_FALSE(setDynamic(song, at(0, 3), F).refused);
    // The free slot of an empty measure: no note follows to take it.
    const Entry none = setDynamic(song, at(1, 0), PP);
    CHECK(none.command == nullptr);
    CHECK(none.refused == Refusal::NothingAtCaret);
}

TEST_CASE("Dynamics: new notes take the dynamic in force at the caret", "[core][entry][dynamics]") {
    using enum Dynamic;
    const Song song = flute({PP, F}, {});

    SECTION("a note name after the passage") {
        Document doc = applied(song, enterPitch(song, at(0, 2), Step::G, kQuarter));
        CHECK(dynamicsOf(doc.song(), 0) == Names{"pp", "f", "f"});
    }
    SECTION("a note name over a beat takes the beat's") {
        Document doc = applied(song, enterPitch(song, at(0, 0), Step::G, kQuarter));
        CHECK(dynamicsOf(doc.song(), 0) == Names{"pp", "f"});
    }
    SECTION("into an empty measure, the measure before's") {
        Document doc = applied(song, enterPitch(song, at(1, 0), Step::G, kQuarter));
        CHECK(dynamicsOf(doc.song(), 1) == Names{"f"});
    }
    SECTION("the first note of a song is mf") {
        Song blank;
        blank.appendMeasures(1);
        blank.addTrack(Track::fromPreset(*findInstrumentPreset("flute")));
        Document doc = applied(blank, enterPitch(blank, at(0, 0), Step::G, kQuarter));
        CHECK(dynamicsOf(doc.song(), 0) == Names{"mf"});
    }
    SECTION("frets, chords and drum hits") {
        Song guitar;
        guitar.appendMeasures(1);
        guitar.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
        Staff& staff = guitar.track(0).staff(0);
        Note first = staff.frettedNote(1, 3);
        first.effects.dynamic = FF;
        staff.measure(0).voices[0].beats = {quarter(first), Beat{.duration = kQuarter}};
        Document next =
            applied(guitar, enterFret(guitar, Caret{.beat = 2, .string = 2}, 1, kQuarter));
        CHECK(dynamicsOf(next.song(), 0) == Names{"ff", "rest", "ff"});
        Document chord =
            applied(guitar, enterFret(guitar, Caret{.beat = 0, .string = 2}, 1, kQuarter));
        CHECK(dynamicsOf(chord.song(), 0) == Names{"ff ff", "rest"});
        Document rest =
            applied(guitar, enterFret(guitar, Caret{.beat = 1, .string = 2}, 1, kQuarter));
        CHECK(dynamicsOf(rest.song(), 0) == Names{"ff", "ff"});

        Song drums;
        drums.appendMeasures(1);
        drums.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
        Document hit = applied(drums, toggleDrum(drums, Caret{.beat = 0, .string = 3}, kQuarter));
        Note loud = hit.song().track(0).staff(0).measure(0).voices[0].beats[0].notes[0];
        loud.effects.dynamic = P;
        drums.track(0).staff(0).measure(0).voices[0].beats = {quarter(loud)};
        Document second =
            applied(drums, toggleDrum(drums, Caret{.beat = 1, .string = 3}, kQuarter));
        CHECK(dynamicsOf(second.song(), 0) == Names{"p", "p"});
    }
}
