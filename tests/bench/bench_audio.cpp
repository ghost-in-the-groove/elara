// SPDX-License-Identifier: AGPL-3.0-or-later
// Micro-benchmarks for elara_audio: the sequencer. Run optimised builds only:
//   python scripts/elara.py bench -- "[audio]"
#include <elara/audio/sequence.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

TEST_CASE("sequencer benchmarks", "[benchmark][audio]") {
    elara::core::Song song = elara::bench::largeSong();
    BENCHMARK("sounding notes of the large song (what the sequencer starts from)") {
        return elara::core::soundingNotes(song).size();
    };
    BENCHMARK("sequence the large song (500 measures, 160k notes)") {
        return elara::audio::Sequence(song).events().size();
    };
    // Every four measures repeated: 1000 measures and 320k notes played.
    for (std::size_t m = 0; m < song.measureCount(); m += 4) {
        song.measureHeader(m).repeatStart = true;
        song.measureHeader(m + 3).repeatTimes = 2;
    }
    BENCHMARK("sequence the large song with every four measures repeated") {
        return elara::audio::Sequence(song).events().size();
    };
}
