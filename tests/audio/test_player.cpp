// SPDX-License-Identifier: AGPL-3.0-or-later
// Real-time playback with the built-in synthesizer (ADR-0019).
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <span>
#include <thread>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::OfflineRenderer;
using elara::audio::peakLevel;
using elara::audio::Player;
using elara::audio::Sequence;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;

namespace {

constexpr double kRate = 48000.0;
constexpr double kFrame = 1.0 / kRate;

Beat beat(NoteValue value, std::vector<int> keys = {}) {
    Beat result{.duration = Duration(value)};
    for (const int key : keys) {
        result.notes.push_back(Note{.pitch = Pitch::fromMidi(key)});
    }
    return result;
}

// An organ and a guitar over `measures` measures of 4/4 at quarter = 120 (two
// seconds each); every measure plays `first` on its first beat and rests
// otherwise, unless `everyBeat`.
Song song(std::size_t measures, int first, bool everyBeat = true) {
    Song result;
    result.addTrack(
        Track("Organ", Instrument{.kind = InstrumentKind::Keyboard, .midiProgram = 16}, {Staff{}}));
    result.addTrack(
        Track("Guitar", Instrument{.kind = InstrumentKind::Fretted, .midiProgram = 25}, {Staff{}}));
    result.appendMeasures(measures);
    for (std::size_t m = 0; m < measures; ++m) {
        auto& organ = result.track(0).staff(0).measure(m).voices[0].beats;
        auto& guitar = result.track(1).staff(0).measure(m).voices[0].beats;
        if (everyBeat) {
            organ = {beat(NoteValue::Quarter, {first}), beat(NoteValue::Quarter, {first + 4}),
                     beat(NoteValue::Quarter, {first + 7}), beat(NoteValue::Quarter, {first})};
        } else {
            organ = {beat(NoteValue::Quarter, {first}), beat(NoteValue::Quarter),
                     beat(NoteValue::Half)};
        }
        guitar = {beat(NoteValue::Half, {first - 12, first - 5}), beat(NoteValue::Half)};
    }
    return result;
}

// Renders `seconds` in blocks of `block` frames, as an audio callback would.
std::vector<float> play(Player& player, double seconds, std::size_t block = 480) {
    const auto frames = static_cast<std::size_t>(std::lround(seconds * kRate));
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (std::size_t done = 0; done < frames; done += block) {
        const std::size_t n = std::min(block, frames - done);
        player.render(std::span<float>(left).subspan(done, n),
                      std::span<float>(right).subspan(done, n));
    }
    REQUIRE(left == right);
    return left;
}

std::span<const float> between(const std::vector<float>& samples, double from, double to) {
    const auto first = static_cast<std::size_t>(std::lround(from * kRate));
    const auto last = static_cast<std::size_t>(std::lround(to * kRate));
    return std::span<const float>(samples).subspan(first, last - first);
}

} // namespace

