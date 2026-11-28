// SPDX-License-Identifier: AGPL-3.0-or-later
// The steps of `elara-cli edit`: each parsed and run against a document as an
// undoable edit (ADR-0005). The edit subcommand reads them from its arguments
// and prints the result; tests run them directly.
#pragma once

#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>
#include <elara/core/edit_session.hpp>

#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace elara::cli {

/// A step that is not well formed (as opposed to one the song rejects): the
/// edit subcommand exits with 2 for it and with 1 for any other exception.
class StepSyntaxError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/// What a run of steps carries from one step to the next.
struct StepState {
    /// Keyboard entry across steps (ADR-0021): the caret and the input
    /// duration, in the session the app's keys use too (ADR-0127).
    core::EditSession session;
    /// What the last copy step copied. The copy and paste steps name their
    /// measures and tracks, so they do not use the session's selection.
    core::MeasureClip clipboard;
    /// Songs open beside the one edited, numbered from 1, that a copy step
    /// copies from with `from <song>` (ADR-0160).
    std::vector<core::Song> sources;
};

/// Every form of step runStep() runs, as the edit subcommand's help lists
/// them: the verb, then what it takes ("time <m> <N/D>"). A step whose verb no
/// form starts with is unknown.
[[nodiscard]] std::span<const std::string_view> stepForms() noexcept;

/// Runs one step, its words separated by spaces ("repeat 1 end 3"), on `doc`,
/// then brings the caret back into the song. Throws StepSyntaxError for a
/// malformed step, and what the song's edits throw for one it rejects.
void runStep(core::Document& doc, std::string_view text, StepState& state);

} // namespace elara::cli
