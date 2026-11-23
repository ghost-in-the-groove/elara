// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>

#include <stdexcept>
#include <utility>

namespace elara::core {

Document::Document(Song song, std::size_t undoLimit)
    : song_(std::move(song)), undoLimit_(undoLimit) {}

void Document::execute(std::unique_ptr<Command> command) {
    if (!command) {
        throw std::invalid_argument("Document: null command");
    }
    const std::size_t depth = done_.size();
    done_.push_back(nullptr); // allocate first: nothing can fail after apply()
    try {
        command->apply(song_);
    } catch (...) {
        done_.pop_back();
        throw;
    }
    done_.back() = std::move(command);
    if (savedDepth_ && *savedDepth_ > depth) {
        savedDepth_.reset(); // the saved state was on the discarded redo branch
    }
    undone_.clear();
    while (done_.size() > undoLimit_) {
        done_.pop_front();
        if (savedDepth_) {
            savedDepth_ = *savedDepth_ == 0 ? std::nullopt : std::optional(*savedDepth_ - 1);
        }
    }
}

std::string_view Document::undoName() const noexcept {
    return done_.empty() ? std::string_view{} : done_.back()->name();
}

std::string_view Document::redoName() const noexcept {
    return undone_.empty() ? std::string_view{} : undone_.back()->name();
}

void Document::undo() {
    if (done_.empty()) {
        throw std::logic_error("Document: nothing to undo");
    }
    undone_.reserve(undone_.size() + 1);
    done_.back()->revert(song_);
    undone_.push_back(std::move(done_.back()));
    done_.pop_back();
}

void Document::redo() {
    if (undone_.empty()) {
        throw std::logic_error("Document: nothing to redo");
    }
    done_.push_back(nullptr);
    try {
        undone_.back()->apply(song_);
    } catch (...) {
        done_.pop_back();
        throw;
    }
    done_.back() = std::move(undone_.back());
    undone_.pop_back();
}

} // namespace elara::core
