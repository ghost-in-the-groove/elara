// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Duration;
using elara::core::Fraction;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::NoteValue;
using elara::core::Song;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::TimeSignature;

TEST_CASE("Measures start in 4/4, C major, at 120 quarter notes per minute", "[core][timeline]") {
    Song song;
    song.appendMeasures(1);
    CHECK(song.measureHeader(0).timeSignature == TimeSignature(4, 4));
    CHECK(song.measureHeader(0).keySignature == KeySignature{});
    CHECK(song.startTempo() == Tempo{});
    CHECK(Tempo{}.bpm() == 120.0);
    CHECK(Tempo{}.beatUnit() == Duration(NoteValue::Quarter));
}

TEST_CASE("New measures continue the meter and key around them", "[core][timeline]") {
    Song song;
    song.appendMeasures(2);
    song.measureHeader(1).timeSignature = TimeSignature(3, 4);
    song.measureHeader(1).keySignature = KeySignature(2, KeyMode::Major);
    song.measureHeader(1).tempoChanges.push_back({.offset = Fraction(0), .tempo = Tempo(90)});

    song.appendMeasures(1); // continues measure 1
    CHECK(song.measureHeader(2).timeSignature == TimeSignature(3, 4));
    CHECK(song.measureHeader(2).keySignature == KeySignature(2, KeyMode::Major));
    CHECK(song.measureHeader(2).tempoChanges.empty()); // events are not copied

    song.insertMeasures(1); // continues measure 0, which precedes it
    CHECK(song.measureHeader(1).timeSignature == TimeSignature(4, 4));
    CHECK(song.measureHeader(2).timeSignature == TimeSignature(3, 4));
    CHECK(song.measureHeader(2).tempoChanges.size() == 1); // moved with its measure

    song.insertMeasures(0); // nothing precedes: continues the old first measure
    CHECK(song.measureHeader(0).timeSignature == TimeSignature(4, 4));
}

TEST_CASE("Measure starts add up the meters before them", "[core][timeline]") {
    Song song;
    song.appendMeasures(4);
    song.measureHeader(2).timeSignature = TimeSignature(3, 4);
    song.measureHeader(3).timeSignature = TimeSignature(6, 8);
    CHECK(song.measureStart(0) == Fraction(0));
    CHECK(song.measureStart(1) == Fraction(1));
    CHECK(song.measureStart(3) == Fraction(11, 4));
    CHECK(song.measureStart(4) == Fraction(7, 2)); // end of the song
    CHECK(song.length() == Fraction(7, 2));
    CHECK_THROWS_AS(song.measureStart(5), std::out_of_range);
}

TEST_CASE("Seconds follow the tempo map", "[core][timeline]") {
    Song song;
    song.appendMeasures(4);
    // 4/4 at quarter = 120: two seconds per measure.
    CHECK_THAT(song.secondsAt(1), WithinAbs(2.0, 1e-12));
    CHECK_THAT(song.secondsAt(1, Fraction(1, 2)), WithinAbs(3.0, 1e-12));

    // Slow down to 60 from the third measure, halfway through it speed up to 240.
    song.measureHeader(2).tempoChanges = {
        TempoChange{.offset = Fraction(0), .tempo = Tempo(60)},
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(240)},
    };
    CHECK_THAT(song.secondsAt(2), WithinAbs(4.0, 1e-12));
    CHECK_THAT(song.secondsAt(2, Fraction(1, 2)), WithinAbs(6.0, 1e-12));
    CHECK_THAT(song.secondsAt(3), WithinAbs(6.5, 1e-12));
    CHECK_THAT(song.secondsAt(4), WithinAbs(7.5, 1e-12)); // end of the song
    CHECK_THAT(song.durationSeconds(), WithinAbs(7.5, 1e-12));
    CHECK(song.tempoAt(2, Fraction(1, 4)) == Tempo(60));
    CHECK(song.tempoAt(3) == Tempo(240));
    CHECK(song.tempoAt(0) == Tempo(120));
    CHECK_THROWS_AS(song.secondsAt(5), std::out_of_range);
}

