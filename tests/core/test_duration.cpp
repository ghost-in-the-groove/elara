// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/duration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using elara::core::Duration;
using elara::core::Fraction;
using elara::core::NoteValue;
using elara::core::Tuplet;

TEST_CASE("Plain note values are powers of two of a whole note", "[core][duration]") {
    CHECK(Duration(NoteValue::DoubleWhole).length() == Fraction(2));
    CHECK(Duration(NoteValue::Whole).length() == Fraction(1));
    CHECK(Duration(NoteValue::Half).length() == Fraction(1, 2));
    CHECK(Duration(NoteValue::Quarter).length() == Fraction(1, 4));
    CHECK(Duration(NoteValue::Eighth).length() == Fraction(1, 8));
    CHECK(Duration(NoteValue::Sixteenth).length() == Fraction(1, 16));
    CHECK(Duration(NoteValue::ThirtySecond).length() == Fraction(1, 32));
    CHECK(Duration(NoteValue::SixtyFourth).length() == Fraction(1, 64));
    CHECK(Duration(NoteValue::HundredTwentyEighth).length() == Fraction(1, 128));
    CHECK(Duration{}.length() == Fraction(1, 4)); // default is a quarter note
}

TEST_CASE("Each dot adds half of the previous value", "[core][duration]") {
    CHECK(Duration(NoteValue::Quarter, 1).length() == Fraction(3, 8));  // 1/4 + 1/8
    CHECK(Duration(NoteValue::Quarter, 2).length() == Fraction(7, 16)); // + 1/16
    CHECK(Duration(NoteValue::Half, 3).length() == Fraction(15, 16));   // 1/2+1/4+1/8+1/16
    CHECK(Duration(NoteValue::Eighth, 1).length() == Fraction(3, 16));
}

TEST_CASE("Tuplets scale by normal/actual", "[core][duration]") {
    const Tuplet triplet(3, 2);
    const Duration eighthTriplet(NoteValue::Eighth, 0, triplet);
    CHECK(eighthTriplet.length() == Fraction(1, 12));
    CHECK(eighthTriplet.length() * 3 == Duration(NoteValue::Quarter).length());

    const Duration sixteenthQuintuplet(NoteValue::Sixteenth, 0, Tuplet(5, 4));
    CHECK(sixteenthQuintuplet.length() * 5 == Fraction(1, 4));

    // Dotted eighth inside a triplet: 3/16 * 2/3.
    CHECK(Duration(NoteValue::Eighth, 1, triplet).length() == Fraction(1, 8));

    // Septuplet of 32nds in the time of 8.
    CHECK(Duration(NoteValue::ThirtySecond, 0, Tuplet(7, 8)).length() * 7 == Fraction(1, 4));
}

TEST_CASE("Tuplet defaults to none and rejects non-positive counts", "[core][duration]") {
    CHECK(Tuplet{}.isNone());
    CHECK_FALSE(Tuplet(3, 2).isNone());
    CHECK(Tuplet(3, 2).actual() == 3);
    CHECK(Tuplet(3, 2).normal() == 2);
    CHECK_THROWS_AS(Tuplet(0, 2), std::invalid_argument);
    CHECK_THROWS_AS(Tuplet(3, -1), std::invalid_argument);
}

TEST_CASE("Duration rejects an unsupported number of dots", "[core][duration]") {
    CHECK_NOTHROW(Duration(NoteValue::Quarter, Duration::kMaxDots));
    CHECK_THROWS_AS(Duration(NoteValue::Quarter, Duration::kMaxDots + 1), std::invalid_argument);
    CHECK_THROWS_AS(Duration(NoteValue::Quarter, -1), std::invalid_argument);
}

TEST_CASE("Durations compare by value, not by length", "[core][duration]") {
    // A dotted quarter and a quarter-note triplet tied to an eighth may last the
    // same, but they are written differently.
    CHECK(Duration(NoteValue::Quarter, 1) == Duration(NoteValue::Quarter, 1));
    CHECK(Duration(NoteValue::Quarter, 1) != Duration(NoteValue::Quarter));
    CHECK(Duration(NoteValue::Half, 0, Tuplet(3, 2)).length() ==
          Duration(NoteValue::Quarter, 0).length() * Fraction(4, 3));
}
