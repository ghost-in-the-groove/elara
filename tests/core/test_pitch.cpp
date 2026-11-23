// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/pitch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stdexcept>

using elara::core::Pitch;
using elara::core::Step;

TEST_CASE("Pitch maps its spelling to a MIDI number (C4 = 60)", "[core][pitch]") {
    CHECK(Pitch(Step::C, 0, 4).midiNumber() == 60);
    CHECK(Pitch(Step::A, 0, 4).midiNumber() == 69);
    CHECK(Pitch(Step::E, 0, 2).midiNumber() == 40); // low E string of a guitar
    CHECK(Pitch(Step::E, 0, 1).midiNumber() == 28); // low E string of a bass
    CHECK(Pitch(Step::F, 1, 3).midiNumber() == 54);
    CHECK(Pitch(Step::B, -1, 2).midiNumber() == 46);
    CHECK(Pitch(Step::C, 0, -1).midiNumber() == 0);
    CHECK(Pitch(Step::G, 0, 9).midiNumber() == 127);
    CHECK(Pitch{}.midiNumber() == 60); // default is middle C
}

TEST_CASE("Enharmonic spellings share a MIDI number but are different pitches", "[core][pitch]") {
    const Pitch bSharp(Step::B, 1, 3);
    const Pitch c(Step::C, 0, 4);
    const Pitch dDoubleFlat(Step::D, -2, 4);
    CHECK(bSharp.midiNumber() == 60); // B#3 crosses into the next octave number
    CHECK(dDoubleFlat.midiNumber() == 60);
    CHECK(bSharp != c);
    CHECK(bSharp.isEnharmonicWith(c));
    CHECK(c.isEnharmonicWith(dDoubleFlat));
    CHECK_FALSE(c.isEnharmonicWith(Pitch(Step::C, 1, 4)));
    CHECK(Pitch(Step::C, -1, 4).midiNumber() == 59); // Cb4 is B3
}

TEST_CASE("Pitch rejects unsupported alterations and notes outside MIDI", "[core][pitch]") {
    CHECK_NOTHROW(Pitch(Step::F, Pitch::kMaxAlter, 4));
    CHECK_NOTHROW(Pitch(Step::B, Pitch::kMinAlter, 4));
    CHECK_THROWS_AS(Pitch(Step::F, 3, 4), std::invalid_argument);
    CHECK_THROWS_AS(Pitch(Step::B, -3, 4), std::invalid_argument);
    CHECK_THROWS_AS(Pitch(Step::C, -1, -1), std::invalid_argument); // MIDI -1
    CHECK_THROWS_AS(Pitch(Step::G, 1, 9), std::invalid_argument);   // MIDI 128
}

TEST_CASE("Pitch spells a MIDI number with sharps or flats", "[core][pitch]") {
    using elara::core::SpellingPreference;
    CHECK(Pitch::fromMidi(60) == Pitch(Step::C, 0, 4));
    CHECK(Pitch::fromMidi(61) == Pitch(Step::C, 1, 4)); // sharps by default
    CHECK(Pitch::fromMidi(61, SpellingPreference::Flats) == Pitch(Step::D, -1, 4));
    CHECK(Pitch::fromMidi(70, SpellingPreference::Flats) == Pitch(Step::B, -1, 4));
    CHECK(Pitch::fromMidi(66) == Pitch(Step::F, 1, 4));
    CHECK(Pitch::fromMidi(64, SpellingPreference::Flats) == Pitch(Step::E, 0, 4));
    CHECK(Pitch::fromMidi(0) == Pitch(Step::C, 0, -1));
    CHECK(Pitch::fromMidi(127) == Pitch(Step::G, 0, 9));
    CHECK(Pitch::fromMidi(59) == Pitch(Step::B, 0, 3));
    for (int midi = 0; midi <= 127; ++midi) {
        CHECK(Pitch::fromMidi(midi).midiNumber() == midi);
        CHECK(Pitch::fromMidi(midi, SpellingPreference::Flats).midiNumber() == midi);
    }
    CHECK_THROWS_AS(Pitch::fromMidi(-1), std::invalid_argument);
    CHECK_THROWS_AS(Pitch::fromMidi(128), std::invalid_argument);
}