TEST_CASE("Tempo can count dotted beats", "[core][timeline]") {
    Song song;
    song.appendMeasures(2);
    song.measureHeader(0).timeSignature = TimeSignature(6, 8);
    song.measureHeader(1).timeSignature = TimeSignature(6, 8);
    song.setStartTempo(Tempo(60, Duration(NoteValue::Quarter, 1))); // dotted quarter = 60
    CHECK_THAT(song.secondsAt(1), WithinAbs(2.0, 1e-12));           // two dotted beats
    CHECK_THAT(song.durationSeconds(), WithinAbs(4.0, 1e-12));
}

TEST_CASE("Tempo rejects implausible speeds", "[core][timeline]") {
    CHECK_NOTHROW(Tempo(Tempo::kMinBpm));
    CHECK_NOTHROW(Tempo(Tempo::kMaxBpm));
    CHECK_THROWS_AS(Tempo(Tempo::kMinBpm - 1), std::invalid_argument);
    CHECK_THROWS_AS(Tempo(Tempo::kMaxBpm + 1), std::invalid_argument);
    CHECK_THROWS_AS(Tempo(0), std::invalid_argument);
}

namespace {

// A song of `count` empty measures.
Song measures(std::size_t count) {
    Song song;
    song.appendMeasures(count);
    return song;
}

// The alternate ending bit of the n-th time through.
constexpr std::uint8_t ending(int n) {
    return static_cast<std::uint8_t>(1U << (n - 1));
}

using Order = std::vector<std::size_t>;

} // namespace

TEST_CASE("Without repeats measures are played in order", "[core][timeline][repeats]") {
    CHECK(measures(3).playOrder() == Order{0, 1, 2});
    CHECK(measures(0).playOrder().empty());
}

TEST_CASE("A repeat plays its section as many times as it says", "[core][timeline][repeats]") {
    Song song = measures(4);
    song.measureHeader(1).repeatStart = true;
    song.measureHeader(2).repeatTimes = 2;
    CHECK(song.playOrder() == Order{0, 1, 2, 1, 2, 3});

    song.measureHeader(2).repeatTimes = 3;
    CHECK(song.playOrder() == Order{0, 1, 2, 1, 2, 1, 2, 3});
}

TEST_CASE("An end repeat without a start goes back to the start or to the previous repeat",
          "[core][timeline][repeats]") {
    Song song = measures(4);
    song.measureHeader(1).repeatTimes = 2;
    CHECK(song.playOrder() == Order{0, 1, 0, 1, 2, 3});

    song.measureHeader(3).repeatTimes = 2;
    CHECK(song.playOrder() == Order{0, 1, 0, 1, 2, 3, 2, 3});
}

TEST_CASE("Each time through plays only its alternate endings", "[core][timeline][repeats]") {
    // |: 0 | 1 |1. 2 :|2. 3 | 4
    Song song = measures(5);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(2).alternateEndings = ending(1);
    song.measureHeader(2).repeatTimes = 2;
    song.measureHeader(3).alternateEndings = ending(2);
    CHECK(song.playOrder() == Order{0, 1, 2, 0, 1, 3, 4});

    // |: 0 | 1 |1.2. 2 :|3. 3 | 4, played three times
    song.measureHeader(2).alternateEndings = ending(1) | ending(2);
    song.measureHeader(2).repeatTimes = 3;
    song.measureHeader(3).alternateEndings = ending(3);
    CHECK(song.playOrder() == Order{0, 1, 2, 0, 1, 2, 0, 1, 3, 4});
}

TEST_CASE("After the alternate endings a new section starts", "[core][timeline][repeats]") {
    // |: 0 |1. 1 :| 2 | 3 :|  - the second repeat goes back to measure 2
    Song song = measures(4);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).alternateEndings = ending(1);
    song.measureHeader(1).repeatTimes = 2;
    song.measureHeader(3).repeatTimes = 2;
    CHECK(song.playOrder() == Order{0, 1, 0, 2, 3, 2, 3});
}

TEST_CASE("Endings for a time that never comes are not played", "[core][timeline][repeats]") {
    Song song = measures(3);
    song.measureHeader(1).alternateEndings = ending(3);
    CHECK(song.playOrder() == Order{0, 2});
}
