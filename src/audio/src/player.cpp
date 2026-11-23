// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback with the built-in synthesizer (ADR-0019).
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/song.hpp>

#include "playback.hpp"

#include <algorithm>
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
    };

    std::vector<Track> tracks{};
    std::vector<float> mix{};
    std::int64_t endFrame = 0;
    float gain = 1.0f;

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
    auto loaded = std::make_unique<Loaded>();
    const std::size_t tracks = song.trackCount();
    auto events = detail::eventsByTrack(sequence, tracks, sampleRate_);
    loaded->gain = detail::trackGain(events);
    loaded->endFrame = std::llround(sequence.duration() * sampleRate_);
    loaded->mix.assign(maxBlockFrames_, 0.0f);
    loaded->tracks.reserve(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        loaded->tracks.push_back(
            {.synth = Synth(timbreFor(song.track(t).instrument()), sampleRate_),
             .events = std::move(events[t]),
             .buffer = std::vector<float>(maxBlockFrames_, 0.0f)});
    }
    duration_ = sequence.duration();
    tidy();
    // A song the audio thread has not taken yet is replaced.
    delete pending_.exchange(loaded.release(), std::memory_order_acq_rel);
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
    std::span<float> mix(current_->mix.data(), n);
    std::ranges::fill(mix, 0.0f);
    for (Loaded::Track& track : current_->tracks) {
        const std::span<float> out(track.buffer.data(), n);
        std::ranges::fill(out, 0.0f);
        if (playing) {
            detail::playTrack(track.synth, track.events, track.next, frame_, out,
                              [](const detail::FrameEvent&) noexcept {});
        } else {
            track.synth.render(out); // released notes ringing out
        }
        for (std::size_t i = 0; i < n; ++i) {
            mix[i] += out[i];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float sample = detail::limit(mix[i] * current_->gain);
        left[i] = sample;
        right[i] = sample;
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
