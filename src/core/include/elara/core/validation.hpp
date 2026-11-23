// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/measure.hpp>
#include <elara/core/signature.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

class Song;

/// How much of its measure a voice fills. An empty voice stands for a measure rest.
enum class VoiceFill : std::uint8_t { Empty, Incomplete, Complete, Overfull };

[[nodiscard]] VoiceFill voiceFill(const Voice& voice, const TimeSignature& time);

/// Appends rests until the voice fills its measure, following engraving practice:
/// the current beat is completed first (an unfinished tuplet with rests of the
/// same tuplet), then whole beats with the largest rests aligned to the meter
/// (dotted rests by beat in compound meters such as 6/8). Empty and complete
/// voices are left as they are and return true. Returns false, leaving the voice
/// unchanged, when it is overfull or its gap cannot be written with such rests
/// (e.g. after a 5:3 tuplet that does not fill its beat).
bool fillWithRests(Voice& voice, const TimeSignature& time);

enum class IssueKind : std::uint8_t {
    VoiceIncomplete,       // shorter than its measure (not empty)
    VoiceOverfull,         // longer than its measure
    StringOutOfRange,      // fret position on a string the tuning does not have
    FretOutOfRange,        // capo + fret beyond the neck
    FretMismatch,          // the position does not sound the note's pitch
    StringReused,          // two notes of one beat on the same string
    PositionWithoutTuning, // fret position on a staff without a tuning
    TieWithoutPrevious,    // tied, but the previous beat has no such note
};

/// Stable kebab-case name, e.g. "voice-incomplete" (used in JSON output).
[[nodiscard]] std::string_view issueName(IssueKind kind) noexcept;

/// Where a problem is. `beat` and `note` are set for note-level issues.
struct Issue {
    IssueKind kind = IssueKind::VoiceIncomplete;
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t measure = 0;
    std::size_t voice = 0;
    std::optional<std::size_t> beat{};
    std::optional<std::size_t> note{};

    friend bool operator==(const Issue&, const Issue&) = default;
};

/// Every inconsistency in the song, ordered by track, staff, measure, voice and
/// beat. An empty result means the song is well formed.
[[nodiscard]] std::vector<Issue> validate(const Song& song);

} // namespace elara::core
