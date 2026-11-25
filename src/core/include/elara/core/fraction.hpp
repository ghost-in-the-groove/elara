// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>

namespace elara::core {

/// Exact rational number. Durations and time positions are Fractions of a whole
/// note, so tuplets never accumulate rounding error.
///
/// Always normalised: lowest terms and a positive denominator, so equal values
/// have equal representations. A 16-byte value type with no allocation; every
/// operation except toString() is constexpr.
class Fraction {
public:
    constexpr Fraction() noexcept = default;

    /// Implicit from integers so `Fraction x = 1;` reads naturally.
    /// Throws std::invalid_argument when `denominator` is zero.
    constexpr Fraction(std::int64_t numerator,
                       std::int64_t denominator = 1) // NOLINT(google-explicit-constructor)
        : num_(numerator), den_(denominator) {
        if (den_ == 0) {
            throw std::invalid_argument("Fraction: zero denominator");
        }
        normalise();
    }

    [[nodiscard]] constexpr std::int64_t numerator() const noexcept { return num_; }
    [[nodiscard]] constexpr std::int64_t denominator() const noexcept { return den_; }
    [[nodiscard]] constexpr bool isZero() const noexcept { return num_ == 0; }
    [[nodiscard]] constexpr double toDouble() const noexcept {
        return static_cast<double>(num_) / static_cast<double>(den_);
    }

    /// "3/8", "-1/4", or "2" for whole numbers.
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] constexpr Fraction operator-() const { return Fraction(-num_, den_); }

    friend constexpr Fraction operator+(const Fraction& a, const Fraction& b) {
        const std::int64_t g = std::gcd(a.den_, b.den_);
        return Fraction(a.num_ * (b.den_ / g) + b.num_ * (a.den_ / g), a.den_ / g * b.den_);
    }

    friend constexpr Fraction operator-(const Fraction& a, const Fraction& b) { return a + -b; }

    friend constexpr Fraction operator*(const Fraction& a, const Fraction& b) {
        // Cross-reduce first so intermediate products stay small.
        const std::int64_t g1 = std::gcd(a.num_, b.den_);
        const std::int64_t g2 = std::gcd(b.num_, a.den_);
        const std::int64_t n1 = g1 == 0 ? a.num_ : a.num_ / g1;
        const std::int64_t d2 = g1 == 0 ? b.den_ : b.den_ / g1;
        const std::int64_t n2 = g2 == 0 ? b.num_ : b.num_ / g2;
        const std::int64_t d1 = g2 == 0 ? a.den_ : a.den_ / g2;
        return Fraction(n1 * n2, d1 * d2);
    }

    /// Throws std::domain_error when `b` is zero.
    friend constexpr Fraction operator/(const Fraction& a, const Fraction& b) {
        if (b.num_ == 0) {
            throw std::domain_error("Fraction: division by zero");
        }
        return a * Fraction(b.den_, b.num_);
    }

    constexpr Fraction& operator+=(const Fraction& other) { return *this = *this + other; }
    constexpr Fraction& operator-=(const Fraction& other) { return *this = *this - other; }
    constexpr Fraction& operator*=(const Fraction& other) { return *this = *this * other; }
    constexpr Fraction& operator/=(const Fraction& other) { return *this = *this / other; }

    friend constexpr bool operator==(const Fraction&, const Fraction&) noexcept = default;

    friend constexpr std::strong_ordering operator<=>(const Fraction& a,
                                                      const Fraction& b) noexcept {
        // Denominators are positive, so cross-multiplication keeps the order;
        // terms under 2^31, every musical value, give products that fit.
        if (fitsCrossProduct(a) && fitsCrossProduct(b)) {
            return a.num_ * b.den_ <=> b.num_ * a.den_;
        }
        return compareLarge(a.num_, a.den_, b.num_, b.den_);
    }

private:
    [[nodiscard]] static constexpr bool fitsCrossProduct(const Fraction& f) noexcept {
        constexpr std::int64_t kLimit = std::int64_t{1} << 31;
        return f.num_ > -kLimit && f.num_ < kLimit && f.den_ < kLimit;
    }

    // Orders a/b and c/d (positive denominators) without multiplying, so
    // terms near the 64-bit limit (a damaged file's) never overflow: compare
    // the whole parts, then the remainders, whose order flips once inverted.
    [[nodiscard]] static constexpr std::strong_ordering
    compareLarge(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d) noexcept {
        bool flipped = false;
        for (;;) {
            // Floored division: a remainder in [0, b).
            std::int64_t wholeAb = a / b;
            std::int64_t restAb = a % b;
            if (restAb < 0) {
                --wholeAb;
                restAb += b;
            }
            std::int64_t wholeCd = c / d;
            std::int64_t restCd = c % d;
            if (restCd < 0) {
                --wholeCd;
                restCd += d;
            }
            std::strong_ordering order = wholeAb <=> wholeCd;
            if (order == 0 && (restAb == 0 || restCd == 0)) {
                order = restAb <=> restCd; // 0 is below any other remainder
            }
            if (order != 0 || restAb == 0) {
                return flipped ? 0 <=> order : order;
            }
            // restAb/b against restCd/d is d/restCd against b/restAb.
            const std::int64_t nextB = restAb;
            const std::int64_t nextD = restCd;
            a = b;
            b = nextB;
            c = d;
            d = nextD;
            flipped = !flipped;
        }
    }

    constexpr void normalise() noexcept {
        if (den_ < 0) {
            num_ = -num_;
            den_ = -den_;
        }
        const std::int64_t g = std::gcd(num_, den_); // gcd(0, d) == d
        num_ /= g;
        den_ /= g;
    }

    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

} // namespace elara::core
