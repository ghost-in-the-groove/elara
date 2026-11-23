// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline rendering with the built-in synthesizer (ADR-0018).
#include <elara/audio/offline_renderer.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <cmath>

namespace elara::audio {

namespace {

constexpr float kKnee = 0.75f;                // the limiter leaves samples below this alone
constexpr std::size_t kParallelFrames = 8192; // frames x tracks worth starting threads for

// Soft knee: linear up to kKnee, then bending smoothly toward full scale.
float limit(float sample) noexcept {
    const float magnitude = std::abs(sample);
    if (magnitude <= kKnee) {
        return sample;
    }
    constexpr float kRoom = 1.0f - kKnee;
    return std::copysign(kKnee + kRoom * std::tanh((magnitude - kKnee) / kRoom), sample);
}

} // namespace

struct OfflineRenderer::TrackPlayer {
    struct Event {
        std::int64_t frame = 0;
        EventKind kind = EventKind::NoteOn;
        int key = 60;
        int velocity = 80;
    };

    Synth synth;
    std::vector<Event> events{};
    std::size_t next = 0;
    std::vector<float> buffer{};
    std::vector<LoggedEvent> dispatched{};

    // Renders frames [from, from + count) into `buffer`, dispatching each
    // event at its frame.
    void render(std::int64_t from, std::size_t count, std::size_t track, bool log) {
        buffer.assign(count, 0.0f);
        const std::span<float> out(buffer);
        const std::int64_t end = from + static_cast<std::int64_t>(count);
        std::size_t done = 0;
        for (; next < events.size() && events[next].frame < end; ++next) {
            const Event& event = events[next];
            const auto at = static_cast<std::size_t>(std::max<std::int64_t>(event.frame - from, 0));
            if (at > done) {
                synth.render(out.subspan(done, at - done));
                done = at;
            }
            if (event.kind == EventKind::NoteOn) {
                synth.noteOn(event.key, event.velocity);
            } else {
                synth.noteOff(event.key);
            }
            if (log) {
                dispatched.push_back({.frame = event.frame,
                                      .kind = event.kind,
                                      .track = track,
                                      .key = event.key,
                                      .velocity = event.velocity});
            }
        }
        synth.render(out.subspan(done));
    }
};

OfflineRenderer::OfflineRenderer(const core::Song& song, const Sequence& sequence,
                                 RenderOptions options)
    : options_(options) {
    const double rate = options_.sampleRate;
    frames_ = std::llround((sequence.duration() + std::max(0.0, options_.tail)) * rate);
    const std::size_t tracks = song.trackCount();
    timbres_.reserve(tracks);
    players_.reserve(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        timbres_.push_back(timbreFor(song.track(t).instrument()));
        players_.push_back(TrackPlayer{.synth = Synth(timbres_.back(), rate)});
    }
    for (const NoteEvent& event : sequence.events()) {
        if (event.track < tracks) {
            players_[event.track].events.push_back({.frame = std::llround(event.time * rate),
                                                    .kind = event.kind,
                                                    .key = event.key,
                                                    .velocity = event.velocity});
        }
    }
    const auto sounding = std::ranges::count_if(
        players_, [](const TrackPlayer& player) { return !player.events.empty(); });
    trackGain_ = 1.0f / std::sqrt(static_cast<float>(std::max<std::ptrdiff_t>(sounding, 1)));
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
    mix_.assign(count, 0.0f);
    for (const TrackPlayer& player : players_) {
        for (std::size_t i = 0; i < count; ++i) {
            mix_[i] += player.buffer[i];
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const float sample = limit(mix_[i] * trackGain_);
        out[2 * i] = sample;
        out[2 * i + 1] = sample;
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
