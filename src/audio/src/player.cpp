// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback with the built-in synthesizer (ADR-0019).
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/song.hpp>

#include "playback.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace elara::audio {

/// A song ready for the audio thread: everything render() needs, allocated on
/// the control thread.
struct Player::Loaded {
    struct Track {
        Synth synth;
        std::vector<detail::FrameEvent> events{};
        std::size_t next = 0;
        std::vector<float> buffer{};
        detail::TrackLevel applied{}; // audio thread: the level last mixed at
    };

    explicit Loaded(std::size_t trackCount) : levels(2 * trackCount) {}

    std::vector<Track> tracks{};
    std::vector<float> left{};
    std::vector<float> right{};
    std::int64_t endFrame = 0;
    float headroom = 1.0f;
    // Each track's left and right level: written by the control thread
    // (updateMix), read by the audio thread at the start of each block.
    std::vector<std::atomic<float>> levels;

    void setLevel(std::size_t t, detail::TrackLevel level) noexcept {
        levels[2 * t].store(level.left, std::memory_order_relaxed);
        levels[(2 * t) + 1].store(level.right, std::memory_order_relaxed);
    }

    [[nodiscard]] detail::TrackLevel level(std::size_t t) const noexcept {
        return {.left = levels[2 * t].load(std::memory_order_relaxed),
                .right = levels[(2 * t) + 1].load(std::memory_order_relaxed)};
    }

    // Points every track at the first event at or after `frame`.
    void seat(std::int64_t frame) noexcept {
        for (Track& track : tracks) {
            track.next = detail::firstAt(track.events, frame);
        }
    }
};

Player::Player(double sampleRate, std::size_t maxBlockFrames)
    : sampleRate_(sampleRate), maxBlockFrames_(std::max<std::size_t>(maxBlockFrames, 1)) {}

Player::~Player() {
    delete current_;
    delete pending_.load();
    delete retired_.load();
}

void Player::load(const core::Song& song, const Sequence& sequence) {
    const std::size_t tracks = song.trackCount();
    auto loaded = std::make_unique<Loaded>(tracks);
    auto events = detail::eventsByTrack(sequence, tracks, sampleRate_);
    loaded->headroom = detail::headroom(events);
    loaded->endFrame = std::llround(sequence.duration() * sampleRate_);
    loaded->left.assign(maxBlockFrames_, 0.0f);
    loaded->right.assign(maxBlockFrames_, 0.0f);
    loaded->tracks.reserve(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        const detail::TrackLevel level = detail::levelOf(song, t, loaded->headroom);
        loaded->setLevel(t, level);
        loaded->tracks.push_back(
            {.synth = Synth(timbreFor(song.track(t).instrument()), sampleRate_),
             .events = std::move(events[t]),
             .buffer = std::vector<float>(maxBlockFrames_, 0.0f),
             .applied = level});
    }
    duration_ = sequence.duration();
    tidy();
    latest_ = loaded.get();
    // A song the audio thread has not taken yet is replaced. The release
    // publishes the levels stored above with the song.
    delete pending_.exchange(loaded.release(), std::memory_order_acq_rel);
}

void Player::updateMix(const core::Song& song) noexcept {
    if (latest_ == nullptr || song.trackCount() != latest_->tracks.size()) {
        return;
    }
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        latest_->setLevel(t, detail::levelOf(song, t, latest_->headroom));
    }
}

void Player::play() noexcept {
    if (position() >= duration_) {
        seek(0.0);
    }
    playing_.store(true, std::memory_order_release);
}

void Player::pause() noexcept {
    playing_.store(false, std::memory_order_release);
}

void Player::seek(double seconds) noexcept {
    const double clamped = std::clamp(seconds, 0.0, duration_);
    seekTo_.store(std::llround(clamped * sampleRate_), std::memory_order_release);
}

bool Player::isPlaying() const noexcept {
    return playing_.load(std::memory_order_acquire);
}

double Player::position() const noexcept {
    const std::int64_t asked = seekTo_.load(std::memory_order_acquire);
    const std::int64_t frame = asked >= 0 ? asked : position_.load(std::memory_order_acquire);
    return static_cast<double>(frame) / sampleRate_;
}

void Player::tidy() noexcept {
    delete retired_.exchange(nullptr, std::memory_order_acq_rel);
}

void Player::render(std::span<float> left, std::span<float> right) noexcept {
    // Take a newly loaded song, once the control thread has freed the last
    // one let go of.
    if (retired_.load(std::memory_order_acquire) == nullptr) {
        if (Loaded* next = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            retired_.store(current_, std::memory_order_release);
            current_ = next;
            current_->seat(frame_);
        }
    }
    if (const std::int64_t asked = seekTo_.exchange(-1, std::memory_order_acq_rel); asked >= 0) {
        frame_ = asked;
        if (current_ != nullptr) {
            for (Loaded::Track& track : current_->tracks) {
                track.synth.reset();
            }
            current_->seat(frame_);
        }
    }
    const bool playing = playing_.load(std::memory_order_acquire);
    if (current_ != nullptr && wasPlaying_ && !playing) {
        for (Loaded::Track& track : current_->tracks) {
            track.synth.releaseAll();
        }
    }
    wasPlaying_ = playing;

    const std::size_t frames = std::min(left.size(), right.size());
    for (std::size_t done = 0; done < frames;) {
        const std::size_t n = std::min(frames - done, maxBlockFrames_);
        renderBlock(left.subspan(done, n), right.subspan(done, n));
        done += n;
    }
    position_.store(frame_, std::memory_order_release);
}

void Player::renderBlock(std::span<float> left, std::span<float> right) noexcept {
    if (current_ == nullptr) {
        std::ranges::fill(left, 0.0f);
        std::ranges::fill(right, 0.0f);
        return;
    }
    const std::size_t n = left.size();
    const bool playing = wasPlaying_;
    const std::span<float> mixLeft(current_->left.data(), n);
    const std::span<float> mixRight(current_->right.data(), n);
    std::ranges::fill(mixLeft, 0.0f);
    std::ranges::fill(mixRight, 0.0f);
    for (std::size_t t = 0; t < current_->tracks.size(); ++t) {
        Loaded::Track& track = current_->tracks[t];
        const std::span<float> out(track.buffer.data(), n);
        std::ranges::fill(out, 0.0f);
        if (playing) {
            detail::playTrack(track.synth, track.events, track.next, frame_, out,
                              [](const detail::FrameEvent&) noexcept {});
        } else {
            track.synth.render(out); // released notes ringing out
        }
        const detail::TrackLevel target = current_->level(t);
        detail::mixInto(out, track.applied, target, mixLeft, mixRight);
        track.applied = target;
    }
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = detail::limit(mixLeft[i]);
        right[i] = detail::limit(mixRight[i]);
    }
    if (playing) {
        frame_ += static_cast<std::int64_t>(n);
        if (frame_ >= current_->endFrame) {
            // The end: stop and let the last notes ring out.
            frame_ = current_->endFrame;
            playing_.store(false, std::memory_order_release);
            wasPlaying_ = false;
            for (Loaded::Track& track : current_->tracks) {
                track.synth.releaseAll();
            }
        }
    }
}

} // namespace elara::audio
