// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/signature.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using elara::core::Fraction;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Pitch;
using elara::core::Step;
using elara::core::TimeSignature;

TEST_CASE("Time signature gives the measure and beat lengths", "[core][signature]") {
    CHECK(TimeSignature{}.measureLength() == Fraction(1)); // 4/4 by default
    CHECK(TimeSignature(3, 4).measureLength() == Fraction(3, 4));
    CHECK(TimeSignature(6, 8).measureLength() == Fraction(3, 4));
    CHECK(TimeSignature(7, 8).measureLength() == Fraction(7, 8));
    CHECK(TimeSignature(2, 2).beatLength() == Fraction(1, 2));
    CHECK(TimeSignature(6, 8) != TimeSignature(3, 4)); // same length, different meter
}

TEST_CASE("Time signature counts dotted beats in compound meters", "[core][signature]") {
    CHECK(TimeSignature(6, 8).isCompound());
    CHECK(TimeSignature(9, 8).isCompound());
    CHECK(TimeSignature(12, 16).isCompound());
    CHECK(TimeSignature(6, 4).isCompound());
    CHECK_FALSE(TimeSignature(3, 8).isCompound()); // one beat of three would be a waltz in one
    CHECK_FALSE(TimeSignature(3, 4).isCompound());
    CHECK_FALSE(TimeSignature(4, 4).isCompound());
    CHECK_FALSE(TimeSignature(7, 8).isCompound());
    CHECK(TimeSignature(6, 8).pulseLength() == Fraction(3, 8));
    CHECK(TimeSignature(12, 8).pulseLength() == Fraction(3, 8));
    CHECK(TimeSignature(3, 4).pulseLength() == Fraction(1, 4));
    CHECK(TimeSignature(7, 8).pulseLength() == Fraction(1, 8));
    CHECK(TimeSignature(2, 2).pulseLength() == Fraction(1, 2));
}

TEST_CASE("Time signature validates and round-trips text", "[core][signature]") {
    CHECK_THROWS_AS(TimeSignature(0, 4), std::invalid_argument);
    CHECK_THROWS_AS(TimeSignature(TimeSignature::kMaxNumerator + 1, 4), std::invalid_argument);
    CHECK_THROWS_AS(TimeSignature(4, 3), std::invalid_argument); // not a power of two
    CHECK_THROWS_AS(TimeSignature(4, 128), std::invalid_argument);
    CHECK(TimeSignature(12, 8).toString() == "12/8");
    CHECK(TimeSignature::parse("5/4") == TimeSignature(5, 4));
    CHECK_FALSE(TimeSignature::parse("5/3").has_value());
    CHECK_FALSE(TimeSignature::parse("5").has_value());
    CHECK_FALSE(TimeSignature::parse("/4").has_value());
    CHECK_FALSE(TimeSignature::parse("5/4x").has_value());
}

TEST_CASE("Key signature knows its tonic and accidentals", "[core][signature]") {
    const KeySignature dMajor(2, KeyMode::Major);
    CHECK(dMajor.tonic() == Pitch(Step::D, 0, 4).step());
    CHECK(dMajor.tonicAlter() == 0);
    CHECK(dMajor.alterFor(Step::F) == 1);
    CHECK(dMajor.alterFor(Step::C) == 1);
    CHECK(dMajor.alterFor(Step::G) == 0);

    const KeySignature bFlatMinor(-5, KeyMode::Minor);
    CHECK(bFlatMinor.tonic() == Step::B);
    CHECK(bFlatMinor.tonicAlter() == -1);
    for (const Step flat : {Step::B, Step::E, Step::A, Step::D, Step::G}) {
        CHECK(bFlatMinor.alterFor(flat) == -1);
    }
    CHECK(bFlatMinor.alterFor(Step::C) == 0);

    CHECK(KeySignature(7, KeyMode::Major).toString() == "C# major");
    CHECK(KeySignature(-7, KeyMode::Major).toString() == "Cb major");
    CHECK(KeySignature(0, KeyMode::Minor).toString() == "A minor");
    CHECK(KeySignature{}.toString() == "C major");
    CHECK_THROWS_AS(KeySignature(8, KeyMode::Major), std::invalid_argument);
}

