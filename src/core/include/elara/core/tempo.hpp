// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>

#include <cmath>
#include <stdexcept>

namespace elara::core {

/// Speed as beats per minute of a beat unit: "quarter = 120" or, in 6/8,
/// "dotted quarter = 60".
class Tempo {
public:
    static constexpr double kMinBpm = 10.0;
    static constexpr double kMaxBpm = 999.0;

    constexpr Tempo() noexcept = default; // quarter = 120

    /// Throws std::invalid_argument when bpm is outside [kMinBpm, kMaxBpm].
    constexpr explicit Tempo(double bpm, Duration beatUnit = Duration{})
        : bpm_(bpm), beatUnit_(beatUnit) {
        if (!(bpm >= kMinBpm && bpm <= kMaxBpm)) {
            throw std::invalid_argument("Tempo: bpm out of range");
        }
    }

    [[nodiscard]] constexpr double bpm() const noexcept { return bpm_; }
    [[nodiscard]] constexpr Duration beatUnit() const noexcept { return beatUnit_; }

    /// Seconds that `wholeNotes` last at this tempo.
    [[nodiscard]] double secondsFor(const Fraction& wholeNotes) const {
        return wholeNotes.toDouble() * 60.0 / (bpm_ * beatUnit_.length().toDouble());
    }

    /// Whole notes a second: the speed a gradual change moves along (ADR-0082).
    [[nodiscard]] double wholesPerSecond() const {
        return bpm_ * beatUnit_.length().toDouble() / 60.0;
    }

    friend constexpr bool operator==(const Tempo&, const Tempo&) noexcept = default;

private:
    double bpm_ = 120.0;
    Duration beatUnit_{};
};

/// A tempo that starts `offset` whole notes into a measure. A gradual one
/// speeds up or slows down from there to the next change's tempo, reached at
/// that change (accelerando, ritardando); with no change after it, it holds
/// (ADR-0082).
struct TempoChange {
    Fraction offset{};
    Tempo tempo{};
    bool gradual = false;

    friend constexpr bool operator==(const TempoChange&, const TempoChange&) noexcept = default;
};

/// The seconds `distance` whole notes take from where the speed is `speed`
/// whole notes a second and changes by `slope` per whole note along the score:
/// the time of a gradual tempo change (ADR-0082), distance / speed without one.
[[nodiscard]] inline double rampSeconds(double speed, double slope, double distance) {
    constexpr double kSteady = 1e-12; // relative change below which the speed holds
    if (std::abs(slope * distance) <= kSteady * speed) {
        return distance / speed;
    }
    return std::log1p(slope * distance / speed) / slope;
}

} // namespace elara::core
