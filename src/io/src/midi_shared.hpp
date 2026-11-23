// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: choices the MIDI reader and writer must agree on (ADR-0009).
#pragma once

#include <elara/core/instrument.hpp>

#include <cmath>

namespace elara::io::detail {

/// The kind of instrument a track gets back from its General MIDI program:
/// channel 10 is drums, pianos (programs 0-7) are keyboards, guitars and basses
/// (24-39) are fretted, everything else is pitched.
[[nodiscard]] constexpr core::InstrumentKind kindForProgram(int program, bool percussion) noexcept {
    if (percussion) {
        return core::InstrumentKind::Percussion;
    }
    if (program >= 0 && program <= 7) {
        return core::InstrumentKind::Keyboard;
    }
    if (program >= 24 && program <= 39) {
        return core::InstrumentKind::Fretted;
    }
    return core::InstrumentKind::Pitched;
}

/// Quarter notes per minute read from a tempo event, rounded to hundredths.
[[nodiscard]] inline double bpmFromMicroseconds(long long microsecondsPerQuarter) noexcept {
    return std::round(6e9 / static_cast<double>(microsecondsPerQuarter)) / 100.0;
}

/// The tempo event's value for `quarterBpm` quarter notes per minute.
[[nodiscard]] inline long long microsecondsFromBpm(double quarterBpm) noexcept {
    return std::llround(60e6 / quarterBpm);
}

} // namespace elara::io::detail
