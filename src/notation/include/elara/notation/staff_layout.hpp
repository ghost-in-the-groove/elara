// SPDX-License-Identifier: AGPL-3.0-or-later
// Layout of one staff in standard notation (ADR-0012).
#pragma once

#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <cstddef>
#include <limits>

namespace elara::notation {

inline constexpr std::size_t kAllMeasures = std::numeric_limits<std::size_t>::max();

struct StaffLayoutOptions {
    std::size_t firstMeasure = 0;
    std::size_t measureCount = kAllMeasures; ///< clipped to the end of the song
};

/// Lays out measures of one staff on a single line, each at its natural width:
/// clef, key and time signatures, notes and rests with accidentals, dots,
/// ledger lines, stems, flags and beams, and barlines. Notes are drawn at their
/// written pitch (the sounding pitch moved by the instrument's written offset);
/// a percussion staff places General MIDI keys with the default drum map
/// (docs/instruments.md). Throws std::out_of_range for a missing track, staff
/// or first measure.
[[nodiscard]] DisplayList layoutStaff(const core::Song& song, std::size_t track, std::size_t staff,
                                      const EngravingFont& font,
                                      const StaffLayoutOptions& options = {});

} // namespace elara::notation
