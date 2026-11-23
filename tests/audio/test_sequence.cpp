// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer: a song as it is played, in seconds (ADR-0017).
#include <elara/audio/sequence.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::EventKind;
using elara::audio::NoteEvent;
using elara::audio::PlayedMeasure;
using elara::audio::PlayedNote;
using elara::audio::Sequence;
using elara::audio::Touch;
using elara::core::Accent;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::Fraction;
using elara::core::HarmonicKind;
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
using elara::core::Tuning;

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

// `measures` measures of 4/4 at quarter = 120 on a guitar in standard tuning.
Song guitarSong(std::size_t measures) {
    Song song;
    song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .kind = InstrumentKind::Fretted},
                        {Staff(elara::core::Clef::Treble8vb, Tuning::preset("guitar"))}));
    song.appendMeasures(measures);
    return song;
}

Note fretted(const Song& song, int string, int fret) {
    return song.track(0).staff(0).frettedNote(string, fret);
}

std::vector<int> velocities(const Sequence& sequence) {
    std::vector<int> result;
    for (const PlayedNote& note : sequence.notes()) {
        result.push_back(note.velocity);
    }
    return result;
}

std::vector<int> keys(const Sequence& sequence) {
    std::vector<int> result;
    for (const PlayedNote& note : sequence.notes()) {
        result.push_back(note.key);
    }
    return result;
}

std::vector<Touch> touches(const Sequence& sequence) {
    std::vector<Touch> result;
    for (const PlayedNote& note : sequence.notes()) {
        result.push_back(note.touch);
    }
    return result;
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

// Articulations (ADR-0022).
TEST_CASE("Sequence: accents, marcato and ghost notes play louder or softer by dynamics",
          "[audio][sequence][articulation]") {
    Song song = pianoSong(2);
    const auto with = [](int key, Accent accent, bool ghost, Dynamic dynamic = Dynamic::MF) {
        Note result = note(key);
        result.effects.accent = accent;
        result.effects.ghost = ghost;
        result.effects.dynamic = dynamic;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Quarter, {with(60, Accent::Accent, false)}),
                      beat(NoteValue::Quarter, {with(62, Accent::Marcato, false)}),
                      beat(NoteValue::Quarter, {with(64, Accent::None, true)}),
                      beat(NoteValue::Quarter, {with(65, Accent::Marcato, false, Dynamic::FFF)})};
    voice(song, 1) = {beat(NoteValue::Half, {with(67, Accent::Accent, true)}),
                      beat(NoteValue::Half, {with(69, Accent::None, true, Dynamic::PPP)})};
    const Sequence sequence(song);
    // mf is 80; one dynamic is 16; the velocity stays within 1..127.
    CHECK(velocities(sequence) == std::vector<int>{96, 112, 48, 127, 64, 1});
    CHECK(sequence.events().front().velocity == 96);
}

TEST_CASE("Sequence: staccato notes sound half their written length",
          "[audio][sequence][articulation]") {
    Song song = pianoSong(1);
    Note shortened = note(60);
    shortened.effects.staccato = true;
    voice(song, 0) = {beat(NoteValue::Half, {shortened}), beat(NoteValue::Half, {note(62)})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 2);
    CHECK_THAT(sequence.notes()[0].end, WithinAbs(0.5, kEpsilon));
    CHECK_THAT(sequence.notes()[1].start, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(sequence.notes()[1].end, WithinAbs(2.0, kEpsilon));
}

TEST_CASE("Sequence: let ring holds a note until its string is struck again",
          "[audio][sequence][articulation]") {
    Song song = guitarSong(2);
    const auto ringing = [&](int string, int fret) {
        Note result = fretted(song, string, fret);
        result.effects.letRing = true;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Quarter, {ringing(1, 0)}),
                      beat(NoteValue::Quarter, {ringing(2, 1)}),
                      beat(NoteValue::Quarter, {fretted(song, 3, 2)}),
                      beat(NoteValue::Quarter, {fretted(song, 1, 3)})};
    voice(song, 1) = {beat(NoteValue::Half, {fretted(song, 2, 0), ringing(4, 2)}),
                      beat(NoteValue::Half)};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 6);
    const auto endOf = [&](int key) {
        const auto found = std::ranges::find(sequence.notes(), key, &PlayedNote::key);
        REQUIRE(found != sequence.notes().end());
        return found->end;
    };
    CHECK_THAT(endOf(64), WithinAbs(1.5, kEpsilon)); // string 1 struck again at 1.5
    CHECK_THAT(endOf(60), WithinAbs(2.0, kEpsilon)); // string 2 struck again at 2.0
    CHECK_THAT(endOf(57), WithinAbs(1.5, kEpsilon)); // no let ring: as written
    CHECK_THAT(endOf(52), WithinAbs(4.0, kEpsilon)); // never struck again: to the end
}