TEST_CASE("Key signature parses names", "[core][signature]") {
    CHECK(KeySignature::parse("D major") == KeySignature(2, KeyMode::Major));
    CHECK(KeySignature::parse("f# minor") == KeySignature(3, KeyMode::Minor));
    CHECK(KeySignature::parse("Eb MAJOR") == KeySignature(-3, KeyMode::Major));
    CHECK(KeySignature::parse("Bb minor") == KeySignature(-5, KeyMode::Minor));
    CHECK(KeySignature::parse("G") == KeySignature(1, KeyMode::Major)); // major by default
    CHECK_FALSE(KeySignature::parse("G# major").has_value());           // 8 sharps
    CHECK_FALSE(KeySignature::parse("H major").has_value());
    CHECK_FALSE(KeySignature::parse("D dorian").has_value());
    CHECK_FALSE(KeySignature::parse("").has_value());
    for (int fifths = -7; fifths <= 7; ++fifths) {
        for (const auto mode : {KeyMode::Major, KeyMode::Minor}) {
            const KeySignature key(fifths, mode);
            CHECK(KeySignature::parse(key.toString()) == key);
        }
    }
}

TEST_CASE("Key signature spells MIDI numbers in context", "[core][signature]") {
    const KeySignature cMajor;
    CHECK(cMajor.spell(60) == Pitch(Step::C, 0, 4));
    CHECK(cMajor.spell(61) == Pitch(Step::C, 1, 4)); // chromatic: sharp by default

    const KeySignature fMajor(-1, KeyMode::Major);
    CHECK(fMajor.spell(70) == Pitch(Step::B, -1, 4)); // diatonic Bb
    CHECK(fMajor.spell(61) == Pitch(Step::D, -1, 4)); // flat key: Db
    CHECK(fMajor.spell(71) == Pitch(Step::B, 0, 4));  // B natural

    const KeySignature dMajor(2, KeyMode::Major);
    CHECK(dMajor.spell(66) == Pitch(Step::F, 1, 4));
    CHECK(dMajor.spell(68) == Pitch(Step::G, 1, 4));

    const KeySignature cSharpMajor(7, KeyMode::Major);
    CHECK(cSharpMajor.spell(60) == Pitch(Step::B, 1, 3)); // B#3 is the diatonic seventh
    CHECK(cSharpMajor.spell(62) == Pitch(Step::D, 0, 4)); // D natural, not C double sharp

    const KeySignature cFlatMajor(-7, KeyMode::Major);
    CHECK(cFlatMajor.spell(59) == Pitch(Step::C, -1, 4)); // Cb4 sounds like B3

    // The leading tone of a minor key is raised, even in a flat key.
    CHECK(KeySignature(-1, KeyMode::Minor).spell(61) == Pitch(Step::C, 1, 4)); // D minor: C#
    CHECK(KeySignature(-3, KeyMode::Minor).spell(71) == Pitch(Step::B, 0, 4)); // C minor: B
    CHECK(KeySignature(0, KeyMode::Minor).spell(68) == Pitch(Step::G, 1, 4));  // A minor: G#

    for (int fifths = -7; fifths <= 7; ++fifths) {
        for (int midi = 0; midi <= 127; ++midi) {
            CHECK(KeySignature(fifths, KeyMode::Major).spell(midi).midiNumber() == midi);
            CHECK(KeySignature(fifths, KeyMode::Minor).spell(midi).midiNumber() == midi);
        }
    }
}

TEST_CASE("Key signature lists its accidentals in written order", "[core][signature]") {
    using Steps = std::vector<Step>;
    CHECK(KeySignature{}.alteredSteps().empty());
    CHECK(KeySignature(2, KeyMode::Major).alteredSteps() == Steps{Step::F, Step::C});
    CHECK(KeySignature(-5, KeyMode::Minor).alteredSteps() ==
          Steps{Step::B, Step::E, Step::A, Step::D, Step::G});
    CHECK(KeySignature(7, KeyMode::Major).alteredSteps().size() == 7);
}

TEST_CASE("Key signature: the next note of the scale is the one a trill goes to",
          "[core][signature]") {
    // ADR-0090: in C major E trills with F, a semitone up, and D with E, a tone.
    const KeySignature c{};
    CHECK(semitonesToNextStep(Pitch(Step::E, 0, 4), c) == 1);
    CHECK(semitonesToNextStep(Pitch(Step::D, 0, 4), c) == 2);
    CHECK(semitonesToNextStep(Pitch(Step::B, 0, 4), c) == 1); // to C5, over the octave
    // The note's own accidental counts: F# to G in C major.
    CHECK(semitonesToNextStep(Pitch(Step::F, 1, 4), c) == 1);
    // The key's accidentals too: E to F# in D major, A to Bb in F major.
    CHECK(semitonesToNextStep(Pitch(Step::E, 0, 4), KeySignature(2, KeyMode::Major)) == 2);
    CHECK(semitonesToNextStep(Pitch(Step::A, 0, 4), KeySignature(-1, KeyMode::Major)) == 1);
    // Ab to B in C major is three semitones: the step decides, not the distance.
    CHECK(semitonesToNextStep(Pitch(Step::A, -1, 4), c) == 3);
}
