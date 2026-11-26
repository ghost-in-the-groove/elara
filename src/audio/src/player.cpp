// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback (ADR-0019), through a sound bank (ADR-0043).
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sound_bank.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include "playback.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace elara::audio {

/// A song ready for the audio thread: everything render() needs, allocated on
/// the control thread.
/// A track's sound and the instrument it was made for, kept across loads.
struct Player::KeptSound {
    core::Instrument instrument;
    std::shared_ptr<TrackSound> sound;

    /// Whether a track of `other` sounds as this one: the same kind and
    /// program, whatever its name.
    [[nodiscard]] bool fits(const core::Instrument& other) const noexcept {
        return other.kind == instrument.kind && other.midiProgram == instrument.midiProgram;
    }
};

struct Player::Loaded {
    struct Track {
        std::shared_ptr<TrackSound> sound; // shared with the next load when kept
        std::vector<detail::FrameEvent> events{};
        std::size_t next = 0;
        std::vector<float> buffer{};
        detail::TrackLevel applied{}; // audio thread: the level last mixed at
    };

    // `players`: the song's tracks, and the metronome when it clicks.
    Loaded(std::size_t trackCount, std::size_t players, double rate, std::size_t maxFrames)
        : songTracks(trackCount), countIn(Timbre::Drums, rate), countInBuffer(maxFrames, 0.0f),
          levels(2 * players) {}

    std::size_t songTracks = 0;
    std::vector<Track> tracks{}; // the song's, then the metronome's if any
    Synth countIn;               // the clicks before playback starts
    std::vector<float> countInBuffer{};
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
    : sampleRate_(sampleRate), maxBlockFrames_(std::max<std::size_t>(maxBlockFrames, 1)),
      bank_(builtinBank()) {}

void Player::setSoundBank(std::shared_ptr<const SoundBank> bank) {
    if (!bank) {
        bank = builtinBank();
    }
    if (bank != bank_) {
        bank_ = std::move(bank);
        kept_.clear();
    }
}

Player::~Player() {
    delete current_;
    delete pending_.load();
    delete retired_.load();
}

struct PreparedSong::Parts {
    std::unique_ptr<Player::Loaded> loaded;
    std::vector<Player::KeptSound> kept;
    double duration = 0.0;
    double sampleRate = 0.0;
};

PreparedSong::PreparedSong(std::unique_ptr<Parts> parts) : parts_(std::move(parts)) {}
PreparedSong::PreparedSong(PreparedSong&&) noexcept = default;
PreparedSong& PreparedSong::operator=(PreparedSong&&) noexcept = default;
PreparedSong::~PreparedSong() = default;

namespace {

// Prepares `song` as load() does for a player at `sampleRate` that keeps the
// sounds `keptBefore`, getting new ones from `bank`. Runs on any thread.
PreparedSong::Parts prepareSong(const std::vector<Player::KeptSound>& keptBefore,
                                const std::shared_ptr<const SoundBank>& bank, double sampleRate,
                                std::size_t maxBlockFrames, const core::Song& song,
                                const Sequence& sequence) {
    const std::size_t tracks = song.trackCount();
    auto events = detail::eventsByTrack(sequence, tracks + 1, sampleRate); // the metronome's last
    const bool clicks = !events[tracks].empty();
    auto loaded = std::make_unique<Player::Loaded>(tracks, clicks ? tracks + 1 : tracks, sampleRate,
                                                   maxBlockFrames);
    loaded->headroom = detail::headroom(std::span(events).first(tracks));
    loaded->endFrame = std::llround(sequence.duration() * sampleRate);
    loaded->left.assign(maxBlockFrames, 0.0f);
    loaded->right.assign(maxBlockFrames, 0.0f);
    loaded->tracks.reserve(tracks);
    std::vector<Player::KeptSound> kept;
    kept.reserve(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        const core::Instrument& instrument = song.track(t).instrument();
        if (t < keptBefore.size() && keptBefore[t].fits(instrument)) {
            kept.push_back(keptBefore[t]);
        } else {
            kept.push_back(
                {.instrument = instrument, .sound = bank->sound(instrument, sampleRate)});
        }
        const detail::TrackLevel level = detail::levelOf(song, t, loaded->headroom);
        loaded->setLevel(t, level);
        loaded->tracks.push_back({.sound = kept.back().sound,
                                  .events = std::move(events[t]),
                                  .buffer = std::vector<float>(maxBlockFrames, 0.0f),
                                  .applied = level});
    }
    if (clicks) {
        loaded->setLevel(tracks, detail::kMetronomeLevel);
        loaded->tracks.push_back(
            {.sound = builtinBank()->sound({.kind = core::InstrumentKind::Percussion}, sampleRate),
             .events = std::move(events[tracks]),
             .buffer = std::vector<float>(maxBlockFrames, 0.0f),
             .applied = detail::kMetronomeLevel});
    }
    return {.loaded = std::move(loaded),
            .kept = std::move(kept),
            .duration = sequence.duration(),
            .sampleRate = sampleRate};
}

} // namespace

void Player::load(const core::Song& song, const Sequence& sequence) {
    publish(PreparedSong(std::make_unique<PreparedSong::Parts>(
        prepareSong(kept_, bank_, sampleRate_, maxBlockFrames_, song, sequence))));
}

std::function<PreparedSong(const core::Song&, const Sequence&)> Player::preparer() const {
    // Copies of what the player holds now: the sounds are shared, never
    // changed by the worker.
    return [kept = kept_, bank = bank_, sampleRate = sampleRate_,
            maxBlockFrames = maxBlockFrames_](const core::Song& song, const Sequence& sequence) {
        return PreparedSong(std::make_unique<PreparedSong::Parts>(
            prepareSong(kept, bank, sampleRate, maxBlockFrames, song, sequence)));
    };
}

bool Player::publish(PreparedSong song) {
    if (!song.parts_ || song.parts_->sampleRate != sampleRate_) {
        return false;
    }
    PreparedSong::Parts& parts = *song.parts_;
    kept_ = std::move(parts.kept);
    duration_ = parts.duration;
    tidy();
    latest_ = parts.loaded.get();
    // A song the audio thread has not taken yet is replaced. The release
    // publishes the levels stored in the song with it.
    delete pending_.exchange(parts.loaded.release(), std::memory_order_acq_rel);
    return true;
}

void Player::updateMix(const core::Song& song) noexcept {
    if (latest_ == nullptr || song.trackCount() != latest_->songTracks) {
        return;
    }
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        latest_->setLevel(t, detail::levelOf(song, t, latest_->headroom));
    }
}

