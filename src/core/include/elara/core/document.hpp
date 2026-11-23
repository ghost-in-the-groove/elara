// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/song.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

/// One reversible edit of a song (ADR-0005). Commands exchange data with the
/// song (swap, move) instead of copying it, so the history only holds what the
/// song no longer holds.
class Command {
public:
    Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;
    virtual ~Command() = default;

    /// Applies the edit. Checks everything that can fail before changing the
    /// song, so a throwing apply() leaves it unchanged.
    virtual void apply(Song& song) = 0;

    /// Undoes apply() exactly. Only called on the state apply() produced.
    virtual void revert(Song& song) = 0;

    /// Short English name for the Edit menu, e.g. "Insert measures".
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

/// A song being edited: the song, its undo and redo history and its save point.
/// The song can only be changed through commands, so every edit is undoable.
class Document {
public:
    static constexpr std::size_t kDefaultUndoLimit = 1000;

    Document() = default;
    explicit Document(Song song, std::size_t undoLimit = kDefaultUndoLimit);

    [[nodiscard]] const Song& song() const noexcept { return song_; }

    /// Applies `command` and records it as the newest undo step, discarding what
    /// could be redone. Beyond the undo limit the oldest step is dropped. If
    /// apply() throws, the exception propagates and nothing changes.
    void execute(std::unique_ptr<Command> command);

    [[nodiscard]] bool canUndo() const noexcept { return !done_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !undone_.empty(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return done_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return undone_.size(); }
    [[nodiscard]] std::size_t undoLimit() const noexcept { return undoLimit_; }

    /// Name of the step undo() or redo() would apply; empty when there is none.
    [[nodiscard]] std::string_view undoName() const noexcept;
    [[nodiscard]] std::string_view redoName() const noexcept;

    /// Throw std::logic_error when there is nothing to undo or redo.
    void undo();
    void redo();

    /// Marks the current state as saved: isModified() stays false until the song
    /// leaves this state, and is false again whenever undo or redo returns to it.
    void markSaved() noexcept { savedDepth_ = done_.size(); }
    [[nodiscard]] bool isModified() const noexcept {
        return !savedDepth_ || *savedDepth_ != done_.size();
    }

private:
    Song song_;
    std::deque<std::unique_ptr<Command>> done_;    // oldest first
    std::vector<std::unique_ptr<Command>> undone_; // next to redo last
    std::size_t undoLimit_ = kDefaultUndoLimit;
    std::optional<std::size_t> savedDepth_ = 0; // undoCount() at the save point, if reachable
};

} // namespace elara::core
