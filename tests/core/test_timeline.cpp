// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Fermata;
using elara::core::FermataHold;
using elara::core::Fraction;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pedal;
using elara::core::PedalSpan;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::TimeSignature;
using elara::core::Track;

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

TEST_CASE("A gradual tempo change moves to the next tempo along the score", "[core][timeline]") {
    // ADR-0082: quarter = 60 (a quarter of a whole note a second) to 120 over
    // the second measure. The speed grows by 1/4 per whole note, so a whole
    // note takes 4 ln 2 seconds instead of 4 or 2, and its first half 4 ln 1.5.
    Song song;
    song.appendMeasures(4);
    song.setStartTempo(Tempo(60));
    song.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(0), .tempo = Tempo(60), .gradual = true}};
    song.measureHeader(2).tempoChanges = {TempoChange{.offset = Fraction(0), .tempo = Tempo(120)}};
    CHECK_THAT(song.secondsAt(1), WithinAbs(4.0, 1e-12));
    CHECK_THAT(song.secondsAt(1, Fraction(1, 2)), WithinAbs(4.0 + 4.0 * std::log(1.5), 1e-12));
    CHECK_THAT(song.secondsAt(2), WithinAbs(4.0 + 4.0 * std::log(2.0), 1e-12));
    CHECK_THAT(song.secondsAt(3), WithinAbs(6.0 + 4.0 * std::log(2.0), 1e-12));
    // The written tempo is the change's, not the one heard on the way.
    CHECK(song.tempoAt(1, Fraction(1, 2)) == Tempo(60));

    // Slowing down across a bar line, from the middle of one measure to the
    // middle of the next, in dotted quarters: 120 to 60 quarter-equivalents.
    Song slower;
    slower.appendMeasures(2);
    slower.measureHeader(0).tempoChanges = {
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(120), .gradual = true}};
    slower.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(40, Duration(NoteValue::Quarter, 1))}};
    // The speed falls from 1/2 to 1/4 whole note a second over one whole note.
    CHECK_THAT(slower.secondsAt(1, Fraction(1, 2)), WithinAbs(1.0 + 4.0 * std::log(2.0), 1e-12));
    CHECK_THAT(slower.durationSeconds(), WithinAbs(3.0 + 4.0 * std::log(2.0), 1e-12));

    // The last gradual change has nowhere to go: it holds.
    Song held;
    held.appendMeasures(2);
    held.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(0), .tempo = Tempo(60), .gradual = true}};
    CHECK_THAT(held.durationSeconds(), WithinAbs(6.0, 1e-12));
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

namespace {

Beat held(NoteValue value, Fermata fermata = Fermata::None, bool rest = false) {
    Beat beat{.duration = Duration(value)};
    if (!rest) {
        beat.notes.push_back(Note{.pitch = Pitch::fromMidi(60)});
    }
    beat.effects.fermata = fermata;
    return beat;
}

} // namespace

TEST_CASE("A fermata holds its beat longer", "[core][timeline][fermata]") {
    // ADR-0085: at quarter = 60 a quarter note takes a second; under a normal
    // fermata two, under a short one one and a half, under a long one three.
    Song song;
    song.setStartTempo(Tempo(60));
    song.addTrack(Track("Voice", Instrument{.kind = InstrumentKind::Pitched}, {Staff{}}));
    song.addTrack(Track("Piano", Instrument{.kind = InstrumentKind::Keyboard}, {Staff{}}));
    song.appendMeasures(3);
    auto& voice = song.track(0).staff(0);
    voice.measure(0).voices[0].beats = {held(NoteValue::Quarter), held(NoteValue::Quarter),
                                        held(NoteValue::Half, Fermata::Normal)};
    voice.measure(1).voices[0].beats = {held(NoteValue::Quarter, Fermata::Short),
                                        held(NoteValue::Quarter, Fermata::None, true),
                                        held(NoteValue::Half, Fermata::Long, true)};
    // The piano holds the second half of the half note longer still.
    song.track(1).staff(0).measure(0).voices[0].beats = {held(NoteValue::Half),
                                                         held(NoteValue::Quarter, Fermata::Normal),
                                                         held(NoteValue::Quarter, Fermata::Long)};

    CHECK(song.fermataHolds(0) ==
          std::vector<FermataHold>{{.start = Fraction(1, 2), .end = Fraction(3, 4), .stretch = 2.0},
                                   {.start = Fraction(3, 4), .end = Fraction(1), .stretch = 3.0}});
    CHECK(song.fermataHolds(1) ==
          std::vector<FermataHold>{{.start = Fraction(0), .end = Fraction(1, 4), .stretch = 1.5},
                                   {.start = Fraction(1, 2), .end = Fraction(1), .stretch = 3.0}});
    CHECK(song.fermataHolds(2).empty());
    CHECK_THROWS_AS(song.fermataHolds(3), std::out_of_range);

    CHECK_THAT(song.secondsAt(0, Fraction(1, 2)), WithinAbs(2.0, 1e-12));
    CHECK_THAT(song.secondsAt(0, Fraction(5, 8)), WithinAbs(3.0, 1e-12)); // halfway: 2 + 1
    CHECK_THAT(song.secondsAt(1), WithinAbs(2.0 + 2.0 + 3.0, 1e-12));
    CHECK_THAT(song.secondsAt(1, Fraction(1, 4)), WithinAbs(8.5, 1e-12));
    CHECK_THAT(song.secondsAt(2), WithinAbs(8.5 + 1.0 + 6.0, 1e-12));
    CHECK_THAT(song.durationSeconds(), WithinAbs(19.5, 1e-12));
}

