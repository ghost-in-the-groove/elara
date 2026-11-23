// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer: a song as it is played, in seconds (ADR-0017).
#include <elara/audio/sequence.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::EventKind;
using elara::audio::NoteEvent;
using elara::audio::PlayedMeasure;
using elara::audio::PlayedNote;
using elara::audio::Sequence;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Fraction;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::Track;

namespace {

constexpr double kEpsilon = 1e-9;

// `measures` measures of 4/4 at quarter = 120 (two seconds each) on a piano.
Song pianoSong(std::size_t measures) {
    Song song;
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano", .kind = InstrumentKind::Keyboard}, {Staff{}}));
    song.appendMeasures(measures);
    return song;
}

Note note(int key, bool tied = false) {
    Note result{.pitch = Pitch::fromMidi(key)};
    result.effects.tiedFromPrevious = tied;
    return result;
}

Beat beat(NoteValue value, std::vector<Note> notes = {}) {
    return Beat{.duration = Duration(value), .notes = std::move(notes)};
}

std::vector<Beat>& voice(Song& song, std::size_t measure, std::size_t v = 0) {
    return song.track(0).staff(0).measure(measure).voices.at(v).beats;
}

std::vector<std::size_t> writtenOrder(const Sequence& sequence) {
    std::vector<std::size_t> order;
    for (const PlayedMeasure& measure : sequence.measures()) {
        order.push_back(measure.measure);
    }
    return order;
}

} // namespace

TEST_CASE("Sequence: measures and notes in seconds from the tempo", "[audio][sequence]") {
    Song song = pianoSong(2);
    voice(song, 0) = {beat(NoteValue::Quarter, {note(60)}), beat(NoteValue::Quarter),
                      beat(NoteValue::Half, {note(64), note(67)})};
    voice(song, 1) = {beat(NoteValue::Whole, {note(72)})};
    const Sequence sequence(song);

    CHECK(writtenOrder(sequence) == std::vector<std::size_t>{0, 1});
    CHECK_THAT(sequence.measures()[1].start, WithinAbs(2.0, kEpsilon));
    CHECK_THAT(sequence.measures()[1].end, WithinAbs(4.0, kEpsilon));
    CHECK_THAT(sequence.duration(), WithinAbs(4.0, kEpsilon));

    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].key == 60);
    CHECK_THAT(notes[0].start, WithinAbs(0.0, kEpsilon));
    CHECK_THAT(notes[0].end, WithinAbs(0.5, kEpsilon));
    CHECK(notes[1].key == 64);
    CHECK(notes[2].key == 67);
    CHECK_THAT(notes[2].start, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(notes[2].end, WithinAbs(2.0, kEpsilon));
    CHECK(notes[3].key == 72);
    CHECK(notes[3].pass == 1);
    CHECK_THAT(notes[3].end, WithinAbs(4.0, kEpsilon));
}

TEST_CASE("Sequence: repeats play their measures again", "[audio][sequence]") {
    // |: 0 :| 1
    Song song = pianoSong(2);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).repeatTimes = 2;
    voice(song, 0) = {beat(NoteValue::Whole, {note(60)})};
    voice(song, 1) = {beat(NoteValue::Whole, {note(62)})};
    const Sequence sequence(song);

    CHECK(writtenOrder(sequence) == std::vector<std::size_t>{0, 0, 1});
    CHECK_THAT(sequence.duration(), WithinAbs(6.0, kEpsilon));
    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 3);
    CHECK(notes[0].key == 60);
    CHECK(notes[0].pass == 0);
    CHECK(notes[1].key == 60);
    CHECK(notes[1].pass == 1);
    CHECK_THAT(notes[1].start, WithinAbs(2.0, kEpsilon));
    CHECK(notes[2].key == 62);
    CHECK_THAT(notes[2].start, WithinAbs(4.0, kEpsilon));
}

TEST_CASE("Sequence: the played measure at a time", "[audio][sequence]") {
    // |: 0 :| 1, two seconds each: played 0 (0-2 s), 0 (2-4 s), 1 (4-6 s).
    Song song = pianoSong(2);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).repeatTimes = 2;
    const Sequence sequence(song);
    CHECK(sequence.playedMeasureAt(-1.0) == 0);
    CHECK(sequence.playedMeasureAt(0.0) == 0);
    CHECK(sequence.playedMeasureAt(1.999) == 0);
    CHECK(sequence.playedMeasureAt(2.0) == 1);
    CHECK(sequence.playedMeasureAt(5.5) == 2);
    CHECK(sequence.playedMeasureAt(6.0) == 2); // the end belongs to the last measure
    CHECK(sequence.playedMeasureAt(60.0) == 3);
    CHECK(Sequence(pianoSong(0)).playedMeasureAt(0.0) == 0);
}