TEST_CASE("Pitch writes itself as text", "[core][pitch]") {
    CHECK(Pitch(Step::C, 0, 4).toString() == "C4");
    CHECK(Pitch(Step::F, 1, 3).toString() == "F#3");
    CHECK(Pitch(Step::B, -1, 2).toString() == "Bb2");
    CHECK(Pitch(Step::C, 2, 4).toString() == "C##4");
    CHECK(Pitch(Step::D, -2, 4).toString() == "Dbb4");
    CHECK(Pitch(Step::C, 0, -1).toString() == "C-1");
}

TEST_CASE("Pitch parses text and rejects malformed input", "[core][pitch]") {
    CHECK(Pitch::parse("C4") == Pitch(Step::C, 0, 4));
    CHECK(Pitch::parse("f#3") == Pitch(Step::F, 1, 3)); // step is case-insensitive
    CHECK(Pitch::parse("Bb2") == Pitch(Step::B, -1, 2));
    CHECK(Pitch::parse("bb2") == Pitch(Step::B, -1, 2));
    CHECK(Pitch::parse("Cx4") == Pitch(Step::C, 2, 4)); // x is a double sharp
    CHECK(Pitch::parse("C##4") == Pitch(Step::C, 2, 4));
    CHECK(Pitch::parse("Ebb5") == Pitch(Step::E, -2, 5));
    CHECK(Pitch::parse("C-1") == Pitch(Step::C, 0, -1));
    CHECK(Pitch::parse("G9") == Pitch(Step::G, 0, 9));

    CHECK_FALSE(Pitch::parse("").has_value());
    CHECK_FALSE(Pitch::parse("H4").has_value());
    CHECK_FALSE(Pitch::parse("C").has_value());
    CHECK_FALSE(Pitch::parse("C#").has_value());
    CHECK_FALSE(Pitch::parse("C###4").has_value());
    CHECK_FALSE(Pitch::parse("C#b4").has_value());
    CHECK_FALSE(Pitch::parse("C4x").has_value());
    CHECK_FALSE(Pitch::parse(" C4").has_value());
    CHECK_FALSE(Pitch::parse("C--1").has_value());
    CHECK_FALSE(Pitch::parse("G#9").has_value()); // MIDI 128
    CHECK_FALSE(Pitch::parse("C10").has_value());
}

TEST_CASE("Every MIDI pitch survives a text round trip", "[core][pitch]") {
    using elara::core::SpellingPreference;
    for (int midi = 0; midi <= 127; ++midi) {
        for (const auto preference : {SpellingPreference::Sharps, SpellingPreference::Flats}) {
            const Pitch pitch = Pitch::fromMidi(midi, preference);
            CHECK(Pitch::parse(pitch.toString()) == pitch);
        }
    }
}

TEST_CASE("Pitch frequency follows equal temperament around A4", "[core][pitch]") {
    using Catch::Matchers::WithinRel;
    CHECK(Pitch(Step::A, 0, 4).frequency() == 440.0);
    CHECK(Pitch(Step::A, 0, 3).frequency() == 220.0);
    CHECK(Pitch(Step::A, 0, 5).frequency() == 880.0);
    CHECK_THAT(Pitch(Step::C, 0, 4).frequency(), WithinRel(261.6255653, 1e-9));
    CHECK_THAT(Pitch(Step::E, 0, 2).frequency(), WithinRel(82.4068892, 1e-9));
    CHECK(Pitch(Step::A, 0, 4).frequency(432.0) == 432.0); // other reference pitch
    CHECK(Pitch(Step::B, 1, 3).frequency() == Pitch(Step::C, 0, 4).frequency());
}
