// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>

#include <stdexcept>
#include <utility>

namespace elara::core {

Document::Document(Song song, std::size_t undoLimit, std::size_t undoBytes)
    : song_(std::move(song)), undoLimit_(undoLimit), undoBytes_(undoBytes) {}

void Document::execute(std::unique_ptr<Command> command) {
    if (!command) {
        throw std::invalid_argument("Document: null command");
    }
    done_.emplace_back(); // allocate first: nothing can fail after apply()
    try {
        command->apply(song_);
    } catch (...) {
        done_.pop_back();
        throw;
    }
    // A saved state on the redo branch discarded here is out of reach for
    // good: no state takes its revision again.
    lastChanges_ = command->changes();
    const std::size_t bytes = command->heldBytes();
    done_.back() = Step{.command = std::move(command), .revision = next_++, .bytes = bytes};
    for (const Step& step : undone_) {
        historyBytes_ -= step.bytes;
    }
    undone_.clear();
    historyBytes_ += bytes;
    // The newest step stays however much it holds: the last edit can always
    // be undone.
    while (done_.size() > undoLimit_ || (historyBytes_ > undoBytes_ && done_.size() > 1)) {
        base_ = done_.front().revision; // what undoing every step now returns to
        historyBytes_ -= done_.front().bytes;
        done_.pop_front();
    }
}

void Document::remeasure(Step& step) noexcept {
    historyBytes_ -= step.bytes;
    step.bytes = step.command->heldBytes();
    historyBytes_ += step.bytes;
}

std::string_view Document::undoName() const noexcept {
    return done_.empty() ? std::string_view{} : done_.back().command->name();
}

std::string_view Document::redoName() const noexcept {
    return undone_.empty() ? std::string_view{} : undone_.back().command->name();
}

void Document::undo() {
    if (done_.empty()) {
        throw std::logic_error("Document: nothing to undo");
    }
    undone_.reserve(undone_.size() + 1);
    done_.back().command->revert(song_);
    lastChanges_ = done_.back().command->changes();
    remeasure(done_.back());
    undone_.push_back(std::move(done_.back()));
    done_.pop_back();
}

void Document::redo() {
    if (undone_.empty()) {
        throw std::logic_error("Document: nothing to redo");
    }
    done_.emplace_back();
    try {
        undone_.back().command->apply(song_);
    } catch (...) {
        done_.pop_back();
        throw;
    }
    lastChanges_ = undone_.back().command->changes();
    remeasure(undone_.back());
    done_.back() = std::move(undone_.back());
    undone_.pop_back();
}

} // namespace elara::core
