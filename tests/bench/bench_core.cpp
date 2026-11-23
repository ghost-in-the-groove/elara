// SPDX-License-Identifier: AGPL-3.0-or-later
// Micro-benchmarks for elara_core hot paths. Run optimised builds only:
//   python scripts/elara.py bench
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <vector>

using elara::core::Duration;
using elara::core::Fraction;
using elara::core::NoteValue;
using elara::core::Tuplet;

namespace {

// A mix of written durations as they appear in real guitar parts.
std::vector<Duration> sampleDurations(std::size_t count) {
    const std::array<Duration, 6> palette{
        Duration(NoteValue::Eighth),
        Duration(NoteValue::Sixteenth),
        Duration(NoteValue::Quarter, 1),
        Duration(NoteValue::Eighth, 0, Tuplet(3, 2)),
        Duration(NoteValue::Sixteenth, 0, Tuplet(5, 4)),
        Duration(NoteValue::ThirtySecond),
    };
    std::vector<Duration> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(palette[i % palette.size()]);
    }
    return out;
}

} // namespace

TEST_CASE("core benchmarks", "[benchmark][core]") {
    const auto durations = sampleDurations(10'000);

    BENCHMARK("sum 10k mixed durations (one long voice)") {
        Fraction position;
        for (const auto& d : durations) {
            position += d.length();
        }
        return position;
    };

    std::vector<Fraction> lengths;
    lengths.reserve(durations.size());
    for (const auto& d : durations) {
        lengths.push_back(d.length());
    }

    BENCHMARK("sort 10k fractions") {
        auto copy = lengths;
        std::sort(copy.begin(), copy.end());
        return copy.front();
    };
}

namespace {

// A large score: 20 guitar tracks x 500 measures, eight eighth notes per measure,
// a two-note chord on every beat (80 000 beats, 160 000 notes).
elara::core::Song largeSong() {
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

} // namespace

TEST_CASE("song benchmarks", "[benchmark][core][song]") {
    BENCHMARK("build a 20-track x 500-measure song") {
        return largeSong().measureCount();
    };

    const elara::core::Song song = largeSong();

    BENCHMARK("copy the large song (snapshot)") {
        elara::core::Song copy = song;
        return copy.trackCount();
    };

    BENCHMARK_ADVANCED("insert a measure at the start of the large song")
    (Catch::Benchmark::Chronometer meter) {
        std::vector<elara::core::Song> songs(static_cast<std::size_t>(meter.runs()), song);
        meter.measure([&](int i) {
            songs[static_cast<std::size_t>(i)].insertMeasures(0);
            return songs[static_cast<std::size_t>(i)].measureCount();
        });
    };

    BENCHMARK("seconds at the end of the large song (tempo map walk)") {
        return song.durationSeconds();
    };

    BENCHMARK("sum every voice length of the large song") {
        elara::core::Fraction total;
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            const auto& staff = song.track(t).staff(0);
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                total += staff.measure(m).voices[0].length();
            }
        }
        return total;
    };

    BENCHMARK("validate the large song (fills, frets, ties)") {
        return elara::core::validate(song).size();
    };
}

TEST_CASE("edit benchmarks", "[benchmark][core][document]") {
    using namespace elara::core;
    Document doc(largeSong());
    const Measure edited = doc.song().track(7).staff(0).measure(250);

    // Each run applies an edit and undoes it, so the document is the same after
    // every run and the history stays empty.
    BENCHMARK("edit one measure and undo (large song)") {
        doc.execute(std::make_unique<ReplaceMeasure>(7, 0, 250, edited));
        doc.undo();
        return doc.undoCount();
    };

    BENCHMARK("remove 100 measures from every track and undo (large song)") {
        doc.execute(std::make_unique<RemoveMeasures>(200, 100));
        doc.undo();
        return doc.song().measureCount();
    };

    BENCHMARK("remove a track and undo (large song)") {
        doc.execute(std::make_unique<RemoveTrack>(0));
        doc.undo();
        return doc.song().trackCount();
    };
}

// Not a timing: prints the size of the model's value types so memory changes show
// up next to the benchmarks (python scripts/elara.py bench -- "[memory]").
TEST_CASE("model memory footprint", "[benchmark][core][memory]") {
    using namespace elara::core;
    std::printf("sizeof: Note %zu, NoteEffects %zu, BendCurve %zu, Harmonic %zu, Beat %zu, "
                "BeatEffects %zu, Voice %zu, Measure %zu\n",
                sizeof(Note), sizeof(NoteEffects), sizeof(BendCurve), sizeof(Harmonic),
                sizeof(Beat), sizeof(BeatEffects), sizeof(Voice), sizeof(Measure));
    SUCCEED();
}
