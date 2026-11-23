// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/pitch_detector.hpp>
#include <elara/audio/synth.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::PitchDetector;
using elara::audio::PitchEstimate;
using elara::audio::Synth;
using elara::audio::Timbre;
using elara::audio::tunerReading;
using elara::core::Pitch;
using elara::core::Tuning;

namespace {

double keyFrequency(int key) {
    return 440.0 * std::exp2((key - 69) / 12.0);
}

double centsBetween(double frequency, double reference) {
    return 1200.0 * std::log2(frequency / reference);
}

// `seconds` of a sum of harmonics with these amplitudes (the first is the
// fundamental), starting at a phase that is not a zero crossing.
std::vector<float> tone(double frequency, double rate, double seconds,
                        const std::vector<double>& harmonics = {1.0}) {
    std::vector<float> out(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        double value = 0.0;
        for (std::size_t h = 0; h < harmonics.size(); ++h) {
            value += harmonics[h] *
                     std::sin((2.0 * std::numbers::pi * frequency * static_cast<double>(h + 1) *
                               static_cast<double>(i) / rate) +
                              0.3);
        }
        out[i] = static_cast<float>(0.4 * value);
    }
    return out;
}

// A note of the built-in synthesizer, from 0.1 s after its attack.
std::vector<float> plucked(Timbre timbre, int key, double rate) {
    Synth synth(timbre, rate);
    synth.noteOn(key, 100);
    std::vector<float> out(static_cast<std::size_t>(0.4 * rate));
    synth.render(out);
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(0.1 * rate));
    return out;
}

} // namespace

TEST_CASE("The pitch detector finds sine waves within a cent", "[audio][tuner]") {
    const double rate = GENERATE(44100.0, 48000.0);
    const double frequency = GENERATE(30.87, 41.2034, 55.0, 82.4069, 110.0, 146.832, 196.0, 246.942,
                                      329.628, 440.0, 659.255, 987.767, 1318.51);
    PitchDetector detector(rate);
    const std::optional<PitchEstimate> estimate = detector.detect(tone(frequency, rate, 0.2));
    REQUIRE(estimate);
    CHECK_THAT(centsBetween(estimate->frequency, frequency), WithinAbs(0.0, 1.0));
    CHECK(estimate->clarity > 0.95);
}

TEST_CASE("Harmonics do not fool the pitch detector", "[audio][tuner]") {
    PitchDetector detector(48000.0);
    SECTION("a sawtooth") {
        std::vector<double> harmonics;
        for (int h = 1; h <= 20; ++h) {
            harmonics.push_back(1.0 / h);
        }
        const auto estimate = detector.detect(tone(110.0, 48000.0, 0.2, harmonics));
        REQUIRE(estimate);
        CHECK_THAT(centsBetween(estimate->frequency, 110.0), WithinAbs(0.0, 1.0));
    }
    SECTION("a second harmonic louder than the fundamental") {
        const auto estimate = detector.detect(tone(98.0, 48000.0, 0.2, {0.5, 1.0, 0.3}));
        REQUIRE(estimate);
        CHECK_THAT(centsBetween(estimate->frequency, 98.0), WithinAbs(0.0, 1.0));
    }
}

TEST_CASE("The pitch detector hears the strings of the built-in synthesizer", "[audio][tuner]") {
    PitchDetector detector(48000.0);
    SECTION("a guitar's open strings") {
        const int key = GENERATE(40, 45, 50, 55, 59, 64);
        const auto estimate = detector.detect(plucked(Timbre::Guitar, key, 48000.0));
        REQUIRE(estimate);
        CHECK_THAT(centsBetween(estimate->frequency, keyFrequency(key)), WithinAbs(0.0, 5.0));
    }
    SECTION("a bass's low E") {
        const auto estimate = detector.detect(plucked(Timbre::Bass, 28, 48000.0));
        REQUIRE(estimate);
        CHECK_THAT(centsBetween(estimate->frequency, keyFrequency(28)), WithinAbs(0.0, 5.0));
    }
}

