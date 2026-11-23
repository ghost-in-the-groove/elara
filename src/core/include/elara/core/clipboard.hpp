// SPDX-License-Identifier: AGPL-3.0-or-later
// Copying measures and pasting them elsewhere in a song (ADR-0027).
#pragma once

#include <elara/core/commands.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace elara::core {

/// One staff of a copied track: its measures and what its frets were played on.
struct ClipStaff {
    std::optional<Tuning> tuning{};
    int capo = 0;
    std::vector<Measure> measures{};

    friend bool operator==(const ClipStaff&, const ClipStaff&) = default;
};

/// One copied track.
struct ClipTrack {
    InstrumentKind kind = InstrumentKind::Pitched;
    std::vector<ClipStaff> staves{};

    friend bool operator==(const ClipTrack&, const ClipTrack&) = default;
};

/// Measures copied from consecutive tracks of a song, with their headers.
struct MeasureClip {
    std::vector<MeasureHeader> headers{}; ///< one per measure
    std::vector<ClipTrack> tracks{};

    [[nodiscard]] std::size_t measureCount() const noexcept { return headers.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return headers.empty() || tracks.empty(); }

    friend bool operator==(const MeasureClip&, const MeasureClip&) = default;
};

/// Copies measures [first, first + count) of `trackCount` tracks from `track`
/// (every track from it to the last when 0). Throws std::out_of_range for
/// measures or tracks the song does not have, or a count of 0.
[[nodiscard]] MeasureClip copyMeasures(const Song& song, std::size_t first, std::size_t count,
                                       std::size_t track = 0, std::size_t trackCount = 0);

enum class PasteMode : std::uint8_t {
    Replace, ///< over the measures from `at`, appending measures past the end
    Insert,  ///< into new measures before `at`, with the copied headers
};

/// One undo step that pastes `clip` at measure `at` of track `track` and the
/// tracks after it (ADR-0027). Each copied staff goes to the staff of the same
/// index; staves the target track lacks are dropped. Fretted notes are placed
/// on the target staff's strings when its tuning or capo differs, and lose their
/// positions on a staff without a tuning. Throws std::out_of_range when the
/// clip is empty, `at` is past the end, or the song lacks a target track;
/// std::invalid_argument when drums would go to a pitched track or back, or no
/// string of the target can play a note.
[[nodiscard]] std::unique_ptr<Command> makePaste(const Song& song, const MeasureClip& clip,
                                                 std::size_t at, std::size_t track = 0,
                                                 PasteMode mode = PasteMode::Replace);

} // namespace elara::core
