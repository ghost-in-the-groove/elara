// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>

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

    friend constexpr bool operator==(const Tempo&, const Tempo&) noexcept = default;

private:
    double bpm_ = 120.0;
    Duration beatUnit_{};
};

/// A tempo that starts `offset` whole notes into a measure.
struct TempoChange {
    Fraction offset{};
    Tempo tempo{};

    friend constexpr bool operator==(const TempoChange&, const TempoChange&) noexcept = default;
};

} // namespace elara::core
