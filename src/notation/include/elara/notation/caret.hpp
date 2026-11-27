// SPDX-License-Identifier: AGPL-3.0-or-later
// Where keyboard entry's caret (ADR-0021) shows on a laid-out score, and the
// caret a click on the score picks.
#pragma once

#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <optional>

namespace elara::notation {

/// A rectangle in a display list's coordinates: staff spaces, y down.
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Where `caret` shows on `list`, laid out from `song`: on its string, centred
/// over the fret number, on a tablature staff; on its lane, where the drum map
/// places the lane's kit piece, on a percussion staff (ADR-0035); across the
/// staff on other standard notation. When the list shows both views of the
/// staff, the caret goes on the tablature. A beat sits at its column; the free
/// slot after the last beat sits where its time falls between the columns
/// around it, and in a measure without notes near the start. Empty when the
/// list does not show the caret's measure on its staff, or the song has no such
/// track, staff or measure.
[[nodiscard]] std::optional<Rect> caretBox(const DisplayList& list, const core::Song& song,
                                           const core::Caret& caret, const EngravingFont& font);

/// The beat `caret` is on, across the staff it is on: a band as wide as the
/// caret's box and at the same place across, from above the first line to
/// below the last of every view of the caret's staff in its system, the
/// notation and the tablature when the list shows both. It says where in the
/// measure typing goes, as caretBox() says on which string. Empty when
/// caretBox() is.
[[nodiscard]] std::optional<Rect> caretColumn(const DisplayList& list, const core::Song& song,
                                              const core::Caret& caret, const EngravingFont& font);

/// The caret a click at `point` on `list` picks: in the measure under the point,
/// the staff nearest to it, on tablature the nearest string, on a percussion
/// staff the nearest lane (of lanes in the same place, the current one when the
/// caret is on it, otherwise the first), and the beat (or free slot) whose caret
/// box is nearest. The voice comes from `current`, and on other standard
/// notation the string too. Empty when no measure is under the point.
[[nodiscard]] std::optional<core::Caret> caretAt(const DisplayList& list, const core::Song& song,
                                                 Point point, const core::Caret& current,
                                                 const EngravingFont& font);

} // namespace elara::notation
