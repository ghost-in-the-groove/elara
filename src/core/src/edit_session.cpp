// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/edit_session.hpp>
#include <elara/core/measure.hpp>

#include <algorithm>
#include <exception>
#include <utility>

namespace elara::core {

void EditSession::start(std::size_t track) noexcept {
    caret_ = Caret{.track = track};
    input_ = Duration{};
    typed_.reset();
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
    typed_.reset();
    caret_ = clampCaret(song, caret);
    followBeat(song);
}

bool EditSession::enter(Document& document, Entry entry) {
    typed_.reset();
    caret_ = entry.caret;
    bool changed = false;
    try {
        if (entry.command) {
            document.execute(std::move(entry.command));
            changed = true;
        }
    } catch (...) {
        fit(document.song());
        throw;
    }
    fit(document.song());
    return changed;
}

bool EditSession::typeDigit(Document& document, int digit, Clock::time_point now) {
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
        try {
            const int fret = first->digit * 10 + digit;
            const bool entered = enter(document, enterFret(document.song(), caret_, fret, input_));
            // Entering nothing, the song is still back as before the first digit.
            return entered || undone;
        } catch (const std::exception&) {
            // Off the neck: the first digit stays, and the second starts a fret.
            if (undone) {
                document.redo();
            }
        }
    }
    const bool changed = enter(document, enterFret(document.song(), caret_, digit, input_));
    typed_ = Typed{.digit = digit,
                   .caret = caret_,
                   .revision = document.revision(),
                   .changed = changed,
                   .at = now};
    return changed;
}

bool EditSession::setInput(Document& document, Duration duration) {
    typed_.reset();
    input_ = duration;
    if (!hasCaret(document.song())) {
        return false;
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
