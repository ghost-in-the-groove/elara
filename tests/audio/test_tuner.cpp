// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/sample_ring.hpp>
#include <elara/audio/tuner.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::SampleRing;
using elara::audio::Tuner;
using elara::audio::TunerOptions;
using elara::core::Pitch;
using elara::core::Tuning;

namespace {

constexpr double kRate = 48000.0;

// Feeds `seconds` of a sine to the tuner in 512-sample blocks, as an audio
// input would, continuing the phase across calls.
class SineFeeder {
public:
    explicit SineFeeder(Tuner& tuner) : tuner_(tuner) {}

    // Returns the seconds it took for the reading to name `until`, if it did.
    std::optional<double> play(double frequency, double seconds,
                               std::optional<Pitch> until = std::nullopt) {
        std::vector<float> block(512);
        const auto blocks = static_cast<int>(seconds * kRate / 512.0);
        for (int b = 0; b < blocks; ++b) {
            for (float& sample : block) {
                sample = static_cast<float>(amplitude_ * std::sin(phase_));
                phase_ += 2.0 * std::numbers::pi * frequency / kRate;
            }
            tuner_.push(block);
            if (until && tuner_.reading() && tuner_.reading()->note == *until) {
                return (b + 1) * 512.0 / kRate;
            }
        }
        return std::nullopt;
    }

    double amplitude_ = 0.3;

private:
    Tuner& tuner_;
    double phase_ = 0.4;
};

Pitch pitch(const char* text) {
    return Pitch::parse(text).value();
}

} // namespace

TEST_CASE("A sample ring passes samples from one thread to another in order", "[audio][tuner]") {
    SampleRing ring(1000);
    CHECK(ring.capacity() == 1024);
    CHECK(ring.available() == 0);

    SECTION("it keeps what fits and counts what it drops") {
        std::vector<float> many(1500, 1.0f);
        CHECK(ring.write(many) == 1024);
        CHECK(ring.dropped() == 476);
        CHECK(ring.available() == 1024);
        std::vector<float> out(600);
        CHECK(ring.read(out) == 600);
        CHECK(ring.available() == 424);
        CHECK(ring.write(many) == 600);
        ring.clear();
        CHECK(ring.available() == 0);
        CHECK(ring.read(out) == 0);
    }
    SECTION("across the end of its buffer") {
        std::vector<float> in(700);
        std::vector<float> out(700);
        for (int round = 0; round < 5; ++round) {
            for (std::size_t i = 0; i < in.size(); ++i) {
                in[i] = static_cast<float>((round * 1000) + static_cast<int>(i));
            }
            REQUIRE(ring.write(in) == in.size());
            REQUIRE(ring.read(out) == out.size());
            CHECK(out == in);
        }
    }
    SECTION("between two threads") {
        constexpr int kCount = 200000;
        std::thread producer([&ring] {
            std::vector<float> block(37);
            int next = 0;
            while (next < kCount) {
                std::size_t n = 0;
                for (; n < block.size() && next + static_cast<int>(n) < kCount; ++n) {
                    block[n] = static_cast<float>(next + static_cast<int>(n));
                }
                const std::size_t written = ring.write(std::span(block).first(n));
                next += static_cast<int>(written);
                if (written < n) {
                    std::this_thread::yield();
                }
            }
        });
        std::vector<float> out(53);
        int expected = 0;
        bool inOrder = true;
        while (expected < kCount) {
            const std::size_t got = ring.read(out);
            for (std::size_t i = 0; i < got; ++i) {
                inOrder = inOrder && out[i] == static_cast<float>(expected);
                ++expected;
            }
            if (got == 0) {
                std::this_thread::yield();
            }
        }
        producer.join();
        CHECK(inOrder);
    }
}

