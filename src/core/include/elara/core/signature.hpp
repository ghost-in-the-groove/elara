// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/fraction.hpp>
#include <elara/core/pitch.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace elara::core {

/// Meter of a measure: `numerator` beats of 1/`denominator` each. 6/8 and 3/4 last
/// the same but are different meters.
class TimeSignature {
public:
    static constexpr int kMaxNumerator = 32;

    constexpr TimeSignature() noexcept = default; // 4/4

    /// Throws std::invalid_argument unless 1 <= numerator <= kMaxNumerator and
    /// denominator is a power of two from 1 to 64.
    constexpr TimeSignature(int numerator, int denominator)
        : numerator_(static_cast<std::uint8_t>(numerator)),
          denominator_(static_cast<std::uint8_t>(denominator)) {
        const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
        if (numerator < 1 || numerator > kMaxNumerator || !powerOfTwo || denominator > 64) {
            throw std::invalid_argument("TimeSignature: unsupported meter");
        }
    }

    [[nodiscard]] constexpr int numerator() const noexcept { return numerator_; }
    [[nodiscard]] constexpr int denominator() const noexcept { return denominator_; }

    /// Length of a full measure in whole notes (6/8 -> 3/4).
    [[nodiscard]] constexpr Fraction measureLength() const {
        return Fraction(numerator_, denominator_);
    }
    /// Length of the note value the denominator names (6/8 -> 1/8).
    [[nodiscard]] constexpr Fraction beatLength() const { return Fraction(1, denominator_); }

    /// "6/8".
    [[nodiscard]] std::string toString() const;
    /// Reads "N/D"; std::nullopt for anything else or an unsupported meter.
    [[nodiscard]] static std::optional<TimeSignature> parse(std::string_view text);

    friend constexpr bool operator==(const TimeSignature&, const TimeSignature&) noexcept = default;

private:
    std::uint8_t numerator_ = 4;
    std::uint8_t denominator_ = 4;
};

enum class KeyMode : std::uint8_t { Major, Minor };

/// Key signature as a position on the circle of fifths: +n sharps, -n flats
/// (MusicXML's "fifths"), and a mode. Spells MIDI numbers in context.
class KeySignature {
public:
    static constexpr int kMaxFifths = 7;

    constexpr KeySignature() noexcept = default; // C major

    /// Throws std::invalid_argument when |fifths| > kMaxFifths.
    constexpr KeySignature(int fifths, KeyMode mode)
        : fifths_(static_cast<std::int8_t>(fifths)), mode_(mode) {
        if (fifths < -kMaxFifths || fifths > kMaxFifths) {
            throw std::invalid_argument("KeySignature: more than seven accidentals");
        }
    }

    [[nodiscard]] constexpr int fifths() const noexcept { return fifths_; }
    [[nodiscard]] constexpr KeyMode mode() const noexcept { return mode_; }

    [[nodiscard]] Step tonic() const noexcept;
    [[nodiscard]] int tonicAlter() const noexcept;

    /// Alteration the key signature applies to `step`: +1, -1 or 0.
    [[nodiscard]] int alterFor(Step step) const noexcept;

    /// Steps the signature alters, in the order they are written (sharps from F,
    /// flats from B).
    [[nodiscard]] std::vector<Step> alteredSteps() const;

    /// Spells a MIDI number for this key: diatonic notes as in the key, the raised
    /// leading tone in minor, otherwise a natural, then a sharp in sharp keys (and
    /// C) or a flat in flat keys. Throws std::invalid_argument outside 0..127.
    [[nodiscard]] Pitch spell(int midiNumber) const;

    /// "D major", "Bb minor".
    [[nodiscard]] std::string toString() const;
    /// Reads "<tonic> [major|minor]" (case-insensitive, major when omitted);
    /// std::nullopt for an unknown name or a key with more than seven accidentals.
    [[nodiscard]] static std::optional<KeySignature> parse(std::string_view text);

    friend constexpr bool operator==(const KeySignature&, const KeySignature&) noexcept = default;

private:
    std::int8_t fifths_ = 0;
    KeyMode mode_ = KeyMode::Major;
};

} // namespace elara::core
