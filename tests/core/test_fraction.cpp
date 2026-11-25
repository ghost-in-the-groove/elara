// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/fraction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

TEST_CASE("Fractions order by value when their cross products would overflow", "[core][fraction]") {
    // A damaged MusicXML file with <divisions> near the int limit gives
    // denominators around 2^33; multiplying across overflowed 64 bits and
    // ordered these two the wrong way round.
    const Fraction below(3'037'000'499, 3'037'000'500); // 1 - 1/q
    const Fraction above(3'037'000'501, 3'037'000'500); // 1 + 1/q
    CHECK(below < above);
    CHECK(above > below);
    CHECK(below < Fraction(1));
    CHECK(Fraction(1) < above);
    CHECK_FALSE(below < Fraction(3'037'000'499, 3'037'000'500));
    CHECK_FALSE(below > Fraction(3'037'000'499, 3'037'000'500));
    CHECK(-above < -below);
    CHECK(Fraction(-3'037'000'501, 3'037'000'500) < Fraction(-1));
    // Close values with large terms on both sides.
    CHECK(Fraction(4'999'999'999, 4'999'999'998) > Fraction(5'000'000'000, 4'999'999'999));
    CHECK(Fraction(9'000'000'000'000'000'000, 3) > Fraction(8'999'999'999'999'999'999, 3));
    CHECK(Fraction(1, 9'000'000'000'000'000'000) < Fraction(1, 8'999'999'999'999'999'999));

    // Large terms of either sign: the order agrees with doubles wherever
    // doubles tell the two apart.
    std::uint64_t state = 0x9E37'79B9'7F4A'7C15U;
    const auto next = [&state] {
        state = state * 6'364'136'223'846'793'005U + 1'442'695'040'888'963'407U;
        return static_cast<std::int64_t>(state >> 2U); // below 2^62
    };
    int compared = 0;
    for (int i = 0; i < 2000; ++i) {
        const Fraction x((i % 2 == 0 ? 1 : -1) * next(), next() % 4'000'000'000'000 + 1);
        const Fraction y((i % 3 == 0 ? -1 : 1) * next(), next() % 4'000'000'000'000 + 1);
        const double dx = x.toDouble();
        const double dy = y.toDouble();
        if (std::abs(dx - dy) > 1e-9 * std::max(std::abs(dx), std::abs(dy))) {
            ++compared;
            CHECK((x < y) == (dx < dy));
            CHECK((y < x) == (dy < dx));
        }
    }
    CHECK(compared > 1000);
}

TEST_CASE("Fraction converts to text and to double", "[core][fraction]") {
    CHECK(Fraction(3, 8).toString() == "3/8");
    CHECK(Fraction(-1, 4).toString() == "-1/4");
    CHECK(Fraction(4, 2).toString() == "2");
    CHECK(Fraction(3, 8).toDouble() == 0.375);
}
