// SPDX-License-Identifier: AGPL-3.0-or-later
// The steps of `elara-cli edit`: each parsed and run against a document as an
// undoable edit (ADR-0005). The edit subcommand reads them from its arguments
// and prints the result; tests run them directly.
#pragma once

#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>

#include <stdexcept>
#include <string_view>

namespace elara::cli {

/// A step that is not well formed (as opposed to one the song rejects): the
/// edit subcommand exits with 2 for it and with 1 for any other exception.
class StepSyntaxError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/// Keyboard entry across steps (ADR-0021): the caret and the input duration.
struct EntryState {
    core::Caret caret{};
    core::Duration input{core::NoteValue::Quarter};
};

/// What a run of steps carries from one step to the next.
struct StepState {
    EntryState entry;
    core::MeasureClip clipboard; ///< what the last copy step copied
};

/// True when the song has a place for the caret: a track and a measure.
[[nodiscard]] bool hasCaret(const core::Song& song);

/// Runs one step, its words separated by spaces ("repeat 1 end 3"), on `doc`,
/// then brings the caret back into the song. Throws StepSyntaxError for a
/// malformed step, and what the song's edits throw for one it rejects.
void runStep(core::Document& doc, std::string_view text, StepState& state);

} // namespace elara::cli
