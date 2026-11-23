// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/tuning.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using elara::core::Pitch;
using elara::core::Step;
using elara::core::Tuning;

TEST_CASE("Standard guitar tuning numbers strings from the highest", "[core][tuning]") {
    const Tuning guitar = Tuning::standardGuitar();
    REQUIRE(guitar.stringCount() == 6);
    CHECK(guitar.openString(1) == Pitch(Step::E, 0, 4));
    CHECK(guitar.openString(2) == Pitch(Step::B, 0, 3));
    CHECK(guitar.openString(3) == Pitch(Step::G, 0, 3));
    CHECK(guitar.openString(4) == Pitch(Step::D, 0, 3));
    CHECK(guitar.openString(5) == Pitch(Step::A, 0, 2));
    CHECK(guitar.openString(6) == Pitch(Step::E, 0, 2));
}

TEST_CASE("Standard bass tuning is an octave below the guitar's lowest four strings",
          "[core][tuning]") {
    const Tuning bass = Tuning::standardBass();
    REQUIRE(bass.stringCount() == 4);
    CHECK(bass.openString(1) == Pitch(Step::G, 0, 2));
    CHECK(bass.openString(4) == Pitch(Step::E, 0, 1));
    CHECK(bass.openString(4).midiNumber() == 28);
}

TEST_CASE("Tuning is built from open strings, highest first", "[core][tuning]") {
    const Tuning dropD({Pitch(Step::E, 0, 4), Pitch(Step::B, 0, 3), Pitch(Step::G, 0, 3),
                        Pitch(Step::D, 0, 3), Pitch(Step::A, 0, 2), Pitch(Step::D, 0, 2)});
    CHECK(dropD.stringCount() == 6);
    CHECK(dropD.openString(6) == Pitch(Step::D, 0, 2));
    CHECK(dropD != Tuning::standardGuitar());
    CHECK(Tuning::standardGuitar() == Tuning::standardGuitar());
}

TEST_CASE("Tuning rejects no strings, too many strings and bad string numbers", "[core][tuning]") {
    CHECK_THROWS_AS(Tuning({}), std::invalid_argument);
    const std::vector<Pitch> tooMany(Tuning::kMaxStrings + 1, Pitch(Step::E, 0, 2));
    CHECK_THROWS_AS(Tuning(tooMany), std::invalid_argument);
    const Tuning guitar = Tuning::standardGuitar();
    CHECK_THROWS_AS(guitar.openString(0), std::out_of_range);
    CHECK_THROWS_AS(guitar.openString(7), std::out_of_range);
}

TEST_CASE("A fretted note sounds open string + capo + fret", "[core][tuning]") {
    const Tuning guitar = Tuning::standardGuitar();
    CHECK(guitar.midiAt(6, 0) == 40);  // E2
    CHECK(guitar.midiAt(6, 5) == 45);  // A2, same as the open fifth string
    CHECK(guitar.midiAt(1, 12) == 76); // E5
    CHECK(guitar.midiAt(2, 1) == 60);  // C4 (middle C)
    // Tablature frets are written relative to the capo: with a capo at 2,
    // "fret 0" on the sixth string sounds F#2.
    CHECK(guitar.midiAt(6, 0, 2) == 42);
    CHECK(guitar.midiAt(3, 2, 2) == 59);
}

TEST_CASE("Fret and capo outside the neck are rejected", "[core][tuning]") {
    const Tuning guitar = Tuning::standardGuitar();
    CHECK_NOTHROW(guitar.midiAt(1, Tuning::kMaxFret));
    CHECK_THROWS_AS(guitar.midiAt(1, -1), std::out_of_range);
    CHECK_THROWS_AS(guitar.midiAt(1, Tuning::kMaxFret + 1), std::out_of_range);
    CHECK_THROWS_AS(guitar.midiAt(1, 0, -1), std::out_of_range);
    CHECK_THROWS_AS(guitar.midiAt(1, 20, Tuning::kMaxFret - 10), std::out_of_range);
    CHECK_THROWS_AS(guitar.midiAt(0, 3), std::out_of_range);
}

TEST_CASE("Every string/fret position that sounds a pitch", "[core][tuning]") {
    using elara::core::FretPosition;
    const Tuning guitar = Tuning::standardGuitar();
    CHECK(guitar.positionsFor(Pitch(Step::E, 0, 4), 0, 24) ==
          std::vector<FretPosition>{{.string = 1, .fret = 0},
                                    {.string = 2, .fret = 5},
                                    {.string = 3, .fret = 9},
                                    {.string = 4, .fret = 14},
                                    {.string = 5, .fret = 19},
                                    {.string = 6, .fret = 24}});
    CHECK(guitar.positionsFor(Pitch(Step::C, 0, 4), 0, 12) ==
          std::vector<FretPosition>{
              {.string = 2, .fret = 1}, {.string = 3, .fret = 5}, {.string = 4, .fret = 10}});
    // Spelling does not matter, only the sound.
    CHECK(guitar.positionsFor(Pitch(Step::B, 1, 3), 0, 12) ==
          guitar.positionsFor(Pitch(Step::C, 0, 4), 0, 12));
    // Below the capo there is nothing to play; at the capo it is fret 0.
    CHECK(guitar.positionsFor(Pitch(Step::E, 0, 2), 2).empty());
    CHECK(guitar.positionsFor(Pitch(Step::F, 1, 2), 2) ==
          std::vector<FretPosition>{{.string = 6, .fret = 0}});
    CHECK(guitar.positionsFor(Pitch(Step::C, 0, 8)).empty()); // above the neck
}

TEST_CASE("Tuning text lists strings from the lowest tab line to string 1", "[core][tuning]") {
    CHECK(Tuning::standardGuitar().toString() == "E2 A2 D3 G3 B3 E4");
    CHECK(Tuning::standardBass().toString() == "E1 A1 D2 G2");
    CHECK(Tuning::parse("E2 A2 D3 G3 B3 E4") == Tuning::standardGuitar());
    CHECK(Tuning::parse("  E1  A1 D2\tG2 ") == Tuning::standardBass());
    const auto dropD = Tuning::parse("D2 A2 D3 G3 B3 E4");
    REQUIRE(dropD.has_value());
    CHECK(dropD->openString(6) == Pitch(Step::D, 0, 2));
    CHECK_FALSE(Tuning::parse("").has_value());
    CHECK_FALSE(Tuning::parse("E2 A2 X3").has_value());
    CHECK_FALSE(Tuning::parse("E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2").has_value()); // 13
}

TEST_CASE("Tuning presets by name", "[core][tuning]") {
    CHECK(Tuning::preset("guitar") == Tuning::standardGuitar());
    CHECK(Tuning::preset("bass") == Tuning::standardBass());
    CHECK(Tuning::preset("guitar-drop-d")->toString() == "D2 A2 D3 G3 B3 E4");
    CHECK(Tuning::preset("guitar-7")->toString() == "B1 E2 A2 D3 G3 B3 E4");
    CHECK(Tuning::preset("bass-5")->toString() == "B0 E1 A1 D2 G2");
    // Re-entrant: string 4 (G4) is higher than string 3 (C4).
    const auto ukulele = Tuning::preset("ukulele");
    REQUIRE(ukulele.has_value());
    CHECK(ukulele->openString(1) == Pitch(Step::A, 0, 4));
    CHECK(ukulele->openString(4) == Pitch(Step::G, 0, 4));
    CHECK_FALSE(Tuning::preset("theremin").has_value());
    for (const auto name : Tuning::presetNames()) {
        CHECK(Tuning::preset(name).has_value());
    }
    CHECK(Tuning::presetNames().size() >= 10);
}
