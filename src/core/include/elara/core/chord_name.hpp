// SPDX-License-Identifier: AGPL-3.0-or-later
// Chord names (ADR-0080): the pitches a chord symbol's name spells, so that
// writers can name them and transposition can move them.
#pragma once

#include <elara/core/pitch.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace elara::core {

/// A pitch of a chord name, without an octave: a step and its sharps (+) or
/// flats (-).
struct ChordPitch {
    Step step = Step::C;
    int alter = 0;

    /// The step's letter and its accidentals: "Bb", "F#".
    [[nodiscard]] std::string toString() const;

    friend constexpr bool operator==(const ChordPitch&, const ChordPitch&) noexcept = default;
};

/// A chord name split into its parts: "Bb7/F" is the root B flat, the suffix
/// "7" and the bass F. The suffix is a view into the name.
struct ChordNameParts {
    ChordPitch root{};
    std::string_view suffix{};
    std::optional<ChordPitch> bass{};
};

/// The parts of `name`, which starts with a letter A to G and up to two `#` or
/// `b`; std::nullopt for a name that starts otherwise ("N.C."). A slash names
/// a bass only when a pitch follows it to the end: "C6/9" has the suffix "6/9".
[[nodiscard]] std::optional<ChordNameParts> splitChordName(std::string_view name);

} // namespace elara::core