void Player::updateMix(std::span<const core::TrackMix> mixes) noexcept {
    if (latest_ == nullptr || mixes.size() != latest_->songTracks) {
        return;
    }
    const bool anySolo =
        std::ranges::any_of(mixes, [](const core::TrackMix& mix) { return mix.solo; });
    for (std::size_t t = 0; t < mixes.size(); ++t) {
        latest_->setLevel(t,
                          detail::levelOf(mixes[t], mixes[t].audible(anySolo), latest_->headroom));
    }
}

void Player::play() noexcept {
    start(0, 0);
}

void Player::play(const CountIn& countIn) noexcept {
    start(countIn.clicks, std::llround(std::max(countIn.interval, 0.0) * sampleRate_));
}

void Player::start(int clicks, std::int64_t interval) noexcept {
    if (isPlaying()) {
        return;
    }
    if (position() >= duration_) {
        seek(0.0);
    }
    const bool counting = clicks > 0 && interval > 0;
    countInClicks_.store(counting ? clicks : 0, std::memory_order_relaxed);
    countInInterval_.store(interval, std::memory_order_relaxed);
    countingIn_.store(counting, std::memory_order_relaxed);
    playing_.store(true, std::memory_order_release); // publishes the count-in
}

void Player::pause() noexcept {
    playing_.store(false, std::memory_order_release);
}

