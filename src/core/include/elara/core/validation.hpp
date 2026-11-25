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

/// Where splitBeatAt() made a beat start.
struct BeatSplit {
    std::size_t first = 0; ///< the beat that was split, now its first part
    std::size_t at = 0;    ///< the part that starts at the onset
    std::size_t added = 0; ///< beats inserted after `first` (the later beats moved on by as many)
};

/// Makes a beat of `voice` start at `onset` by splitting the beat that sounds
/// across it into beats of plain or dotted values, longest first, its notes
/// tied from each part to the next (ADR-0096). It sounds the same. The first
/// part keeps the notes' effects, the strum, grace notes, chord symbol, text
/// and lyrics; the others tie the notes on and carry what spans beats: the
/// hairpin, octave line, pedal (down, not changed again), slur and tremolo
/// picking, and the notes' harmonics, trill, vibrato, palm muting, let ring,
/// dead or ghost note and dynamic. Rests split into rests. A beat that starts
/// at `onset` is left as it is. Returns nullopt, changing nothing, when no
/// beat starts or sounds there, or the beat is a tuplet or has a fermata, a
/// breath mark, a whammy bar, or a note that bends or slides out: those
/// belong to the whole beat.
[[nodiscard]] std::optional<BeatSplit> splitBeatAt(Voice& voice, const Fraction& onset);

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
