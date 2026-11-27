// SPDX-License-Identifier: AGPL-3.0-or-later
// Songs shared by the benchmarks.
#pragma once

#include <elara/core/song.hpp>

#include <cstddef>
#include <vector>

namespace elara::bench {

// A large score: 20 guitar tracks x 500 measures, eight eighth notes per measure,
// a two-note chord on every beat (80 000 beats, 160 000 notes).
inline elara::core::Song largeSong() {
    using namespace elara::core;
    Song song;
    const auto* preset = findInstrumentPreset("electric-guitar");
    for (int t = 0; t < 20; ++t) {
        song.addTrack(Track::fromPreset(*preset));
    }
    song.appendMeasures(500);
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        Staff& staff = song.track(t).staff(0);
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            auto& beats = staff.measure(m).voices[0].beats;
            beats.reserve(8);
            for (int b = 0; b < 8; ++b) {
                beats.push_back(
                    Beat{.duration = Duration(NoteValue::Eighth),
                         .notes = {staff.frettedNote(6, b % 5), staff.frettedNote(5, b % 5)}});
            }
        }
    }
    return song;
}

// `song` with a system break before every measure, in the song's measure
// layouts and in each track's (ADR-0152): the most layout adjustments hold.
inline elara::core::Song withBreaksEverywhere(elara::core::Song song) {
    using namespace elara::core;
    const auto everywhere = [&] {
        return MeasureLayouts(std::vector<MeasureLayout>(
            song.measureCount(), MeasureLayout{.breakBefore = Break::System}));
    };
    song.setMeasureLayouts(LayoutScope::song(), everywhere());
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        song.setMeasureLayouts(LayoutScope::ofTrack(t), everywhere());
    }
    return song;
}

} // namespace elara::bench