TEST_CASE("Player: silent, and holding still, until it plays", "[audio][player]") {
    Player player(kRate);
    CHECK(peakLevel(play(player, 0.1)) == 0.0f); // nothing loaded
    const Song s = song(2, 60);
    player.load(s, Sequence(s));
    CHECK(peakLevel(play(player, 0.5)) == 0.0f);
    CHECK_FALSE(player.isPlaying());
    CHECK(player.position() == 0.0);
    CHECK_THAT(player.duration(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("Player: plays what the offline renderer renders", "[audio][player]") {
    const Song s = song(2, 60);
    const Sequence sequence(s);
    OfflineRenderer renderer(s, sequence, {.sampleRate = kRate, .tail = 0.0});
    const std::vector<float> stereo = renderer.renderRest();
    std::vector<float> offline;
    for (std::size_t i = 0; i < stereo.size(); i += 2) {
        offline.push_back(stereo[i]);
    }

    Player player(kRate, 256); // blocks longer than the mixing buffers are split
    player.load(s, sequence);
    player.play();
    const std::vector<float> live = play(player, 4.0, 1000);
    CHECK(live == offline);
    CHECK_FALSE(player.isPlaying()); // the end of the song
    CHECK_THAT(player.position(), WithinAbs(4.0, kFrame));
}

TEST_CASE("Player: a pause holds the position and lets the notes ring out", "[audio][player]") {
    const Song s = song(2, 60);
    Player player(kRate);
    player.load(s, Sequence(s));
    player.play();
    CHECK(peakLevel(play(player, 0.3)) > 0.02f);
    player.pause();
    const std::vector<float> paused = play(player, 2.0);
    CHECK_THAT(player.position(), WithinAbs(0.3, kFrame));
    CHECK(peakLevel(between(paused, 0.0, 0.02)) > 0.0f); // still ringing
    CHECK(peakLevel(between(paused, 1.9, 2.0)) == 0.0f); // rung out

    player.play();
    CHECK(peakLevel(play(player, 0.3)) > 0.02f); // the organ's next note at 0.5 s
    CHECK_THAT(player.position(), WithinAbs(0.6, kFrame));
}

TEST_CASE("Player: a seek goes on from there without striking earlier notes", "[audio][player]") {
    const Song s = song(2, 60, false); // notes at 0 s and 2 s only
    Player player(kRate);
    player.load(s, Sequence(s));
    player.play();
    static_cast<void>(play(player, 0.2));
    player.seek(1.0);
    CHECK_THAT(player.position(), WithinAbs(1.0, kFrame)); // before the audio thread takes it
    const std::vector<float> after = play(player, 1.5);
    CHECK(peakLevel(between(after, 0.0, 0.95)) == 0.0f); // the notes at 0 s are cut
    CHECK(peakLevel(between(after, 1.0, 1.5)) > 0.02f);  // the notes at 2 s
    CHECK_THAT(player.position(), WithinAbs(2.5, kFrame));

    player.seek(-3.0); // clamped to the song
    CHECK(player.position() == 0.0);
    player.seek(99.0);
    CHECK_THAT(player.position(), WithinAbs(4.0, kFrame));
}

TEST_CASE("Player: after the end, play starts again from the start", "[audio][player]") {
    const Song s = song(1, 60);
    Player player(kRate);
    player.load(s, Sequence(s));
    player.play();
    static_cast<void>(play(player, 2.5));
    CHECK_FALSE(player.isPlaying());
    CHECK_THAT(player.position(), WithinAbs(2.0, kFrame));
    player.play();
    CHECK(player.isPlaying());
    static_cast<void>(play(player, 0.5));
    CHECK_THAT(player.position(), WithinAbs(0.5, kFrame));
}

TEST_CASE("Player: a song loaded while playing takes over at the same position",
          "[audio][player]") {
    const Song first = song(4, 60);
    Song second = song(4, 60);
    for (std::size_t t = 0; t < 2; ++t) {
        for (std::size_t m = 0; m < 4; ++m) {
            second.track(t).staff(0).measure(m).voices[0].beats = {beat(NoteValue::Whole)};
        }
    }
    Player player(kRate);
    player.load(first, Sequence(first));
    player.play();
    CHECK(peakLevel(play(player, 1.0)) > 0.02f);
    player.load(second, Sequence(second)); // the same length, all rests
    const std::vector<float> after = play(player, 1.0);
    CHECK(player.isPlaying());
    CHECK_THAT(player.position(), WithinAbs(2.0, kFrame));
    CHECK(peakLevel(after) == 0.0f);
    player.tidy();
}

TEST_CASE("Player: control calls from another thread while the audio thread renders",
          "[audio][player]") {
    const Song a = song(8, 60);
    const Song b = song(8, 64);
    const Sequence sa(a);
    const Sequence sb(b);
    Player player(kRate, 512);
    player.load(a, sa);
    std::atomic<bool> done{false};
    std::thread audio([&] {
        std::vector<float> left(256);
        std::vector<float> right(256);
        while (!done.load()) {
            player.render(left, right);
        }
    });
    for (int i = 0; i < 200; ++i) {
        player.load(i % 2 == 0 ? b : a, i % 2 == 0 ? sb : sa);
        player.play();
        player.seek(static_cast<double>(i % 16));
        if (i % 3 == 0) {
            player.pause();
        }
        static_cast<void>(player.position());
        player.tidy();
    }
    done.store(true);
    audio.join();
    player.tidy();
    CHECK(player.position() <= player.duration());
}
