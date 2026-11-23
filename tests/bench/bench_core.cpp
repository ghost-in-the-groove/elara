// SPDX-License-Identifier: AGPL-3.0-or-later
// Micro-benchmarks for elara_core hot paths. Run optimised builds only:
//   python scripts/elara.py bench
#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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

TEST_CASE("core benchmarks", "[!benchmark][core]") {
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