bool Player::isCountingIn() const noexcept {
    return countingIn_.load(std::memory_order_acquire);
}

void Player::setLoop(double from, double to) noexcept {
    const auto frame = [&](double seconds) {
        constexpr auto kMax = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        return static_cast<std::uint64_t>(
            std::min(std::llround(std::clamp(seconds, 0.0, duration_) * sampleRate_),
                     static_cast<long long>(kMax)));
    };
    const std::uint64_t a = frame(from);
    const std::uint64_t b = frame(to);
    loop_.store(b > a ? (a << 32U) | b : 0, std::memory_order_release);
}

void Player::seek(double seconds) noexcept {
    const double clamped = std::clamp(seconds, 0.0, duration_);
    seekTo_.store(std::llround(clamped * sampleRate_), std::memory_order_release);
}

void Player::takeOver(const Player& other) noexcept {
    seek(other.position());
    // The loop in seconds, from the frames at the other player's rate.
    const std::uint64_t loop = other.loop_.load(std::memory_order_acquire);
    const auto seconds = [&](std::uint64_t frame) {
        return static_cast<double>(frame) / other.sampleRate_;
    };
    setLoop(seconds(loop >> 32U), seconds(loop & 0xFFFF'FFFFU));
    if (other.isPlaying()) {
        play();
    } else {
        pause();
    }
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
            // Kept sounds still hold the notes of the song let go of.
            for (Loaded::Track& track : current_->tracks) {
                track.sound->reset();
            }
            current_->seat(frame_);
        }
    }
    if (const std::int64_t asked = seekTo_.load(std::memory_order_acquire); asked >= 0) {
        frame_ = asked;
        // The new position is published before the seek counts as taken, so
        // that position() never reads the one from before it. A seek asked
        // for meanwhile stays for the next callback.
        position_.store(asked, std::memory_order_release);
        std::int64_t taken = asked;
        seekTo_.compare_exchange_strong(taken, -1, std::memory_order_acq_rel);
        if (current_ != nullptr) {
            for (Loaded::Track& track : current_->tracks) {
                track.sound->reset();
            }
            current_->seat(frame_);
        }
    }
    const bool playing = playing_.load(std::memory_order_acquire);
    if (current_ != nullptr && wasPlaying_ && !playing) {
        for (Loaded::Track& track : current_->tracks) {
            track.sound->releaseAll();
        }
    }
    if (playing && !wasPlaying_) {
        // A start: the count-in asked for, if any.
        clicks_ = countInClicks_.load(std::memory_order_relaxed);
        interval_ = countInInterval_.load(std::memory_order_relaxed);
        countedIn_ = 0;
        // Nothing to count in to without a song.
        countInEnd_ = current_ != nullptr ? static_cast<std::int64_t>(clicks_) * interval_ : 0;
    } else if (!playing) {
        countInEnd_ = countedIn_; // a pause ends it
    }
    wasPlaying_ = playing;

    const std::size_t frames = std::min(left.size(), right.size());
    for (std::size_t done = 0; done < frames;) {
        const std::size_t n = std::min(frames - done, maxBlockFrames_);
        renderBlock(left.subspan(done, n), right.subspan(done, n));
        done += n;
    }
    countingIn_.store(countedIn_ < countInEnd_, std::memory_order_release);
    position_.store(frame_, std::memory_order_release);
}

namespace {

// Renders `out` of the count-in synth, `from` frames into the count-in,
// striking click k at frame k x `interval`, the first one higher.
void renderClicks(Synth& synth, std::int64_t from, std::span<float> out, std::int64_t interval,
                  int clicks) noexcept {
    const std::int64_t end = from + static_cast<std::int64_t>(out.size());
    std::size_t done = 0;
    for (std::int64_t k = (from + interval - 1) / interval; k < clicks && k * interval < end; ++k) {
        const auto at = static_cast<std::size_t>((k * interval) - from);
        synth.render(out.subspan(done, at - done));
        done = at;
        synth.noteOn(k == 0 ? kMetronomeAccentKey : kMetronomeKey, k == 0 ? 100 : 80);
    }
    synth.render(out.subspan(done));
}

} // namespace

