// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/tone_generator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

int countRisingZeroCrossings(const std::vector<float>& samples) {
    int crossings = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
            ++crossings;
        }
    }
    return crossings;
}

} // namespace

TEST_CASE("ToneGenerator produces the requested frequency", "[audio][tone]") {
    elara::audio::ToneGenerator generator(440.0, 48000.0, 0.5f);
    std::vector<float> samples(48000);
    generator.render(samples);

    // One rising zero crossing per period: 440 in one second.
    REQUIRE(countRisingZeroCrossings(samples) >= 439);
    REQUIRE(countRisingZeroCrossings(samples) <= 440);
}

TEST_CASE("ToneGenerator respects the amplitude", "[audio][tone]") {
    elara::audio::ToneGenerator generator(1000.0, 48000.0, 0.25f);
    std::vector<float> samples(4800);
    generator.render(samples);

    REQUIRE_THAT(static_cast<double>(elara::audio::peakLevel(samples)), WithinAbs(0.25, 1e-3));
}

TEST_CASE("fadeGain ramps in and out and is silent outside the sound", "[audio][fade]") {
    using elara::audio::fadeGain;
    constexpr std::int64_t kTotal = 1000;
    constexpr std::int64_t kFade = 100;

    REQUIRE(fadeGain(-1, kTotal, kFade) == 0.0f);
    REQUIRE(fadeGain(0, kTotal, kFade) == 0.0f);
    REQUIRE_THAT(static_cast<double>(fadeGain(50, kTotal, kFade)), WithinAbs(0.5, 1e-6));
    REQUIRE(fadeGain(500, kTotal, kFade) == 1.0f);
    REQUIRE(fadeGain(kTotal - 1, kTotal, kFade) == 0.0f);
    REQUIRE(fadeGain(kTotal, kTotal, kFade) == 0.0f);
    REQUIRE(fadeGain(10, kTotal, 0) == 1.0f);
}

TEST_CASE("renderTone has the right length, level and silent edges", "[audio][tone]") {
    const auto samples = elara::audio::renderTone(440.0, 48000.0, 0.5, 0.2f);

    REQUIRE(samples.size() == 24000);
    REQUIRE(samples.front() == 0.0f);
    REQUIRE(samples.back() == 0.0f);
    REQUIRE_THAT(static_cast<double>(elara::audio::peakLevel(samples)), WithinAbs(0.2, 1e-3));
}