TEST_CASE("The pitch detector stays quiet without a clear pitch", "[audio][tuner]") {
    PitchDetector detector(48000.0);
    CHECK(detector.frameSize() == 3201); // two periods of 30 Hz, and one sample
    SECTION("silence") {
        CHECK_FALSE(detector.detect(std::vector<float>(detector.frameSize(), 0.0f)));
    }
    SECTION("noise") {
        std::vector<float> noise(detector.frameSize());
        std::uint32_t state = 12345;
        for (float& sample : noise) {
            state = (state * 1664525U) + 1013904223U;
            sample = (static_cast<float>(state >> 8U) / 8388608.0f) - 1.0f;
        }
        CHECK_FALSE(detector.detect(noise));
    }
    SECTION("a note too quiet to tell from the noise floor") {
        std::vector<float> faint = tone(110.0, 48000.0, 0.2);
        for (float& sample : faint) {
            sample *= 1e-4f; // -88 dBFS
        }
        CHECK_FALSE(detector.detect(faint));
        for (float& sample : faint) {
            sample *= 100.0f; // -48 dBFS
        }
        CHECK(detector.detect(faint));
    }
    SECTION("below its lowest frequency") {
        CHECK_FALSE(detector.detect(tone(20.0, 48000.0, 0.2)));
    }
    SECTION("too few samples") {
        CHECK_FALSE(detector.detect(tone(110.0, 48000.0, 0.05)));
    }
    SECTION("a range it cannot hear") {
        CHECK_THROWS_AS(PitchDetector(0.0), std::invalid_argument);
        CHECK_THROWS_AS(PitchDetector(48000.0, 500.0, 400.0), std::invalid_argument);
        CHECK_THROWS_AS(PitchDetector(8000.0, 30.0, 4000.0), std::invalid_argument);
    }
}

TEST_CASE("A tuner reading names the nearest note and string", "[audio][tuner]") {
    const auto a = tunerReading(440.0);
    REQUIRE(a);
    CHECK(a->note == Pitch::parse("A4").value());
    CHECK_THAT(a->cents, WithinAbs(0.0, 1e-9));
    CHECK(a->string == 0);

    const auto sharp = tunerReading(445.0);
    REQUIRE(sharp);
    CHECK(sharp->note == Pitch::parse("A4").value());
    CHECK_THAT(sharp->cents, WithinAbs(19.56, 0.01));

    const auto between = tunerReading(80.0); // just under halfway from D#2 to E2
    REQUIRE(between);
    CHECK(between->note == Pitch::parse("D#2").value());
    CHECK_THAT(between->cents, WithinAbs(48.68, 0.01));

    SECTION("with A4 at another frequency") {
        const auto baroque = tunerReading(415.0, 415.0);
        REQUIRE(baroque);
        CHECK(baroque->note == Pitch::parse("A4").value());
        CHECK_THAT(baroque->cents, WithinAbs(0.0, 1e-9));
    }
    SECTION("on a guitar, the string to tune") {
        const auto reading = tunerReading(112.0, 440.0, Tuning::standardGuitar());
        REQUIRE(reading);
        CHECK(reading->string == 5); // A2
        CHECK_THAT(reading->stringCents, WithinAbs(31.19, 0.01));
        const auto high = tunerReading(325.0, 440.0, Tuning::standardGuitar());
        REQUIRE(high);
        CHECK(high->string == 1);
        CHECK_THAT(high->stringCents, WithinAbs(-24.47, 0.01));
    }
    SECTION("no reading for what is not a frequency") {
        CHECK_FALSE(tunerReading(0.0));
        CHECK_FALSE(tunerReading(-5.0));
        CHECK_FALSE(tunerReading(5.0)); // below MIDI 0
        CHECK_FALSE(tunerReading(std::nan("")));
    }
}
