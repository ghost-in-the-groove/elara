// SPDX-License-Identifier: AGPL-3.0-or-later
// Picking a symbol of a laid-out score with the pointer (ADR-0130): what is
// drawn at a point, for the symbols that are edited where they are drawn.
#pragma once

#include <elara/notation/caret.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace elara::notation {

/// A symbol on a display list: everything of one role drawn together for one
/// measure, such as the sharps of a key signature, the two numbers of a time
/// signature, or the note and the number of a tempo mark.
struct Symbol {
    Role role{};
    std::size_t measure = 0; ///< the measure it was laid out for
    Rect box;                ///< round its ink, in the list's staff spaces
    /// The staff it is drawn on, or the nearest one of its system, as an
    /// index into the list's staves; none on a list without staves.
    std::optional<std::size_t> staff;

    friend bool operator==(const Symbol&, const Symbol&) = default;
};

/// The symbols that are edited where they are drawn (ADR-0130): a clef, a key
/// and a time signature, a tempo mark, the name of a section, a chord symbol
/// and a beat's text.
inline constexpr std::array kEditedRoles{Role::Clef,    Role::KeySignature, Role::TimeSignature,
                                         Role::Tempo,   Role::Section,      Role::Chord,
                                         Role::BeatText};

/// How near the ink the pointer picks a symbol, in staff spaces: symbols are
/// small, and a hand is not that steady.
inline constexpr double kPickSlack = 0.4;

/// The symbol of one of `roles` drawn at `point`, or within `slack` of it; of
/// several there, the one whose piece under the point is the smallest. Empty
/// when none is. A glyph is where the font says its ink is; a text, where
/// layout assumes it is (textWidth()).
[[nodiscard]] std::optional<Symbol> symbolAt(const DisplayList& list, Point point,
                                             std::span<const Role> roles, const EngravingFont& font,
                                             double slack = kPickSlack);

} // namespace elara::notation
