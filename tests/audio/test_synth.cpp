// SPDX-License-Identifier: AGPL-3.0-or-later
// The built-in synthesizer (ADR-0018).
#include <elara/audio/synth.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/core/instrument.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

using elara::audio::peakLevel;
using elara::audio::Synth;
using elara::audio::Timbre;
using elara::audio::timbreFor;
using elara::audio::timbreName;
using elara::audio::Touch;
using elara::audio::touchName;
using elara::core::Instrument;
using elara::core::InstrumentKind;

namespace {

constexpr double kRate = 48000.0;

std::size_t frames(double seconds) {
    return static_cast<std::size_t>(std::lround(seconds * kRate));
}

std::vector<float> render(Synth& synth, double seconds) {
    std::vector<float> out(frames(seconds));
    synth.render(out);
    return out;
}

double keyFrequency(int key) {
    return 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

// The fundamental frequency of `x`, by the YIN difference function: the first
// lag whose cumulative-mean-normalised difference dips below 0.15, refined by
// a parabola through its neighbours.
double pitchOf(std::span<const float> x) {
    const auto maxLag = static_cast<std::size_t>(kRate / 30.0);
    const std::size_t window = x.size() - maxLag;
    std::vector<double> d(maxLag + 1, 0.0);
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < window; ++i) {
            const double diff = static_cast<double>(x[i]) - static_cast<double>(x[i + lag]);
            sum += diff * diff;
        }
        d[lag] = sum;
    }
    double running = 0.0;
    std::vector<double> normalised(maxLag + 1, 1.0);
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        running += d[lag];
        normalised[lag] = running > 0.0 ? d[lag] * static_cast<double>(lag) / running : 1.0;
    }
    for (std::size_t lag = static_cast<std::size_t>(kRate / 2000.0); lag < maxLag; ++lag) {
        if (normalised[lag] < 0.15) {
            while (lag + 1 < maxLag && normalised[lag + 1] < normalised[lag]) {
                ++lag;
            }
            const double a = normalised[lag - 1];
            const double b = normalised[lag];
            const double c = normalised[lag + 1];
            const double denominator = a - 2.0 * b + c;
            const double shift = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
            return kRate / (static_cast<double>(lag) + shift);
        }
    }
    return 0.0;
}

double rms(std::span<const float> x) {
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

// How much `x` changes from one frame to the next for its level: higher for
// brighter sounds.
double brightness(std::span<const float> x) {
    double change = 0.0;
    double level = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        change += std::abs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1]));
        level += std::abs(static_cast<double>(x[i]));
    }
    return level > 0.0 ? change / level : 0.0;
}

std::vector<float> strike(Timbre timbre, int key, Touch touch, double seconds = 1.5) {
    Synth synth(timbre, kRate);
    synth.noteOn(key, 100, touch);
    return render(synth, seconds);
}

std::span<const float> between(const std::vector<float>& x, double from, double to) {
    return std::span<const float>(x).subspan(frames(from), frames(to) - frames(from));
}

Instrument program(int midiProgram) {
    return Instrument{.kind = InstrumentKind::Pitched, .midiProgram = midiProgram};
}

} // namespace