TEST_CASE("Sequence: every measure keeps the tempo written there", "[audio][sequence]") {
    // |: 0 | 1 (quarter = 60) :|: back at the start, quarter = 120 again.
    Song song = pianoSong(2);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).repeatTimes = 2;
    song.measureHeader(1).tempoChanges.push_back(TempoChange{.tempo = Tempo(60.0)});
    voice(song, 1) = {beat(NoteValue::Half), beat(NoteValue::Half, {note(65)})};
    const Sequence sequence(song);

    CHECK(writtenOrder(sequence) == std::vector<std::size_t>{0, 1, 0, 1});
    std::vector<double> starts;
    for (const PlayedMeasure& measure : sequence.measures()) {
        starts.push_back(measure.start);
    }
    REQUIRE(starts.size() == 4);
    CHECK_THAT(starts[1], WithinAbs(2.0, kEpsilon));
    CHECK_THAT(starts[2], WithinAbs(6.0, kEpsilon));
    CHECK_THAT(starts[3], WithinAbs(8.0, kEpsilon));
    CHECK_THAT(sequence.duration(), WithinAbs(12.0, kEpsilon));
    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[0].start, WithinAbs(4.0, kEpsilon));
    CHECK_THAT(notes[0].end, WithinAbs(6.0, kEpsilon));
    CHECK_THAT(notes[1].start, WithinAbs(10.0, kEpsilon));

    SECTION("a tempo change inside a measure") {
        Song inside = pianoSong(1);
        inside.measureHeader(0).tempoChanges.push_back(
            TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(60.0)});
        voice(inside, 0) = {beat(NoteValue::Whole, {note(60)})};
        const Sequence played(inside);
        CHECK_THAT(played.duration(), WithinAbs(3.0, kEpsilon)); // 1 s, then 2 s
        REQUIRE(played.notes().size() == 1);
        CHECK_THAT(played.notes()[0].end, WithinAbs(3.0, kEpsilon));
    }
}

TEST_CASE("Sequence: tied notes sound for their whole written length", "[audio][sequence]") {
    Song song = pianoSong(2);
    song.measureHeader(1).tempoChanges.push_back(TempoChange{.tempo = Tempo(60.0)});
    voice(song, 0) = {beat(NoteValue::Half), beat(NoteValue::Half, {note(60)})};
    voice(song, 1) = {beat(NoteValue::Quarter, {note(60, true)})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 1);
    const PlayedNote& tied = sequence.notes()[0];
    CHECK_THAT(tied.start, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(tied.end, WithinAbs(3.0, kEpsilon)); // 1 s at 120, then 1 s at 60
}

TEST_CASE("Sequence: note events for a synthesizer", "[audio][sequence]") {
    // Two voices strike the same key while it sounds: it is struck again and
    // released when the last of its notes ends.
    Song song = pianoSong(1);
    voice(song, 0, 0) = {beat(NoteValue::Whole, {note(60)})};
    voice(song, 0, 1) = {beat(NoteValue::Quarter), beat(NoteValue::Quarter, {note(60)}),
                         beat(NoteValue::Quarter, {note(64)})};
    const Sequence sequence(song);
    const auto& events = sequence.events();
    REQUIRE(events.size() == 6);
    const auto is = [](const NoteEvent& event, double time, EventKind kind, int key) {
        return event.time > time - kEpsilon && event.time < time + kEpsilon && event.kind == kind &&
               event.key == key && event.track == 0;
    };
    CHECK(is(events[0], 0.0, EventKind::NoteOn, 60));
    CHECK(is(events[1], 0.5, EventKind::NoteOff, 60)); // struck again
    CHECK(is(events[2], 0.5, EventKind::NoteOn, 60));
    CHECK(is(events[3], 1.0, EventKind::NoteOn, 64));
    CHECK(is(events[4], 1.5, EventKind::NoteOff, 64));
    CHECK(is(events[5], 2.0, EventKind::NoteOff, 60)); // the whole note ends
    CHECK(events[0].velocity == sequence.notes()[0].velocity);

    // Seeking: the first event at or after a time.
    CHECK(sequence.eventAt(0.0) == 0);
    CHECK(sequence.eventAt(0.5) == 1);
    CHECK(sequence.eventAt(0.75) == 3);
    CHECK(sequence.eventAt(10.0) == events.size());
}

TEST_CASE("Sequence: an empty song plays nothing", "[audio][sequence]") {
    const Sequence sequence{Song{}};
    CHECK(sequence.measures().empty());
    CHECK(sequence.notes().empty());
    CHECK(sequence.events().empty());
    CHECK(sequence.duration() == 0.0);
}

TEST_CASE("Sequence: a note tied across an end repeat sounds into the measure played next",
          "[audio][sequence]") {
    // |: 0 | 1 :| 2, a note in measure 1 tied into measure 2.
    Song song = pianoSong(3);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).repeatTimes = 2;
    voice(song, 1) = {beat(NoteValue::Half), beat(NoteValue::Half, {note(67)})};
    voice(song, 2) = {beat(NoteValue::Quarter, {note(67, true)})};
    const Sequence sequence(song);
    CHECK(writtenOrder(sequence) == std::vector<std::size_t>{0, 1, 0, 1, 2});
    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[0].start, WithinAbs(3.0, kEpsilon));
    CHECK_THAT(notes[0].end, WithinAbs(4.5, kEpsilon)); // over the start of measure 0
    CHECK_THAT(notes[1].start, WithinAbs(7.0, kEpsilon));
    CHECK_THAT(notes[1].end, WithinAbs(8.5, kEpsilon)); // into measure 2
}

TEST_CASE("Sequence: a key struck by two voices at once is struck once", "[audio][sequence]") {
    Song song = pianoSong(1);
    voice(song, 0, 0) = {beat(NoteValue::Whole, {note(60)})};
    voice(song, 0, 1) = {beat(NoteValue::Quarter, {note(60)})};
    const Sequence sequence(song);
    REQUIRE(sequence.events().size() == 2);
    CHECK(sequence.events()[0].kind == EventKind::NoteOn);
    CHECK(sequence.events()[1].kind == EventKind::NoteOff);
    CHECK_THAT(sequence.events()[1].time, WithinAbs(2.0, kEpsilon));
}
