// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/edit_session.hpp>
#include <elara/core/measure.hpp>

#include <algorithm>
#include <utility>

namespace elara::core {

void EditSession::start(std::size_t track) noexcept {
    caret_ = Caret{.track = track};
    input_ = Duration{};
}

void EditSession::showTrack(const Song& song, std::optional<std::size_t> track) {
    shown_ = track;
    fit(song);
}

void EditSession::songChanged(const Song& song) {
    fit(song);
}

void EditSession::move(const Song& song, CaretMove move) {
    if (!hasCaret(song)) {
        return;
    }
    caret_ = moveCaret(song, caret_, move);
    followBeat(song);
}

bool EditSession::setVoice(const Song& song, std::size_t voice) {
    if (!hasCaret(song) || voice >= Measure::kMaxVoices || voice == caret_.voice) {
        return false;
    }
    caret_.voice = voice;
    caret_ = clampCaret(song, caret_);
    followBeat(song);
    return true;
}

void EditSession::place(const Song& song, const Caret& caret) {
    if (!hasCaret(song)) {
        return;
    }
    caret_ = clampCaret(song, caret);
    followBeat(song);
}

bool EditSession::enter(Document& document, Entry entry) {
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

bool EditSession::setInput(Document& document, Duration duration) {
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
