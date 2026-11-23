// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmarks for elara_io: saving and loading the native format.
#include <elara/io/native_format.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

TEST_CASE("native format benchmarks", "[benchmark][io]") {
    const elara::core::Song song = elara::bench::largeSong();
    const std::string json = elara::io::songToJson(song);
    const std::vector<std::uint8_t> file = elara::io::encodeNative(song);
    std::printf("large song: song.json %zu bytes, .elara file %zu bytes\n", json.size(),
                file.size());

    BENCHMARK("write song.json of the large song") {
        return elara::io::songToJson(song).size();
    };
    BENCHMARK("read song.json of the large song") {
        return elara::io::songFromJson(json)->measureCount();
    };
    BENCHMARK("encode the large song as a .elara file (JSON + deflate)") {
        return elara::io::encodeNative(song).size();
    };
    BENCHMARK("decode the large song from a .elara file") {
        return elara::io::decodeNative(file)->measureCount();
    };
}
