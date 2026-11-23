// SPDX-License-Identifier: AGPL-3.0-or-later
// Transposition (ADR-0026): pitches move by an interval that keeps their
// spelling's degree, key signatures move with them, and fretted notes find a
// string that can play their new pitch.
#pragma once

#include <elara/core/commands.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace elara::core {

/// A distance between two pitches: diatonic steps and semitones. A major second
/// up is {1, 2}, a minor third down {-2, -3}, an octave {7, 12}.
struct Interval {
    int steps = 0;
    int semitones = 0;

    /// The interval of `semitones` that takes `key` to the key, on the tonic so
    /// many semitones away, with the fewest accidentals: C major up 1 goes to
    /// D-flat major, a minor second {1, 1}. Whole octaves keep the key.
    [[nodiscard]] static Interval fromSemitones(int semitones, const KeySignature& key = {});

    friend constexpr bool operator==(const Interval&, const Interval&) noexcept = default;
};

/// `pitch` moved by `interval`, on the step that many steps away: C#4 up a
/// major second is D#4, B3 up a minor second C4. When that step would need
/// more than a double sharp or flat, the pitch is spelled from its MIDI number
/// instead. std::nullopt when it leaves MIDI 0..127.
[[nodiscard]] std::optional<Pitch> transposed(const Pitch& pitch, Interval interval);

/// The key whose tonic is this key's tonic moved by `interval`, in the same
/// mode; its enharmonic twin when that would need more than seven accidentals.
[[nodiscard]] KeySignature transposed(const KeySignature& key, Interval interval);

/// What to transpose: `semitones` up (down when negative) over measures
/// [first, first + count) of `tracks`, every track when empty. Percussion
/// tracks never move.
struct Transposition {
    int semitones = 0;
    std::size_t first = 0;
    std::size_t count = std::numeric_limits<std::size_t>::max(); ///< to the end
    std::vector<std::size_t> tracks{};
};

/// One undo step that transposes as `what` says (ADR-0026). The interval comes
/// from the key at the first measure (Interval::fromSemitones). Key
/// signatures move only when every pitched track moves. A fretted note keeps
/// its string when its new fret is on the neck; otherwise it moves to the
/// nearest free string that can play it. Throws std::out_of_range for a
/// missing track or measure, and std::invalid_argument when a note would leave
/// the MIDI range or no string can play it.
[[nodiscard]] std::unique_ptr<Command> makeTranspose(const Song& song, const Transposition& what);

} // namespace elara::core
