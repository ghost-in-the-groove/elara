// SPDX-License-Identifier: AGPL-3.0-or-later
// What offline rendering and real-time playback share (ADR-0018, ADR-0019,
// ADR-0020): note events in sample frames, dispatching them into a synth, and
// the mix.
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace elara::audio::detail {

/// A note event of one track at a sample frame.
struct FrameEvent {
    std::int64_t frame = 0;
    EventKind kind = EventKind::NoteOn;
    int key = 60;
    int velocity = 80;
};

/// The sequence's events split by track, at frame round(t x rate).
[[nodiscard]] inline std::vector<std::vector<FrameEvent>>
eventsByTrack(const Sequence& sequence, std::size_t tracks, double rate) {
    std::vector<std::vector<FrameEvent>> result(tracks);
    for (const NoteEvent& event : sequence.events()) {
        if (event.track < tracks) {
            result[event.track].push_back({.frame = std::llround(event.time * rate),
                                           .kind = event.kind,
                                           .key = event.key,
                                           .velocity = event.velocity});
        }
    }
    return result;
}

/// Room for every track to play at once: 1/sqrt(n) for n tracks with notes.
/// It does not depend on the mix, so muting a track leaves the others as loud.
[[nodiscard]] inline float headroom(const std::vector<std::vector<FrameEvent>>& events) {
    const auto sounding = std::ranges::count_if(
        events, [](const std::vector<FrameEvent>& track) { return !track.empty(); });
    return 1.0f / std::sqrt(static_cast<float>(std::max<std::ptrdiff_t>(sounding, 1)));
}

/// How loud a track goes into each channel.
struct TrackLevel {
    float left = 1.0f;
    float right = 1.0f;

    friend bool operator==(const TrackLevel&, const TrackLevel&) = default;
};

/// The level of track `t` from its mix (ADR-0020), times `headroom`: the fader
/// as a gain of (volume / 0.8)^2, unity at the default position; a
/// constant-power pan, unity in the centre and sqrt(2) on the side it goes to;
/// silence when mute or solo keeps the track quiet.
[[nodiscard]] inline TrackLevel levelOf(const core::Song& song, std::size_t t,
                                        float headroom) noexcept {
    if (!song.audible(t)) {
        return {.left = 0.0f, .right = 0.0f};
    }
    const core::TrackMix& mix = song.track(t).mix();
    const double fader = mix.volume / core::TrackMix::kDefaultVolume;
    const double gain = fader * fader * static_cast<double>(headroom) * std::numbers::sqrt2;
    // The same expression for both sides, so the centre is exactly even; a
    // quarter turn's cosine is not exactly 0 in floating point.
    const auto side = [](double angle) {
        const double share = std::cos(angle);
        return share < 1e-9 ? 0.0 : share;
    };
    const double angle = (mix.pan + 1.0) * std::numbers::pi / 4.0;
    return {.left = static_cast<float>(gain * side(angle)),
            .right = static_cast<float>(gain * side((std::numbers::pi / 2.0) - angle))};
}

/// Adds `track` at its level to `left` and `right`. The level moves in a
/// straight line from `from` to `to` over the block, so a change never clicks.
inline void mixInto(std::span<const float> track, TrackLevel from, TrackLevel to,
                    std::span<float> left, std::span<float> right) noexcept {
    const std::size_t n = track.size();
    if (from == to) {
        if (to.left == 0.0f && to.right == 0.0f) {
            return;
        }
        for (std::size_t i = 0; i < n; ++i) {
            left[i] += track[i] * to.left;
            right[i] += track[i] * to.right;
        }
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(n);
        left[i] += track[i] * (from.left + ((to.left - from.left) * t));
        right[i] += track[i] * (from.right + ((to.right - from.right) * t));
    }
}

/// The first event at or after `frame`.
[[nodiscard]] inline std::size_t firstAt(std::span<const FrameEvent> events,
                                         std::int64_t frame) noexcept {
    return static_cast<std::size_t>(
        std::ranges::lower_bound(events, frame, {}, &FrameEvent::frame) - events.begin());
}

/// Renders frames [from, from + out.size()) of one track, adding them to
/// `out` and dispatching each event from `next` on at its frame. Calls
/// `dispatched(event)` for every event dispatched.
template <typename Dispatched>
void playTrack(Synth& synth, std::span<const FrameEvent> events, std::size_t& next,
               std::int64_t from, std::span<float> out, Dispatched&& dispatched) {
    const std::int64_t end = from + static_cast<std::int64_t>(out.size());
    std::size_t done = 0;
    for (; next < events.size() && events[next].frame < end; ++next) {
        const FrameEvent& event = events[next];
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
        dispatched(event);
    }
    synth.render(out.subspan(done));
}

/// Soft knee: linear up to 0.75, then bending smoothly toward full scale.
[[nodiscard]] inline float limit(float sample) noexcept {
    constexpr float kKnee = 0.75f;
    constexpr float kRoom = 1.0f - kKnee;
    const float magnitude = std::abs(sample);
    if (magnitude <= kKnee) {
        return sample;
    }
    return std::copysign(kKnee + kRoom * std::tanh((magnitude - kKnee) / kRoom), sample);
}

} // namespace elara::audio::detail