void Player::renderBlock(std::span<float> left, std::span<float> right) noexcept {
    if (current_ == nullptr) {
        std::ranges::fill(left, 0.0f);
        std::ranges::fill(right, 0.0f);
        return;
    }
    const std::size_t n = left.size();
    const std::uint64_t loop = loop_.load(std::memory_order_acquire);
    const auto loopFrom = static_cast<std::int64_t>(loop >> 32U);
    const auto loopTo = static_cast<std::int64_t>(loop & 0xFFFF'FFFFU);
    for (Loaded::Track& track : current_->tracks) {
        std::fill_n(track.buffer.begin(), n, 0.0f);
    }
    std::fill_n(current_->countInBuffer.begin(), n, 0.0f);
    const auto slice = [](std::vector<float>& buffer, std::size_t at, std::size_t count) {
        return std::span<float>(buffer).subspan(at, count);
    };
    const auto ring = [&](std::size_t at, std::size_t count) {
        for (Loaded::Track& track : current_->tracks) {
            track.sound->render(slice(track.buffer, at, count)); // released notes ringing out
        }
    };

    // The block in pieces: the count-in, the song up to the end of the loop,
    // the song again from its start...
    for (std::size_t done = 0; done < n;) {
        const std::size_t rest = n - done;
        if (!wasPlaying_) {
            ring(done, rest);
            current_->countIn.render(slice(current_->countInBuffer, done, rest));
            break;
        }
        if (countedIn_ < countInEnd_) {
            const auto count = static_cast<std::size_t>(
                std::min<std::int64_t>(static_cast<std::int64_t>(rest), countInEnd_ - countedIn_));
            ring(done, count);
            renderClicks(current_->countIn, countedIn_, slice(current_->countInBuffer, done, count),
                         interval_, clicks_);
            countedIn_ += static_cast<std::int64_t>(count);
            done += count;
            continue;
        }
        const bool looping = loopTo > loopFrom && frame_ < loopTo;
        const auto count = static_cast<std::size_t>(
            looping ? std::min<std::int64_t>(static_cast<std::int64_t>(rest), loopTo - frame_)
                    : static_cast<std::int64_t>(rest));
        for (Loaded::Track& track : current_->tracks) {
            detail::playTrack(*track.sound, track.events, track.next, frame_,
                              slice(track.buffer, done, count),
                              [](const detail::FrameEvent&) noexcept {});
        }
        current_->countIn.render(slice(current_->countInBuffer, done, count));
        frame_ += static_cast<std::int64_t>(count);
        done += count;
        if (looping && frame_ >= loopTo) {
            frame_ = loopFrom;
            for (Loaded::Track& track : current_->tracks) {
                track.sound->reset();
            }
            current_->seat(frame_);
        } else if (frame_ >= current_->endFrame) {
            // The end: stop and let the last notes ring out.
            frame_ = current_->endFrame;
            playing_.store(false, std::memory_order_release);
            wasPlaying_ = false;
            for (Loaded::Track& track : current_->tracks) {
                track.sound->releaseAll();
            }
        }
    }

    const std::span<float> mixLeft(current_->left.data(), n);
    const std::span<float> mixRight(current_->right.data(), n);
    std::ranges::fill(mixLeft, 0.0f);
    std::ranges::fill(mixRight, 0.0f);
    for (std::size_t t = 0; t < current_->tracks.size(); ++t) {
        Loaded::Track& track = current_->tracks[t];
        const detail::TrackLevel target = current_->level(t);
        detail::mixInto(slice(track.buffer, 0, n), track.applied, target, mixLeft, mixRight);
        track.applied = target;
    }
    detail::mixInto(slice(current_->countInBuffer, 0, n), detail::kMetronomeLevel,
                    detail::kMetronomeLevel, mixLeft, mixRight);
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = detail::limit(mixLeft[i]);
        right[i] = detail::limit(mixRight[i]);
    }
}

} // namespace elara::audio
