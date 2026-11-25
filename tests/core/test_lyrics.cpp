// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/lyrics.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::placeVerse;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Syllable;
using elara::core::syllablesOf;
using elara::core::Track;

namespace {

// Syllables as text: a hyphen after a syllable joined to the next one, "_" for
// a beat left blank.
std::vector<std::string> spelled(std::string_view text) {
    std::vector<std::string> out;
    for (const std::optional<Syllable>& syllable : syllablesOf(text)) {
        out.push_back(!syllable ? "_" : syllable->text + (syllable->hyphen ? "-" : ""));
    }
    return out;
}

Beat note(int midi, bool tied = false) {
    Note n{.pitch = Pitch::fromMidi(midi)};
    n.effects.tiedFromPrevious = tied;
    return Beat{.duration = Duration(NoteValue::Quarter), .notes = {n}};
}

Beat rest() {
    return Beat{.duration = Duration(NoteValue::Quarter)};
}

// Measure 1: a rest, C, D tied from C, E; measure 2: F, G.
Song melody() {
    Song song;
    song.addTrack(
        Track("Voice", Instrument{.name = "Voice", .kind = InstrumentKind::Pitched}, {Staff{}}));
    song.appendMeasures(2);
    Staff& staff = song.track(0).staff(0);
    staff.measure(0).voices[0].beats = {rest(), note(60), note(60, true), note(64)};
    staff.measure(1).voices[0].beats = {note(65), note(67)};
    staff.measure(1).voices[1].beats = {note(48)};
    return song;
}

std::vector<std::string> verse(const Staff& staff, std::size_t number) {
    std::vector<std::string> out;
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        for (const Beat& beat : staff.measure(m).voices[0].beats) {
            const bool has =
                beat.lyrics && number < beat.lyrics->size() && !(*beat.lyrics)[number].text.empty();
            out.push_back(has ? (*beat.lyrics)[number].text +
                                    ((*beat.lyrics)[number].hyphen ? "-" : "")
                              : "");
        }
    }
    return out;
}

} // namespace

TEST_CASE("Lyrics text: syllables change at spaces and hyphens", "[core][lyrics]") {
    CHECK(spelled("I see the ache be-hind your stare") ==
          std::vector<std::string>{"I", "see", "the", "ache", "be-", "hind", "your", "stare"});
    // "+" keeps two words on one beat.
    CHECK(spelled("a ga+in") == std::vector<std::string>{"a", "ga in"});
    CHECK(spelled("") == std::vector<std::string>{});
    CHECK(spelled("  \n ") == std::vector<std::string>{});
}

TEST_CASE("Lyrics text: more spaces or hyphens leave beats blank", "[core][lyrics]") {
    CHECK(spelled("go  now") == std::vector<std::string>{"go", "_", "now"});
    CHECK(spelled("sha--dows") == std::vector<std::string>{"sha-", "_", "dows"});
    CHECK(spelled("sha- dows") == std::vector<std::string>{"sha-", "_", "dows"});
    // Spaces before the first syllable wait for as many beats.
    CHECK(spelled("  in") == std::vector<std::string>{"_", "_", "in"});
    // Nothing waits after the last one.
    CHECK(spelled("end   ") == std::vector<std::string>{"end"});
}

TEST_CASE("Lyrics text: a line break is a space unless one is next to it", "[core][lyrics]") {
    CHECK(spelled("stare\nA storm") == std::vector<std::string>{"stare", "A", "storm"});
    CHECK(spelled("stare \nA") == std::vector<std::string>{"stare", "A"});
    CHECK(spelled("stare\r\n\r\n\r\nA") == std::vector<std::string>{"stare", "A"});
    CHECK(spelled("go  \nnow") == std::vector<std::string>{"go", "_", "now"});
}

TEST_CASE("Lyrics text: words in brackets are comments", "[core][lyrics]") {
    CHECK(spelled("[Verse 1]\nI see\n\n[Chorus]\nThe cure") ==
          std::vector<std::string>{"I", "see", "The", "cure"});
    // With the spaces around it, a comment leaves no beat blank.
    CHECK(spelled("[Verse] Sing [softly] a-long") ==
          std::vector<std::string>{"Sing", "a-", "long"});
    CHECK(spelled("be-[x]hind") == std::vector<std::string>{"be-", "hind"});
}

TEST_CASE("A verse goes on the beats with notes, past rests and tied notes", "[core][lyrics]") {
    Song song = melody();
    Staff& staff = song.track(0).staff(0);
    CHECK(placeVerse(staff, 0, 0, "one two three four five six") == 2);
    CHECK(verse(staff, 0) == std::vector<std::string>{"", "one", "", "two", "three", "four"});
    // Other voices sing nothing.
    CHECK(!staff.measure(1).voices[1].beats[0].lyrics);
    // The rest and the tied note hold no lyrics at all.
    CHECK(!staff.measure(0).voices[0].beats[0].lyrics);
    CHECK(!staff.measure(0).voices[0].beats[2].lyrics);
}

TEST_CASE("A verse starts at its measure and replaces what the verse held there",
          "[core][lyrics]") {
    Song song = melody();
    Staff& staff = song.track(0).staff(0);
    placeVerse(staff, 0, 0, "one two three four");
    CHECK(placeVerse(staff, 1, 1, "be-low") == 0);
    CHECK(verse(staff, 1) == std::vector<std::string>{"", "", "", "", "be-", "low"});
    CHECK(staff.measure(1).voices[0].beats[0].lyrics->size() == 2);

    // Verse 1 again from measure 2, with a blank: measure 1 keeps its syllables.
    CHECK(placeVerse(staff, 0, 1, "-la") == 0);
    CHECK(verse(staff, 0) == std::vector<std::string>{"", "one", "", "two", "", "la"});
    CHECK(verse(staff, 1) == std::vector<std::string>{"", "", "", "", "be-", "low"});

    // An empty verse takes the lyrics away; a beat with none left holds none.
    placeVerse(staff, 1, 0, "");
    placeVerse(staff, 0, 0, "");
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        for (const Beat& beat : staff.measure(m).voices[0].beats) {
            CHECK(!beat.lyrics);
        }
    }
}