TEST_CASE("Sequence: palm mutes, dead notes and harmonics strike the string their own way",
          "[audio][sequence][articulation]") {
    Song song = guitarSong(1);
    const auto played = [&](bool palmMute, bool dead, HarmonicKind harmonic) {
        Note result = fretted(song, 6, 12);
        result.effects.palmMute = palmMute;
        result.effects.deadNote = dead;
        result.effects.harmonic = {.kind = harmonic, .fretTenths = 120};
        return result;
    };
    voice(song, 0) = {
        beat(NoteValue::Eighth, {played(true, false, HarmonicKind::None)}),
        beat(NoteValue::Eighth, {played(false, true, HarmonicKind::None)}),
        beat(NoteValue::Eighth, {played(false, false, HarmonicKind::Natural)}),
        beat(NoteValue::Eighth, {played(true, true, HarmonicKind::Natural)}),
        beat(NoteValue::Eighth, {played(true, false, HarmonicKind::Natural)}),
        beat(NoteValue::Eighth, {played(false, false, HarmonicKind::None)}),
    };
    const Sequence sequence(song);
    // A dead note is dead whatever else it is; a harmonic beats a palm mute.
    CHECK(touches(sequence) == std::vector<Touch>{Touch::PalmMute, Touch::Dead, Touch::Harmonic,
                                                  Touch::Dead, Touch::Harmonic, Touch::Normal});
    std::vector<Touch> struck;
    for (const NoteEvent& event : sequence.events()) {
        if (event.kind == EventKind::NoteOn) {
            struck.push_back(event.touch);
        }
    }
    CHECK(struck == touches(sequence));
    CHECK(sizeof(NoteEvent) == 32); // the touch fits in padding
}

TEST_CASE("Sequence: a harmonic sounds the partial of its node",
          "[audio][sequence][articulation]") {
    Song song = guitarSong(1);
    const auto harmonic = [&](int string, int fret, HarmonicKind kind, int tenths) {
        Note result = fretted(song, string, fret);
        result.effects.harmonic = {.kind = kind, .fretTenths = static_cast<std::int16_t>(tenths)};
        return result;
    };
    voice(song, 0) = {
        beat(NoteValue::Eighth, {harmonic(6, 7, HarmonicKind::Natural, 70)}),   // E2 + 19
        beat(NoteValue::Eighth, {harmonic(6, 12, HarmonicKind::Natural, 120)}), // E2 + 12
        beat(NoteValue::Eighth, {harmonic(6, 5, HarmonicKind::Natural, 50)}),   // E2 + 24
        beat(NoteValue::Eighth, {harmonic(6, 1, HarmonicKind::Natural, 10)}),   // no node
        beat(NoteValue::Eighth, {harmonic(3, 2, HarmonicKind::Artificial, 120)}),
        beat(NoteValue::Eighth, {harmonic(3, 2, HarmonicKind::Pinch, 70)}),
        beat(NoteValue::Eighth, {harmonic(3, 2, HarmonicKind::Tapped, 120)}),
        beat(NoteValue::Eighth, {harmonic(3, 2, HarmonicKind::Semi, 120)}),
    };
    const Sequence sequence(song);
    CHECK(keys(sequence) == std::vector<int>{59, 52, 64, 41, 69, 76, 69, 57});
}

TEST_CASE("Sequence: drum notes take accents and ghosting, but no touch or partial",
          "[audio][sequence][articulation]") {
    Song song;
    song.addTrack(Track("Drums", Instrument{.kind = InstrumentKind::Percussion}, {Staff{}}));
    song.appendMeasures(1);
    Note snare = note(38);
    snare.effects.ghost = true;
    snare.effects.deadNote = true;
    snare.effects.harmonic = {.kind = HarmonicKind::Artificial, .fretTenths = 120};
    voice(song, 0) = {beat(NoteValue::Whole, {snare})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 1);
    CHECK(sequence.notes()[0].key == 38);
    CHECK(sequence.notes()[0].velocity == 48);
    CHECK(sequence.notes()[0].touch == Touch::Normal);
}
