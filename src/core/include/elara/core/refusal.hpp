// SPDX-License-Identifier: AGPL-3.0-or-later
// Why an edit at the caret was not made (ADR-0180).
#pragma once

#include <elara/core/names.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace elara::core {

/// Why an edit at the caret was not made: what the song, the staff or the note
/// lacks for it. A refused edit changes nothing. Each front end says why in its
/// own words: the app in the interface's language, the command line by name.
///
/// An edit that changes nothing because what it asks is so already is not
/// refused.
enum class Refusal : std::uint8_t {
    /// Nothing at the caret takes the edit: a rest, the free slot or a string
    /// without a note for what goes on notes (ADR-0123), the free slot for what
    /// goes on beats, no note after the caret for a dynamic.
    NothingAtCaret,
    /// A fret, a technique of strings, a harmonic or a plucking finger on a
    /// staff without strings.
    NeedsStrings,
    /// A drum hit on a staff that is not a percussion staff.
    NeedsDrums,
    /// A note name on a percussion staff.
    NoNoteNames,
    /// No string of the staff can play the note.
    NoString,
    /// A fret past the neck, or a note moved off it.
    OffTheNeck,
    /// A note outside MIDI 0..127.
    OutOfRange,
    /// A tie with no note before it to continue.
    NoNoteToTie,
    /// A natural harmonic at a fret near no node.
    NoNaturalHarmonic,
    /// A trill more than an octave away.
    TrillTooWide,
};
template <>
struct EnumNames<Refusal> {
    static constexpr auto kNames = std::to_array<std::string_view>(
        {"nothing-at-caret", "needs-strings", "needs-drums", "no-note-names", "no-string",
         "off-the-neck", "out-of-range", "no-note-to-tie", "no-natural-harmonic",
         "trill-too-wide"});
};
static_assert(namesEnd(Refusal::TrillTooWide));

} // namespace elara::core
