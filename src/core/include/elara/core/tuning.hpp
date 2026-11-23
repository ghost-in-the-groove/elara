// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/pitch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace elara::core {

/// Where a note is played on a fretted instrument. `fret` is relative to the capo.
struct FretPosition {
    std::int8_t string = 1;
    std::int8_t fret = 0;
    friend constexpr bool operator==(const FretPosition&, const FretPosition&) noexcept = default;
};

/// Open-string pitches of a fretted instrument. Strings are numbered from 1, the
/// top line of a tablature staff: the highest string on most instruments
/// (re-entrant tunings such as the ukulele's are allowed).
///
/// A fixed-capacity value type (no allocation), so it can be copied freely
/// between tracks and commands.
class Tuning {
public:
    static constexpr int kMaxStrings = 12;
    static constexpr int kMaxFret = 30; // highest fret, counted from the nut or the capo

    /// Throws std::invalid_argument for no strings or more than kMaxStrings.
    constexpr Tuning(std::initializer_list<Pitch> openStrings)
        : Tuning(std::span<const Pitch>(openStrings.begin(), openStrings.size())) {}

    /// Open strings ordered from string 1 (highest) to the lowest.
    constexpr explicit Tuning(std::span<const Pitch> openStrings) {
        if (openStrings.empty() || openStrings.size() > kMaxStrings) {
            throw std::invalid_argument("Tuning: needs 1 to 12 strings");
        }
        for (std::size_t i = 0; i < openStrings.size(); ++i) {
            strings_[i] = openStrings[i];
        }
        count_ = static_cast<std::uint8_t>(openStrings.size());
    }

    /// E4 B3 G3 D3 A2 E2.
    [[nodiscard]] static constexpr Tuning standardGuitar() {
        return {Pitch(Step::E, 0, 4), Pitch(Step::B, 0, 3), Pitch(Step::G, 0, 3),
                Pitch(Step::D, 0, 3), Pitch(Step::A, 0, 2), Pitch(Step::E, 0, 2)};
    }

    /// G2 D2 A1 E1.
    [[nodiscard]] static constexpr Tuning standardBass() {
        return {Pitch(Step::G, 0, 2), Pitch(Step::D, 0, 2), Pitch(Step::A, 0, 1),
                Pitch(Step::E, 0, 1)};
    }

    [[nodiscard]] constexpr int stringCount() const noexcept { return count_; }

    /// Throws std::out_of_range unless 1 <= string <= stringCount().
    [[nodiscard]] constexpr Pitch openString(int string) const {
        checkString(string);
        return strings_[static_cast<std::size_t>(string - 1)];
    }

    /// MIDI number sounded by `fret` on `string` with a capo at fret `capo`.
    /// Frets are written relative to the capo, as in tablature. Throws
    /// std::out_of_range for a bad string, a fret or capo outside the neck
    /// (capo + fret > kMaxFret) or a result above MIDI 127.
    [[nodiscard]] constexpr int midiAt(int string, int fret, int capo = 0) const {
        checkString(string);
        if (fret < 0 || capo < 0 || capo + fret > kMaxFret) {
            throw std::out_of_range("Tuning: fret outside the neck");
        }
        const int midi = strings_[static_cast<std::size_t>(string - 1)].midiNumber() + capo + fret;
        if (midi > 127) {
            throw std::out_of_range("Tuning: note above the MIDI range");
        }
        return midi;
    }

    /// Every position that sounds `pitch` (any spelling), string 1 first, with
    /// frets from 0 to `maxFret` above the capo.
    [[nodiscard]] std::vector<FretPosition> positionsFor(const Pitch& pitch, int capo = 0,
                                                         int maxFret = kMaxFret) const;

    /// Open strings from the lowest tab line to string 1, the way tunings are
    /// named ("DADGAD"): "E2 A2 D3 G3 B3 E4".
    [[nodiscard]] std::string toString() const;

    /// Reads the toString() form; any whitespace separates strings. std::nullopt
    /// for a malformed pitch or a string count outside 1..kMaxStrings.
    [[nodiscard]] static std::optional<Tuning> parse(std::string_view text);

    /// Common tunings: "guitar", "guitar-drop-d", "bass", "bass-5", "ukulele"...
    [[nodiscard]] static std::optional<Tuning> preset(std::string_view name);
    [[nodiscard]] static std::span<const std::string_view> presetNames() noexcept;

    friend constexpr bool operator==(const Tuning&, const Tuning&) noexcept = default;

private:
    constexpr void checkString(int string) const {
        if (string < 1 || string > count_) {
            throw std::out_of_range("Tuning: no such string");
        }
    }

    std::array<Pitch, kMaxStrings> strings_{}; // unused slots stay default
    std::uint8_t count_ = 0;
};

} // namespace elara::core