TEST_CASE("The tuner reads a steady note from an input stream", "[audio][tuner]") {
    Tuner tuner(kRate);
    SineFeeder feeder(tuner);
    CHECK_FALSE(feeder.play(110.0, 3000.0 / kRate)); // less than a window
    CHECK_FALSE(tuner.reading());
    const auto heard = feeder.play(110.0, 0.5, pitch("A2"));
    REQUIRE(heard);
    CHECK(*heard < 0.1);
    feeder.play(110.0, 0.5);
    REQUIRE(tuner.reading());
    CHECK(tuner.reading()->note == pitch("A2"));
    CHECK_THAT(tuner.reading()->cents, WithinAbs(0.0, 0.5));
    CHECK(tuner.reading()->string == 0);

    SECTION("a new note replaces it quickly") {
        const auto changed = feeder.play(146.832, 0.5, pitch("D3"));
        REQUIRE(changed);
        CHECK(*changed < 0.2);
        feeder.play(146.832, 0.3);
        CHECK_THAT(tuner.reading()->cents, WithinAbs(0.0, 0.5));
    }
    SECTION("it is held a moment after the note stops, then cleared") {
        feeder.amplitude_ = 0.0;
        feeder.play(110.0, 0.3);
        REQUIRE(tuner.reading());
        CHECK(tuner.reading()->note == pitch("A2"));
        feeder.play(110.0, 0.4);
        CHECK_FALSE(tuner.reading());
    }
    SECTION("with a tuning, the nearest string") {
        tuner.setOptions({.a4 = 440.0, .tuning = Tuning::standardGuitar()});
        feeder.play(110.0, 0.2);
        REQUIRE(tuner.reading());
        CHECK(tuner.reading()->string == 5);
    }
}

TEST_CASE("The tuner smooths what it hears", "[audio][tuner]") {
    Tuner tuner(kRate);
    const std::size_t hop = tuner.frameSize() / 2;

    SECTION("the median of the last estimates") {
        CHECK(tuner.observe(110.0, hop));
        CHECK(tuner.observe(112.0, hop));
        CHECK(tuner.observe(110.2, hop));
        REQUIRE(tuner.reading());
        CHECK_THAT(tuner.reading()->frequency, WithinAbs(110.2, 1e-9));
        CHECK_THAT(tuner.reading()->cents, WithinAbs(3.1447, 0.001));
    }
    SECTION("a single estimate of another note is ignored") {
        tuner.observe(110.0, hop);
        tuner.observe(110.0, hop);
        CHECK_FALSE(tuner.observe(220.0, hop)); // an octave slip for one window
        CHECK(tuner.reading()->note == pitch("A2"));
        tuner.observe(110.0, hop);
        CHECK_FALSE(tuner.observe(146.8, hop));
        CHECK(tuner.reading()->note == pitch("A2"));
    }
    SECTION("two in a row change the note") {
        tuner.observe(110.0, hop);
        CHECK_FALSE(tuner.observe(146.8, hop));
        CHECK(tuner.observe(146.9, hop));
        CHECK(tuner.reading()->note == pitch("D3"));
        // The median of the two D3 estimates: A2 is forgotten.
        CHECK_THAT(tuner.reading()->frequency, WithinAbs(146.85, 1e-9));
    }
    SECTION("silence clears it after the hold time") {
        tuner.observe(110.0, hop);
        CHECK_FALSE(tuner.observe(std::nullopt, static_cast<std::size_t>(0.45 * kRate)));
        CHECK(tuner.reading());
        CHECK(tuner.observe(std::nullopt, static_cast<std::size_t>(0.1 * kRate)));
        CHECK_FALSE(tuner.reading());
        CHECK(tuner.observe(146.8, hop)); // the first note after silence is shown at once
        CHECK(tuner.reading()->note == pitch("D3"));
    }
    SECTION("a reference other than 440 Hz") {
        tuner.setOptions({.a4 = 442.0});
        tuner.observe(442.0, hop);
        CHECK(tuner.reading()->note == pitch("A4"));
        CHECK_THAT(tuner.reading()->cents, WithinAbs(0.0, 1e-9));
    }
}
