// SPDX-License-Identifier: AGPL-3.0-or-later
// What ScoreDocument and CaretEditor both need: the model's text for the
// interface, and what kind of staff a caret is on.
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace elara::app {

/// Text of the model, which is UTF-8, as the interface's.
[[nodiscard]] inline QString qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// 1 for a whole note, 2 for a half note... 64.
[[nodiscard]] inline int denominatorOf(core::NoteValue value) {
    const auto steps = static_cast<int>(value) - static_cast<int>(core::NoteValue::Whole);
    return steps < 0 ? 0 : 1 << steps;
}

/// A name as the core writes it and the text the interface shows for it,
/// marked for translation.
struct Translation {
    std::string_view key;
    const char* text;
};

/// The text of `key` in `table`, translated in `context`; `fallback` as the
/// core writes it for a name the table lacks.
template <std::size_t N>
[[nodiscard]] QString translated(const char* context, const std::array<Translation, N>& table,
                                 std::string_view key, std::string_view fallback) {
    const auto found = std::ranges::find(table, key, &Translation::key);
    return found == table.end() ? qstring(fallback)
                                : QCoreApplication::translate(context, found->text);
}

/// Whether the caret's staff has strings, so that digits type frets on it;
/// false for a caret on a track or a staff the song does not have.
[[nodiscard]] inline bool onStrings(const core::Song& song, const core::Caret& caret) {
    if (caret.track >= song.trackCount()) {
        return false;
    }
    const core::Track& track = song.track(caret.track);
    return caret.staff < track.staffCount() && track.staff(caret.staff).isFretted();
}

/// Whether the caret is on a drum staff, where its string is a lane
/// (ADR-0035); false for a caret on a track or a staff the song does not have.
[[nodiscard]] inline bool onDrumStaff(const core::Song& song, const core::Caret& caret) {
    if (caret.track >= song.trackCount()) {
        return false;
    }
    const core::Track& track = song.track(caret.track);
    return caret.staff < track.staffCount() &&
           track.staff(caret.staff).clef() == core::Clef::Percussion;
}

} // namespace elara::app
