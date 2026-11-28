// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/edit_session.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/transpose.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace elara::core {

void EditSession::start(std::size_t track) noexcept {
    caret_ = Caret{.track = track};
    input_ = Duration{};
    typed_.reset();
    anchor_.reset();
}

void EditSession::showTrack(const Song& song, std::optional<std::size_t> track) {
    shown_ = track;
    fit(song);
}

void EditSession::songChanged(const Song& song) {
    typed_.reset();
    fit(song);
}

void EditSession::move(const Song& song, CaretMove move) {
    if (!hasCaret(song)) {
        return;
    }
    typed_.reset();
    anchor_.reset();
    caret_ = moveCaret(song, caret_, move);
    followBeat(song);
}

bool EditSession::setVoice(const Song& song, std::size_t voice) {
    if (!hasCaret(song) || voice >= Measure::kMaxVoices || voice == caret_.voice) {
        return false;
    }
    typed_.reset();
    caret_.voice = voice;
    caret_ = clampCaret(song, caret_);
    followBeat(song);
    return true;
}

void EditSession::place(const Song& song, const Caret& caret) {
    if (!hasCaret(song)) {
        return;
    }
    anchor_.reset();
    moveTo(song, caret);
}

void EditSession::moveTo(const Song& song, const Caret& caret) {
    typed_.reset();
    caret_ = clampCaret(song, caret);
    followBeat(song);
}

EditSession::Measures EditSession::selection() const noexcept {
    if (!anchor_) {
        return {.first = caret_.measure, .count = 1};
    }
    const std::size_t first = std::min(*anchor_, caret_.measure);
    return {.first = first, .count = std::max(*anchor_, caret_.measure) - first + 1};
}

void EditSession::extendSelection(const Song& song, long long measures) {
    if (!hasCaret(song) || measures == 0) {
        return;
    }
    if (!anchor_) {
        anchor_ = caret_.measure;
    }
    const auto last = static_cast<long long>(song.measureCount()) - 1;
    Caret caret = caret_;
    caret.measure = static_cast<std::size_t>(
        std::clamp(static_cast<long long>(caret_.measure) + measures, 0LL, last));
    caret.beat = 0;
    moveTo(song, caret);
}

void EditSession::selectAll(const Song& song) {
    if (!hasCaret(song)) {
        return;
    }
    anchor_ = song.measureCount() - 1;
    Caret caret = caret_;
    caret.measure = 0;
    caret.beat = 0;
    moveTo(song, caret);
}

bool EditSession::clearSelection() noexcept {
    const bool had = anchor_.has_value();
    anchor_.reset();
    return had;
}

EditSession::EditSession(std::shared_ptr<Clipboard> clipboard) {
    shareClipboard(std::move(clipboard));
}

void EditSession::shareClipboard(std::shared_ptr<Clipboard> clipboard) {
    clipboard_ = clipboard ? std::move(clipboard) : std::make_shared<Clipboard>();
}

EditSession::Measures EditSession::copy(const Song& song) {
    const Measures range = selection();
    // The track shown alone, or every track from the first.
    clipboard_->hold(
        copyMeasures(song, range.first, range.count, shown_ ? caret_.track : 0, shown_ ? 1 : 0));
    return range;
}

bool EditSession::paste(Document& document, PasteMode mode) {
    // Held while it is pasted, whatever else is copied meanwhile.
    const std::shared_ptr<const MeasureClip> clip = clipboard_->clip();
    if (!clip) {
        throw std::logic_error("paste: nothing was copied");
    }
    const std::size_t at = selection().first;
    // One copied track goes to the caret's; several to the tracks of the same
    // places from the first.
    const std::size_t track = clip->tracks.size() == 1 ? caret_.track : 0;
    std::unique_ptr<Command> command = makePaste(document.song(), *clip, at, track, mode);
    Caret caret = caret_;
    caret.measure = at + clip->measureCount() - 1;
    caret.beat = 0;
    anchor_ = at;
    try {
        return enter(document, Entry{.command = std::move(command), .caret = caret}).changed;
    } catch (...) {
        anchor_.reset();
        throw;
    }
}

bool EditSession::transpose(Document& document, int semitones) {
    if (semitones == 0 || !hasCaret(document.song())) {
        return false;
    }
    const Measures range = selection();
    Transposition what{.semitones = semitones, .first = range.first, .count = range.count};
    if (shown_) {
        what.tracks.push_back(caret_.track);
    }
    std::unique_ptr<Command> command = makeTranspose(document.song(), what);
    typed_.reset();
    document.execute(std::move(command));
    fit(document.song());
    return true;
}

EntryResult EditSession::enter(Document& document, Entry entry) {
    typed_.reset();
    caret_ = entry.caret;
    EntryResult result{.changed = false, .refused = entry.refused};
    try {
        if (entry.command && !entry.refused) {
            document.execute(std::move(entry.command));
            result.changed = true;
        }
    } catch (...) {
        fit(document.song());
        throw;
    }
    fit(document.song());
    return result;
}

EntryResult EditSession::typeDigit(Document& document, int digit, Clock::time_point now) {
    const std::optional<Typed> first = std::exchange(typed_, std::nullopt);
    const bool second = first && first->digit > 0 && first->caret == caret_ &&
                        first->revision == document.revision() &&
                        now - first->at <= kTwoDigitWindow;
    if (second) {
        // The first digit's note goes, so that both digits are one undo step.
        const bool undone = first->changed;
        if (undone) {
            document.undo();
        }
        const int fret = first->digit * 10 + digit;
        Entry two = enterFret(document.song(), caret_, fret, input_);
        if (!two.refused) {
            const EntryResult entered = enter(document, std::move(two));
            // Entering nothing, the song is still back as before the first digit.
            return EntryResult{.changed = entered.changed || undone};
        }
        // Off the neck: the first digit stays, and the second starts a fret.
        if (undone) {
            document.redo();
        }
    }
    const EntryResult result = enter(document, enterFret(document.song(), caret_, digit, input_));
    if (!result.refused) {
        typed_ = Typed{.digit = digit,
                       .caret = caret_,
                       .revision = document.revision(),
                       .changed = result.changed,
                       .at = now};
    }
    return result;
}

EntryResult EditSession::setInput(Document& document, Duration duration) {
    typed_.reset();
    input_ = duration;
    if (!hasCaret(document.song())) {
        return {};
    }
    return enter(document, setBeatDuration(document.song(), caret_, input_));
}

void EditSession::fit(const Song& song) {
    if (!hasCaret(song)) {
        return;
    }
    if (shown_) {
        caret_.track = *shown_;
    }
    caret_ = clampCaret(song, caret_);
    if (anchor_) {
        anchor_ = std::min(*anchor_, song.measureCount() - 1);
    }
}

void EditSession::followBeat(const Song& song) {
    if (onFreeSlot(song, caret_)) {
        return;
    }
    const Duration duration = song.track(caret_.track)
                                  .staff(caret_.staff)
                                  .measure(caret_.measure)
                                  .voices.at(caret_.voice)
                                  .beats.at(caret_.beat)
                                  .duration;
    const Tuplet tuplet = duration.tuplet();
    const bool offered =
        tuplet.isNone() || std::ranges::any_of(kInputTuplets, [&tuplet](const InputTuplet& item) {
            return item.actual == tuplet.actual() && item.normal == tuplet.normal();
        });
    const bool typed = std::ranges::find(kInputValues, duration.value()) != kInputValues.end();
    if (offered && typed && duration.dots() <= 1) {
        input_ = duration;
    }
}

} // namespace elara::core
