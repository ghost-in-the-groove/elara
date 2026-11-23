// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/tuning.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace elara::core {

/// One sounding note (ADR-0004). `pitch` is the sounding (concert) pitch; on a
/// fretted staff `position` says where it is played and must sound `pitch` under
/// the staff's tuning and capo. Percussion notes use the General MIDI percussion
/// key as the pitch's MIDI number.
struct Note {
    Pitch pitch{};
    std::optional<FretPosition> position{};

    friend bool operator==(const Note&, const Note&) = default;
};

/// Notes that start together and share a written duration. No notes = a rest.
struct Beat {
    Duration duration{};
    std::vector<Note> notes{};

    [[nodiscard]] bool isRest() const noexcept { return notes.empty(); }

    friend bool operator==(const Beat&, const Beat&) = default;
};

/// A sequence of beats. Positions are derived from the durations, never stored.
struct Voice {
    std::vector<Beat> beats{};

    [[nodiscard]] bool isEmpty() const noexcept { return beats.empty(); }

    /// Total length in whole notes.
    [[nodiscard]] Fraction length() const;

    /// Start of beat `index` from the start of the measure, in whole notes.
    /// Throws std::out_of_range for a missing beat.
    [[nodiscard]] Fraction startOf(std::size_t index) const;

    friend bool operator==(const Voice&, const Voice&) = default;
};

/// The content of one staff in one measure: up to four independent voices.
struct Measure {
    static constexpr std::size_t kMaxVoices = 4;

    std::array<Voice, kMaxVoices> voices{};

    [[nodiscard]] bool isEmpty() const noexcept;

    friend bool operator==(const Measure&, const Measure&) = default;
};

} // namespace elara::core