TEST_CASE("Synth: the General MIDI program family picks the timbre", "[audio][synth]") {
    CHECK(timbreFor(program(0)) == Timbre::Piano);
    CHECK(timbreFor(program(4)) == Timbre::Piano);
    CHECK(timbreFor(program(11)) == Timbre::Mallet);
    CHECK(timbreFor(program(19)) == Timbre::Organ);
    CHECK(timbreFor(program(24)) == Timbre::Guitar);
    CHECK(timbreFor(program(27)) == Timbre::Guitar);
    CHECK(timbreFor(program(29)) == Timbre::Overdrive);
    CHECK(timbreFor(program(30)) == Timbre::Overdrive);
    CHECK(timbreFor(program(33)) == Timbre::Bass);
    CHECK(timbreFor(program(38)) == Timbre::Bass);
    CHECK(timbreFor(program(40)) == Timbre::Bowed);
    CHECK(timbreFor(program(45)) == Timbre::Guitar); // pizzicato strings
    CHECK(timbreFor(program(46)) == Timbre::Guitar); // harp
    CHECK(timbreFor(program(47)) == Timbre::Mallet); // timpani
    CHECK(timbreFor(program(48)) == Timbre::Bowed);
    CHECK(timbreFor(program(56)) == Timbre::Wind);
    CHECK(timbreFor(program(66)) == Timbre::Wind);
    CHECK(timbreFor(program(73)) == Timbre::Wind);
    CHECK(timbreFor(program(81)) == Timbre::Wind);
    CHECK(timbreFor(program(89)) == Timbre::Bowed);
    CHECK(timbreFor(program(105)) == Timbre::Guitar); // banjo
    CHECK(timbreFor(program(110)) == Timbre::Bowed);  // fiddle
    CHECK(timbreFor(program(114)) == Timbre::Mallet); // steel drums
    CHECK(timbreFor(Instrument{.kind = InstrumentKind::Percussion, .midiProgram = 0}) ==
          Timbre::Drums);
    CHECK(timbreName(Timbre::Overdrive) == "overdrive");
    CHECK(timbreName(Timbre::Drums) == "drums");
}

