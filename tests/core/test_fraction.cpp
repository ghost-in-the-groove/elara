// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/fraction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using elara::core::Fraction;

TEST_CASE("Fraction is normalised: lowest terms, positive denominator", "[core][fraction]") {
    const Fraction half(2, 4);
    CHECK(half.numerator() == 1);
    CHECK(half.denominator() == 2);

    const Fraction negative(3, -6);
    CHECK(negative.numerator() == -1);
    CHECK(negative.denominator() == 2);

    const Fraction zero(0, 7);
    CHECK(zero.numerator() == 0);
    CHECK(zero.denominator() == 1);
    CHECK(zero.isZero());

    CHECK(Fraction{} == Fraction(0));
    CHECK(Fraction(5) == Fraction(10, 2));
}

TEST_CASE("Fraction rejects a zero denominator", "[core][fraction]") {
    CHECK_THROWS_AS(Fraction(1, 0), std::invalid_argument);
}

TEST_CASE("Fraction arithmetic is exact", "[core][fraction]") {
    CHECK(Fraction(1, 4) + Fraction(1, 8) == Fraction(3, 8));
    CHECK(Fraction(1, 2) - Fraction(3, 4) == Fraction(-1, 4));
    CHECK(Fraction(2, 3) * Fraction(3, 4) == Fraction(1, 2));
    CHECK(Fraction(1, 2) / Fraction(1, 4) == Fraction(2));
    CHECK(-Fraction(3, 8) == Fraction(-3, 8));

    // A sixteenth-note triplet three times is an eighth note: no rounding drift.
    Fraction sum;
    for (int i = 0; i < 3; ++i) {
        sum += Fraction(1, 24);
    }
    CHECK(sum == Fraction(1, 8));

    Fraction x(3, 4);
    x -= Fraction(1, 4);
    x *= Fraction(3);
    x /= Fraction(3, 2);
    CHECK(x == Fraction(1));
}

TEST_CASE("Fraction division by zero throws", "[core][fraction]") {
    CHECK_THROWS_AS(Fraction(1, 2) / Fraction(0), std::domain_error);
}

TEST_CASE("Fractions order by value", "[core][fraction]") {
    CHECK(Fraction(1, 3) < Fraction(1, 2));
    CHECK(Fraction(-1, 2) < Fraction(1, 3));
    CHECK(Fraction(2, 4) <= Fraction(1, 2));
    CHECK(Fraction(5, 4) > Fraction(1));
    CHECK(Fraction(3, 8) != Fraction(3, 16));
}

TEST_CASE("Fraction converts to text and to double", "[core][fraction]") {
    CHECK(Fraction(3, 8).toString() == "3/8");
    CHECK(Fraction(-1, 4).toString() == "-1/4");
    CHECK(Fraction(4, 2).toString() == "2");
    CHECK(Fraction(3, 8).toDouble() == 0.375);
}
