// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline rendering with the built-in synthesizer (ADR-0018).
#include <elara/audio/offline_renderer.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/song.hpp>

#include "playback.hpp"

#include <algorithm>
#include <cmath>

namespace elara::audio {

namespace {

constexpr std::size_t kParallelFrames = 8192; // frames x tracks worth starting threads for

} // namespace

struct OfflineRenderer::TrackPlayer {
    Synth synth;
    std::vector<detail::FrameEvent> events{};
    std::size_t next = 0;
    std::vector<float> buffer{};
    std::vector<LoggedEvent> dispatched{};

    void render(std::int64_t from, std::size_t count, std::size_t track, bool log) {
        buffer.assign(count, 0.0f);
        detail::playTrack(synth, events, next, from, buffer, [&](const detail::FrameEvent& event) {
            if (log) {
                dispatched.push_back({.frame = event.frame,
                                      .kind = event.kind,
                                      .touch = event.touch,
                                      .track = track,
                                      .key = event.key,
                                      .velocity = event.velocity});
            }
        });
    }
};

OfflineRenderer::OfflineRenderer(const core::Song& song, const Sequence& sequence,
                                 RenderOptions options)
    : options_(options) {
    const double rate = options_.sampleRate;
    frames_ = std::llround((sequence.duration() + std::max(0.0, options_.tail)) * rate);
    const std::size_t tracks = song.trackCount();
    auto events = detail::eventsByTrack(sequence, tracks, rate);
    const float headroom = detail::headroom(events);
    levels_.reserve(2 * tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        const detail::TrackLevel level = detail::levelOf(song, t, headroom);
        levels_.push_back(level.left);
        levels_.push_back(level.right);
    }
    timbres_.reserve(tracks);
    players_.reserve(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        timbres_.push_back(timbreFor(song.track(t).instrument()));
        players_.push_back(
            TrackPlayer{.synth = Synth(timbres_.back(), rate), .events = std::move(events[t])});
    }
}

OfflineRenderer::~OfflineRenderer() = default;
OfflineRenderer::OfflineRenderer(OfflineRenderer&&) noexcept = default;
OfflineRenderer& OfflineRenderer::operator=(OfflineRenderer&&) noexcept = default;

std::size_t OfflineRenderer::render(std::span<float> out) {
    const auto count = static_cast<std::size_t>(
        std::min(static_cast<std::int64_t>(out.size() / 2), frames_ - position_));
    if (count == 0) {
        return 0;
    }
    const unsigned threads = core::threadsFor(options_.threads, count * players_.size(),
                                              kParallelFrames, players_.size());
    core::forEachIndex(0, players_.size(), threads, [&](std::size_t t) {
        players_[t].render(position_, count, t, options_.logEvents);
    });

    // Mix in track order, so the sums do not depend on the threads.
    mix_.assign(2 * count, 0.0f);
    const std::span<float> left(mix_.data(), count);
    const std::span<float> right(mix_.data() + count, count);
    for (std::size_t t = 0; t < players_.size(); ++t) {
        const detail::TrackLevel level{.left = levels_[2 * t], .right = levels_[(2 * t) + 1]};
        detail::mixInto(players_[t].buffer, level, level, left, right);
    }
    for (std::size_t i = 0; i < count; ++i) {
        out[2 * i] = detail::limit(left[i]);
        out[(2 * i) + 1] = detail::limit(right[i]);
    }

    if (options_.logEvents) {
        const std::size_t first = log_.size();
        for (TrackPlayer& player : players_) {
            log_.insert(log_.end(), player.dispatched.begin(), player.dispatched.end());
            player.dispatched.clear();
        }
        std::stable_sort(
            log_.begin() + static_cast<std::ptrdiff_t>(first), log_.end(),
            [](const LoggedEvent& a, const LoggedEvent& b) { return a.frame < b.frame; });
    }
    position_ += static_cast<std::int64_t>(count);
    return count;
}

std::vector<float> OfflineRenderer::renderRest() {
    std::vector<float> out(2 * static_cast<std::size_t>(frames_ - position_));
    static_cast<void>(render(out));
    return out;
}

} // namespace elara::audio