TEST_CASE("Synth: a synth is silent until a key is struck", "[audio][synth]") {
    Synth synth(Timbre::Guitar, kRate);
    const auto out = render(synth, 0.05);
    CHECK(peakLevel(out) == 0.0f);
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("Synth: every timbre sounds at the pitch of its key", "[audio][synth]") {
    const auto timbre = GENERATE(Timbre::Piano, Timbre::Mallet, Timbre::Organ, Timbre::Guitar,
                                 Timbre::Overdrive, Timbre::Bass, Timbre::Bowed, Timbre::Wind);
    const int key = timbre == Timbre::Bass ? 40 : 57; // E2 (82.4 Hz) or A3 (220 Hz)
    CAPTURE(timbreName(timbre));
    Synth synth(timbre, kRate);
    synth.noteOn(key, 100);
    const auto out = render(synth, 0.4);
    const std::span<const float> steady(out.data() + frames(0.15), frames(0.25));
    CHECK(peakLevel(steady) > 0.02f);
    const double pitch = pitchOf(steady);
    CHECK(std::abs(pitch / keyFrequency(key) - 1.0) < 0.004);
}

TEST_CASE("Synth: a released key fades to silence", "[audio][synth]") {
    const auto timbre =
        GENERATE(Timbre::Piano, Timbre::Organ, Timbre::Guitar, Timbre::Bowed, Timbre::Wind);
    CAPTURE(timbreName(timbre));
    Synth synth(timbre, kRate);
    synth.noteOn(60, 90);
    CHECK(peakLevel(render(synth, 0.2)) > 0.02f);
    synth.noteOff(60);
    const auto after = render(synth, 1.0);
    CHECK(peakLevel(std::span<const float>(after).last(frames(0.1))) == 0.0f);
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("Synth: a plucked string and a struck piano ring out by themselves", "[audio][synth]") {
    const auto timbre = GENERATE(Timbre::Guitar, Timbre::Piano, Timbre::Mallet);
    CAPTURE(timbreName(timbre));
    Synth synth(timbre, kRate);
    synth.noteOn(76, 100);
    CHECK(peakLevel(render(synth, 0.5)) > 0.02f);
    CHECK(synth.activeVoices() == 1);
    for (int second = 0; second < 15 && synth.activeVoices() > 0; ++second) {
        static_cast<void>(render(synth, 1.0));
    }
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("Synth: a steady timbre holds its note until it is released", "[audio][synth]") {
    Synth synth(Timbre::Organ, kRate);
    synth.noteOn(64, 90);
    const auto out = render(synth, 3.0);
    CHECK(peakLevel(std::span<const float>(out).last(frames(0.1))) > 0.05f);
    CHECK(synth.activeVoices() == 1);
}

TEST_CASE("Synth: louder velocities give louder notes", "[audio][synth]") {
    Synth soft(Timbre::Guitar, kRate);
    Synth loud(Timbre::Guitar, kRate);
    soft.noteOn(52, 30);
    loud.noteOn(52, 120);
    CHECK(peakLevel(render(loud, 0.2)) > 2.0f * peakLevel(render(soft, 0.2)));
}

TEST_CASE("Synth: a note takes a voice from others when every voice sounds", "[audio][synth]") {
    Synth synth(Timbre::Organ, kRate, 4);
    for (int key = 60; key < 66; ++key) {
        synth.noteOn(key, 90);
    }
    CHECK(synth.activeVoices() == 4);
    CHECK(peakLevel(render(synth, 0.1)) > 0.05f);
    synth.releaseAll();
    static_cast<void>(render(synth, 1.0));
    CHECK(synth.activeVoices() == 0);
}

TEST_CASE("Synth: drums ring out without a release; a closed hi-hat chokes an open one",
          "[audio][synth]") {
    Synth drums(Timbre::Drums, kRate);
    drums.noteOn(36, 110); // bass drum
    drums.noteOff(36);
    CHECK(peakLevel(render(drums, 0.05)) > 0.1f);
    CHECK(drums.activeVoices() == 1);
    static_cast<void>(render(drums, 2.0));
    CHECK(drums.activeVoices() == 0);

    drums.noteOn(46, 100); // open hi-hat
    static_cast<void>(render(drums, 0.05));
    CHECK(drums.activeVoices() == 1);
    drums.noteOn(42, 100); // closed hi-hat
    static_cast<void>(render(drums, 0.2));
    CHECK(drums.activeVoices() == 0);

    drums.noteOn(46, 100); // left alone, an open hi-hat still sounds after 0.25 s
    static_cast<void>(render(drums, 0.25));
    CHECK(drums.activeVoices() == 1);
}

TEST_CASE("Synth: every drum key sounds and ends", "[audio][synth]") {
    Synth drums(Timbre::Drums, kRate);
    for (int key = 27; key <= 87; ++key) {
        CAPTURE(key);
        drums.reset();
        drums.noteOn(key, 100);
        CHECK(peakLevel(render(drums, 0.1)) > 0.01f);
        static_cast<void>(render(drums, 4.0));
        CHECK(drums.activeVoices() == 0);
    }
}

TEST_CASE("Synth: reset silences every voice at once", "[audio][synth]") {
    Synth synth(Timbre::Bowed, kRate);
    synth.noteOn(60, 90);
    synth.noteOn(64, 90);
    static_cast<void>(render(synth, 0.2));
    synth.reset();
    CHECK(synth.activeVoices() == 0);
    CHECK(peakLevel(render(synth, 0.01)) == 0.0f);
}

TEST_CASE("Synth: the output does not depend on how frames are split into calls",
          "[audio][synth]") {
    const auto timbre =
        GENERATE(Timbre::Piano, Timbre::Guitar, Timbre::Overdrive, Timbre::Bowed, Timbre::Drums);
    CAPTURE(timbreName(timbre));
    Synth whole(timbre, kRate);
    Synth pieces(timbre, kRate);
    for (Synth* synth : {&whole, &pieces}) {
        synth->noteOn(45, 100);
        synth->noteOn(52, 80);
        synth->noteOn(38, 90);
    }
    std::vector<float> a(frames(1.5));
    std::vector<float> b(a.size());
    whole.render(a);
    std::size_t done = 0;
    for (std::size_t size = 1; done < b.size(); size = size * 3 + 1) {
        const std::size_t n = std::min(size, b.size() - done);
        pieces.render(std::span<float>(b).subspan(done, n));
        done += n;
    }
    CHECK(a == b);
}

TEST_CASE("Synth: rendering adds to what is already in the buffer", "[audio][synth]") {
    Synth synth(Timbre::Organ, kRate);
    synth.noteOn(69, 90);
    std::vector<float> out(256, 0.5f);
    Synth copy(Timbre::Organ, kRate);
    copy.noteOn(69, 90);
    std::vector<float> alone(256, 0.0f);
    synth.render(out);
    copy.render(alone);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i] == 0.5f + alone[i]);
    }
}

