// SPDX-License-Identifier: AGPL-3.0-or-later
// Micro-benchmarks for elara_audio: the sequencer, the built-in synthesizer and
// offline rendering. Run optimised builds only:
//   python scripts/elara.py bench -- "[audio]"
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

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

TEST_CASE("synthesizer benchmarks", "[benchmark][audio]") {
    using elara::audio::Synth;
    using elara::audio::Timbre;
    constexpr double kRate = 48000.0;
    // One second at 48 kHz in 512-frame buffers, all 32 voices sounding.
    const auto second = [&](Timbre timbre, int lowestKey) {
        Synth synth(timbre, kRate);
        for (int v = 0; v < 32; ++v) {
            synth.noteOn(lowestKey + v, 100);
        }
        std::vector<float> buffer(512);
        for (int block = 0; block < 94; ++block) {
            synth.render(buffer);
        }
        return buffer[0];
    };
    BENCHMARK("1 s of 32 plucked strings (guitar)") {
        return second(Timbre::Guitar, 40);
    };
    BENCHMARK("1 s of 32 struck partials (piano)") {
        return second(Timbre::Piano, 40);
    };
    BENCHMARK("1 s of 32 sawtooth voices (bowed)") {
        return second(Timbre::Bowed, 40);
    };
    BENCHMARK("1 s of 32 drum hits") {
        return second(Timbre::Drums, 35);
    };
}

TEST_CASE("offline rendering benchmarks", "[benchmark][audio]") {
    const elara::core::Song song = elara::bench::largeSong();
    const elara::audio::Sequence sequence(song);
    // The first 10 s: 20 guitar tracks, each striking two-note chords.
    const auto tenSeconds = [&](unsigned threads) {
        elara::audio::OfflineRenderer renderer(song, sequence,
                                               {.sampleRate = 48000.0, .threads = threads});
        std::vector<float> block(2 * 48000);
        for (int s = 0; s < 10; ++s) {
            static_cast<void>(renderer.render(block));
        }
        return block[0];
    };
    BENCHMARK("render 10 s of the large song (20 tracks) on every core") {
        return tenSeconds(0);
    };
    BENCHMARK("render 10 s of the large song on one thread") {
        return tenSeconds(1);
    };
}