namespace {

Beat pedalled(NoteValue value, Pedal pedal, bool rest = false) {
    Beat beat = held(value, Fermata::None, rest);
    beat.effects.pedal = pedal;
    return beat;
}

} // namespace

TEST_CASE("The sustain pedal of a track goes over the beats of all its staves",
          "[core][timeline][pedal]") {
    // ADR-0092: the beats under the pedal in any staff and voice of the track
    // make one pedalling; a change lifts it and presses it again where its
    // beat starts, also inside a beat of another staff still under it.
    Song song;
    song.addTrack(Track("Voice", Instrument{.kind = InstrumentKind::Pitched}, {Staff{}}));
    song.addTrack(
        Track("Piano", Instrument{.kind = InstrumentKind::Keyboard}, {Staff{}, Staff{}}));
    song.appendMeasures(3);
    Track& piano = song.track(1);
    piano.staff(0).measure(0).voices[0].beats = {
        pedalled(NoteValue::Quarter, Pedal::Down), pedalled(NoteValue::Quarter, Pedal::Change),
        pedalled(NoteValue::Quarter, Pedal::None), pedalled(NoteValue::Quarter, Pedal::Change)};
    piano.staff(1).measure(0).voices[0].beats = {pedalled(NoteValue::Half, Pedal::Down),
                                                 pedalled(NoteValue::Half, Pedal::None)};
    // A rest goes on under it; beats under it in a row and overlapping join.
    piano.staff(0).measure(1).voices[0].beats = {pedalled(NoteValue::Quarter, Pedal::None),
                                                 pedalled(NoteValue::Quarter, Pedal::Down),
                                                 pedalled(NoteValue::Half, Pedal::None)};
    piano.staff(1).measure(1).voices[0].beats = {pedalled(NoteValue::Quarter, Pedal::Down, true),
                                                 pedalled(NoteValue::Quarter, Pedal::Down),
                                                 pedalled(NoteValue::Half, Pedal::None)};

    CHECK(song.pedalSpans(1, 0) ==
          std::vector<PedalSpan>{{.start = Fraction(0), .end = Fraction(1, 4), .change = false},
                                 {.start = Fraction(1, 4), .end = Fraction(1, 2), .change = true},
                                 {.start = Fraction(3, 4), .end = Fraction(1), .change = true}});
    CHECK(song.pedalSpans(1, 1) ==
          std::vector<PedalSpan>{{.start = Fraction(0), .end = Fraction(1, 2), .change = false}});
    CHECK(song.pedalSpans(1, 2).empty());
    CHECK(song.pedalSpans(0, 0).empty());
    CHECK_THROWS_AS(song.pedalSpans(1, 3), std::out_of_range);
    CHECK_THROWS_AS(song.pedalSpans(2, 0), std::out_of_range);
}

TEST_CASE("A fermata in a gradual tempo change holds what it takes there",
          "[core][timeline][fermata]") {
    // Quarter = 60 to 120 over the first measure (ADR-0082); a fermata over its
    // second half doubles the time of that half: 4 (ln 2 - ln 1.5) seconds.
    Song song;
    song.setStartTempo(Tempo(60));
    song.addTrack(Track("Voice", Instrument{.kind = InstrumentKind::Pitched}, {Staff{}}));
    song.appendMeasures(2);
    song.measureHeader(0).tempoChanges = {
        TempoChange{.offset = Fraction(0), .tempo = Tempo(60), .gradual = true}};
    song.measureHeader(1).tempoChanges = {TempoChange{.offset = Fraction(0), .tempo = Tempo(120)}};
    song.track(0).staff(0).measure(0).voices[0].beats = {held(NoteValue::Half),
                                                         held(NoteValue::Half, Fermata::Normal)};
    const double half = 4.0 * (std::log(2.0) - std::log(1.5));
    CHECK_THAT(song.secondsAt(1), WithinAbs(4.0 * std::log(2.0) + half, 1e-12));
}
