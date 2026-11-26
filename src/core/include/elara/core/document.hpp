// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/song.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

/// What an edit changes (ADR-0111), so that what is derived from the song,
/// its pages and the sequence playback plays, is made again only when the
/// edit can have changed it. Both are true when that is not known.
struct Changes {
    bool notation = true; ///< what the pages show
    bool sound = true;    ///< what playback plays

    [[nodiscard]] constexpr Changes operator|(Changes other) const noexcept {
        return {.notation = notation || other.notation, .sound = sound || other.sound};
    }
    friend constexpr bool operator==(Changes, Changes) noexcept = default;
};

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

    /// What apply() and revert() change, valid once apply() ran: everything
    /// unless the command knows better.
    [[nodiscard]] virtual Changes changes() const noexcept { return {}; }
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

    /// What the last execute(), undo() or redo() changed; everything before
    /// any of them.
    [[nodiscard]] Changes lastChanges() const noexcept { return lastChanges_; }

    /// Names the song's present state: undo and redo return to a state and to
    /// its revision, and no other state ever has it. A save running on a
    /// worker reads it as it starts, and marks that state saved as it ends.
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return done_.empty() ? base_ : done_.back().revision;
    }

    /// Marks the current state as saved: isModified() stays false until the song
    /// leaves this state, and is false again whenever undo or redo returns to it.
    void markSaved() noexcept { saved_ = revision(); }
    /// Marks the state that revision() named as `state` saved, though the song
    /// may have left it since.
    void markSaved(std::uint64_t state) noexcept { saved_ = state; }
    /// Marks the song as held by no file in its present state, as a song
    /// recovered after a crash is: isModified() is true until markSaved().
    void markUnsaved() noexcept { saved_.reset(); }
    [[nodiscard]] bool isModified() const noexcept { return !saved_ || *saved_ != revision(); }

private:
    /// A command and the revision of the state it makes.
    struct Step {
        std::unique_ptr<Command> command;
        std::uint64_t revision = 0;
    };

    Song song_;
    std::deque<Step> done_;    // oldest first
    std::vector<Step> undone_; // next to redo last
    std::size_t undoLimit_ = kDefaultUndoLimit;
    std::uint64_t base_ = 0;                 ///< the state before the oldest step kept
    std::uint64_t next_ = 1;                 ///< the revision of the next command executed
    std::optional<std::uint64_t> saved_ = 0; ///< the state saved; none when no state is
    Changes lastChanges_;
};

} // namespace elara::core
