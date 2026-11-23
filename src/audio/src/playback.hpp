// SPDX-License-Identifier: AGPL-3.0-or-later
// What offline rendering and real-time playback share (ADR-0018, ADR-0019):
// note events in sample frames, dispatching them into a synth, and the mix.
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/audio/synth.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

/// The level of every track until the mixer exists: 1/sqrt(n) for n tracks
/// with notes.
[[nodiscard]] inline float trackGain(const std::vector<std::vector<FrameEvent>>& events) {
    const auto sounding = std::ranges::count_if(
        events, [](const std::vector<FrameEvent>& track) { return !track.empty(); });
    return 1.0f / std::sqrt(static_cast<float>(std::max<std::ptrdiff_t>(sounding, 1)));
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