// Touches (ADR-0022): plucked strings play each one differently.
TEST_CASE("Synth: a palm-muted string dies away sooner than an open one", "[audio][synth][touch]") {
    const Timbre timbre = GENERATE(Timbre::Guitar, Timbre::Overdrive, Timbre::Bass);
    CAPTURE(timbreName(timbre));
    const int key = timbre == Timbre::Bass ? 40 : 52;
    const std::vector<float> open = strike(timbre, key, Touch::Normal);
    const std::vector<float> muted = strike(timbre, key, Touch::PalmMute);
    CHECK(rms(between(muted, 0.0, 0.02)) > 0.3 * rms(between(open, 0.0, 0.02)));
    CHECK(rms(between(muted, 0.5, 0.7)) < 0.1 * rms(between(open, 0.5, 0.7)));
}

TEST_CASE("Synth: a palm-muted string sounds darker", "[audio][synth][touch]") {
    const Timbre timbre = GENERATE(Timbre::Guitar, Timbre::Bass);
    CAPTURE(timbreName(timbre));
    const std::vector<float> open = strike(timbre, 52, Touch::Normal, 0.1);
    const std::vector<float> muted = strike(timbre, 52, Touch::PalmMute, 0.1);
    CHECK(brightness(between(muted, 0.0, 0.05)) < brightness(between(open, 0.0, 0.05)));
}

TEST_CASE("Synth: a dead note is a click that is over at once", "[audio][synth][touch]") {
    Synth dead(Timbre::Guitar, kRate);
    dead.noteOn(52, 100, Touch::Dead);
    Synth open(Timbre::Guitar, kRate);
    open.noteOn(52, 100);
    const std::vector<float> click = render(dead, 0.3);
    const std::vector<float> ringing = render(open, 0.3);
    CHECK(rms(between(click, 0.0, 0.01)) > 0.0);
    CHECK(rms(between(click, 0.15, 0.3)) < 0.01 * rms(between(ringing, 0.15, 0.3)));
    CHECK(dead.activeVoices() == 0);
    CHECK(open.activeVoices() == 1);
}

TEST_CASE("Synth: a harmonic rings pure and long at the pitch of its key",
          "[audio][synth][touch]") {
    const std::vector<float> open = strike(Timbre::Guitar, 64, Touch::Normal);
    const std::vector<float> harmonic = strike(Timbre::Guitar, 64, Touch::Harmonic);
    const double expected = keyFrequency(64);
    CHECK(std::abs(pitchOf(between(harmonic, 0.1, 0.3)) - expected) < 0.01 * expected);
    CHECK(brightness(between(harmonic, 0.0, 0.2)) < 0.5 * brightness(between(open, 0.0, 0.2)));
    const auto decay = [](const std::vector<float>& x) {
        return rms(between(x, 1.0, 1.2)) / rms(between(x, 0.1, 0.3));
    };
    CHECK(decay(harmonic) > decay(open));
}

TEST_CASE("Synth: timbres that are not plucked play every touch alike", "[audio][synth][touch]") {
    const Timbre timbre = GENERATE(Timbre::Piano, Timbre::Mallet, Timbre::Organ, Timbre::Bowed,
                                   Timbre::Wind, Timbre::Drums);
    const Touch touch = GENERATE(Touch::PalmMute, Touch::Dead, Touch::Harmonic);
    CAPTURE(timbreName(timbre), touchName(touch));
    const int key = timbre == Timbre::Drums ? 38 : 60;
    CHECK(strike(timbre, key, touch, 0.3) == strike(timbre, key, Touch::Normal, 0.3));
}

TEST_CASE("Synth: touches have the names the command line prints", "[audio][synth][touch]") {
    CHECK(touchName(Touch::Normal) == "normal");
    CHECK(touchName(Touch::PalmMute) == "palm-mute");
    CHECK(touchName(Touch::Dead) == "dead");
    CHECK(touchName(Touch::Harmonic) == "harmonic");
}
