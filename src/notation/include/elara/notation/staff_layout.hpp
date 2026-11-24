// SPDX-License-Identifier: AGPL-3.0-or-later
// Layout of staves in standard notation and tablature (ADR-0012).
#pragma once

#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace elara::notation {

inline constexpr std::size_t kAllMeasures = std::numeric_limits<std::size_t>::max();

struct StaffLayoutOptions {
    std::size_t firstMeasure = 0;
    std::size_t measureCount = kAllMeasures; ///< clipped to the end of the song
};

/// One staff of a system and how it is shown.
struct StaffSelection {
    std::size_t track = 0;
    std::size_t staff = 0;
    StaffView view = StaffView::Standard;
    friend bool operator==(const StaffSelection&, const StaffSelection&) = default;
};

/// How whole tracks are shown: their staves in standard notation, as tablature,
/// or both (notation above tablature).
enum class TrackView : std::uint8_t { Standard, Tablature, Both };

/// Every staff of `tracks`, in order, shown as `view`. Staves without a tuning
/// have no tablature, so they show standard notation in every view. Throws
/// std::out_of_range for a missing track.
[[nodiscard]] std::vector<StaffSelection>
selectTracks(const core::Song& song, std::span<const std::size_t> tracks, TrackView view);

/// Whether a staff of `tracks` has a tuning, so that it can be shown as
/// tablature. Throws std::out_of_range for a missing track.
[[nodiscard]] bool hasTablature(const core::Song& song, std::span<const std::size_t> tracks);

/// Lays out measures of several staves on a single line, top to bottom in the
/// order given, with notes that start together aligned and barlines shared.
///
/// Standard notation: clef, key and time signatures, notes and rests with
/// accidentals, dots, ledger lines, stems, flags, beams, ties and articulations,
/// and dynamics marks under the staff where the loudness changes.
/// Notes are drawn at their written pitch (the sounding pitch moved by the
/// instrument's written offset); a percussion staff places General MIDI keys with
/// the default drum map (docs/instruments.md).
///
/// Tablature of a fretted staff: fret numbers on the string lines, with dead,
/// ghost, tied and harmonic notes, hammer-ons and pull-offs, slides and bends.
/// Palm mutes, let ring, vibrato, harmonics and pick strokes go in lanes above
/// the tablature, or above the standard notation when the system has no
/// tablature of that staff. Rests, rhythm and dynamics marks show in tablature
/// only without standard notation of the same staff.
///
/// Throws std::out_of_range for a missing track, staff or first measure and
/// std::invalid_argument for no staves or tablature of a staff without tuning.
[[nodiscard]] DisplayList layoutSystem(const core::Song& song,
                                       std::span<const StaffSelection> staves,
                                       const EngravingFont& font,
                                       const StaffLayoutOptions& options = {});

/// layoutSystem() with the standard notation of one staff.
[[nodiscard]] DisplayList layoutStaff(const core::Song& song, std::size_t track, std::size_t staff,
                                      const EngravingFont& font,
                                      const StaffLayoutOptions& options = {});

} // namespace elara::notation
