// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer: a song as it is played, in seconds (ADR-0017).
#include <elara/audio/sequence.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
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
using elara::core::BendCurve;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::Fermata;
using elara::core::Fraction;
using elara::core::HarmonicKind;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Ornament;
using elara::core::Pedal;
using elara::core::Pitch;
using elara::core::SlideIn;
using elara::core::SlideOut;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::TrackMix;
using elara::core::Trill;
using elara::core::Tuning;
using elara::core::Vibrato;

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

// An event as `kind time key value`: velocity for notes, cents and glide in
// milliseconds for pitch changes, so a whole list compares at once.
struct Event {
    EventKind kind = EventKind::NoteOn;
    double time = 0.0;
    int key = 0;
    int cents = 0;
    double glide = 0.0;
    friend bool operator==(const Event& a, const Event& b) {
        return a.kind == b.kind && std::abs(a.time - b.time) < 1e-6 && a.key == b.key &&
               a.cents == b.cents && std::abs(a.glide - b.glide) < 1e-6;
    }
};

} // namespace

// Failed comparisons print the events as `kind time key cents glide`.
template <>
struct Catch::StringMaker<Event> {
    static std::string convert(const Event& event) {
        constexpr std::array<const char*, 3> kKinds{"off", "on", "pitch"};
        return std::format("{} {:.4f} {} {} {:.4f}",
                           kKinds.at(static_cast<std::size_t>(event.kind)), event.time, event.key,
                           event.cents, event.glide);
    }
};

namespace {

Event on(double time, int key) {
    return {.kind = EventKind::NoteOn, .time = time, .key = key};
}

Event off(double time, int key) {
    return {.kind = EventKind::NoteOff, .time = time, .key = key};
}

Event pitch(double time, int key, int cents, double glide = 0.0) {
    return {.kind = EventKind::Pitch, .time = time, .key = key, .cents = cents, .glide = glide};
}

// A note joined to the voice struck as `key`, `cents` above it (ADR-0070).
Event joined(double time, int key, int cents) {
    return {.kind = EventKind::Legato, .time = time, .key = key, .cents = cents};
}

std::vector<Event> eventsOf(const Sequence& sequence) {
    std::vector<Event> result;
    for (const NoteEvent& event : sequence.events()) {
        result.push_back({.kind = event.kind,
                          .time = event.time,
                          .key = event.key,
                          .cents = event.cents,
                          .glide = static_cast<double>(event.glide)});
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

TEST_CASE("Sequence: the same when only the mix or the title changes", "[audio][sequence]") {
    Song song = pianoSong(2);
    voice(song, 0) = {beat(NoteValue::Whole, {note(60)})};
    const Sequence before(song);

    Song mixed = song;
    mixed.track(0).setMix(TrackMix{.volume = 0.3, .pan = -1.0, .mute = true});
    mixed.info().title = "Another title";
    CHECK(Sequence(mixed) == before);

    Song edited = song;
    voice(edited, 1) = {beat(NoteValue::Whole, {note(62)})};
    CHECK_FALSE(Sequence(edited) == before);
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

TEST_CASE("Sequence: a time in a measure the first time it is played", "[audio][sequence]") {
    // |: 0 :| 1, two seconds each: played 0 (0-2 s), 0 (2-4 s), 1 (4-6 s).
    Song song = pianoSong(2);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).repeatTimes = 2;
    const Sequence sequence(song);
    REQUIRE(sequence.secondsAt(0, 0.0).has_value());
    CHECK_THAT(*sequence.secondsAt(0, 0.0), WithinAbs(0.0, kEpsilon));
    CHECK_THAT(*sequence.secondsAt(0, 0.5), WithinAbs(1.0, kEpsilon));
    CHECK_THAT(*sequence.secondsAt(1, 0.25), WithinAbs(4.5, kEpsilon));
    CHECK_THAT(*sequence.secondsAt(1, 2.0), WithinAbs(6.0, kEpsilon)); // clamped to the measure
    CHECK_FALSE(sequence.secondsAt(2, 0.0).has_value());               // never played
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

TEST_CASE("Sequence: a gradual tempo change speeds the notes up on the way", "[audio][sequence]") {
    // ADR-0082: quarter = 60 to 120 over the second measure; its quarter
    // notes start at 4 ln(1 + x/4) seconds into it, x the quarters before.
    Song song = pianoSong(3);
    song.setStartTempo(Tempo(60.0));
    song.measureHeader(1).tempoChanges.push_back(
        TempoChange{.tempo = Tempo(60.0), .gradual = true});
    song.measureHeader(2).tempoChanges.push_back(TempoChange{.tempo = Tempo(120.0)});
    voice(song, 1) = {beat(NoteValue::Quarter, {note(60)}), beat(NoteValue::Quarter, {note(62)}),
                      beat(NoteValue::Quarter, {note(64)}), beat(NoteValue::Quarter, {note(65)})};
    const Sequence sequence(song);
    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const double x = static_cast<double>(i);
        CHECK_THAT(notes[i].start, WithinAbs(4.0 + 4.0 * std::log(1.0 + x / 4.0), kEpsilon));
    }
    CHECK_THAT(notes[3].end, WithinAbs(4.0 + 4.0 * std::log(2.0), kEpsilon));
    REQUIRE(sequence.measures().size() == 3);
    CHECK_THAT(sequence.measures()[2].start, WithinAbs(4.0 + 4.0 * std::log(2.0), kEpsilon));
    CHECK_THAT(sequence.duration(), WithinAbs(6.0 + 4.0 * std::log(2.0), kEpsilon));
    // Played at half speed, every time doubles.
    const Sequence slow(song, {.speed = 0.5});
    CHECK_THAT(slow.duration(), WithinAbs(2.0 * sequence.duration(), kEpsilon));
}

TEST_CASE("Sequence: a fermata holds its beat and what sounds under it", "[audio][sequence]") {
    // ADR-0085: at quarter = 60, a half note under a fermata lasts 4 s instead
    // of 2; the music after it comes 2 s later, also on the next pass of a
    // repeat, and the metronome slows with it; a count-in does not.
    Song song = pianoSong(2);
    song.setStartTempo(Tempo(60.0));
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).repeatTimes = 2;
    Beat held = beat(NoteValue::Half, {note(64)});
    held.effects.fermata = Fermata::Normal;
    voice(song, 0) = {beat(NoteValue::Quarter, {note(60)}), beat(NoteValue::Quarter, {note(62)}),
                      held};
    voice(song, 1) = {beat(NoteValue::Whole, {note(65)})};
    const Sequence sequence(song, {.metronome = true});
    const auto& notes = sequence.notes();
    REQUIRE(notes.size() == 7);
    CHECK_THAT(notes[2].start, WithinAbs(2.0, kEpsilon));
    CHECK_THAT(notes[2].end, WithinAbs(6.0, kEpsilon));
    CHECK_THAT(notes[3].start, WithinAbs(6.0, kEpsilon)); // the second pass
    CHECK_THAT(notes[5].end, WithinAbs(12.0, kEpsilon));
    CHECK_THAT(notes[6].start, WithinAbs(12.0, kEpsilon));
    CHECK_THAT(sequence.duration(), WithinAbs(16.0, kEpsilon));
    CHECK_THAT(sequence.countInAt(3.0).interval, WithinAbs(1.0, kEpsilon));
    // Clicks on the four beats: the last two 2 s apart.
    std::vector<double> clicks;
    for (const NoteEvent& event : sequence.events()) {
        if (event.kind == EventKind::NoteOn && event.track == sequence.metronomeTrack() &&
            event.time < 6.0) {
            clicks.push_back(event.time);
        }
    }
    REQUIRE(clicks.size() == 4);
    CHECK_THAT(clicks[2], WithinAbs(2.0, kEpsilon));
    CHECK_THAT(clicks[3], WithinAbs(4.0, kEpsilon));
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

TEST_CASE("Sequence: portato notes sound three quarters of their length, tenuto ones all of it",
          "[audio][sequence][articulation]") {
    // ADR-0088: a tenuto with a staccato is portato, detached but not short.
    Song song = pianoSong(1);
    Note portato = note(60);
    portato.effects.staccato = true;
    portato.effects.tenuto = true;
    Note held = note(62);
    held.effects.tenuto = true;
    voice(song, 0) = {beat(NoteValue::Half, {portato}), beat(NoteValue::Quarter, {held}),
                      beat(NoteValue::Quarter, {note(64)})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 3);
    CHECK_THAT(sequence.notes()[0].end, WithinAbs(0.75, kEpsilon));
    CHECK_THAT(sequence.notes()[1].start, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(sequence.notes()[1].end, WithinAbs(1.5, kEpsilon));
}

TEST_CASE("Sequence: staccatissimo notes sound a quarter of their length",
          "[audio][sequence][articulation]") {
    // ADR-0094: shorter than a staccato, with a tenuto too.
    Song song = pianoSong(1);
    Note short1 = note(60);
    short1.effects.staccatissimo = true;
    Note short2 = note(62);
    short2.effects.staccatissimo = true;
    short2.effects.tenuto = true;
    Note dotted = note(64);
    dotted.effects.staccato = true;
    voice(song, 0) = {beat(NoteValue::Half, {short1}), beat(NoteValue::Quarter, {short2}),
                      beat(NoteValue::Quarter, {dotted})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 3);
    CHECK_THAT(sequence.notes()[0].end, WithinAbs(0.25, kEpsilon));
    CHECK_THAT(sequence.notes()[1].end - sequence.notes()[1].start, WithinAbs(0.125, kEpsilon));
    CHECK_THAT(sequence.notes()[2].end - sequence.notes()[2].start, WithinAbs(0.25, kEpsilon));
}

TEST_CASE("Sequence: a trill alternates with its other note at its speed",
          "[audio][sequence][articulation]") {
    // ADR-0090: from the note itself, in sixteenths over a half note (1 s at
    // 120 BPM); the next note is untouched.
    Song song = pianoSong(1);
    Note trilled = note(64);
    trilled.effects.trill = Trill{.semitones = 1, .speed = NoteValue::Sixteenth};
    voice(song, 0) = {beat(NoteValue::Half, {trilled}), beat(NoteValue::Half, {note(60)})};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 9);
    for (std::size_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(sequence.notes()[i].key == (i % 2 == 0 ? 64 : 65));
        CHECK_THAT(sequence.notes()[i].start, WithinAbs(0.125 * static_cast<double>(i), kEpsilon));
        CHECK_THAT(sequence.notes()[i].end,
                   WithinAbs(0.125 * static_cast<double>(i + 1), kEpsilon));
    }
    CHECK(sequence.notes()[8].key == 60);
    CHECK_THAT(sequence.notes()[8].start, WithinAbs(1.0, kEpsilon));

    // Down to the note below, and at most one alternation for a short note.
    trilled.effects.trill = Trill{.semitones = -2, .speed = NoteValue::ThirtySecond};
    voice(song, 0) = {beat(NoteValue::Quarter, {trilled}), beat(NoteValue::SixtyFourth, {trilled})};
    const Sequence down(song);
    REQUIRE(down.notes().size() >= 9);
    CHECK(down.notes()[0].key == 64);
    CHECK(down.notes()[1].key == 62);
    CHECK(down.notes()[7].key == 62);
    CHECK(down.notes()[8].key == 64); // the sixty-fourth: too short to alternate
    CHECK(down.notes().size() == 9);
}

TEST_CASE("Sequence: mordents and turns start their notes with quick notes of the key",
          "[audio][sequence][articulation]") {
    // ADR-0091: at 120 BPM a thirty-second lasts 1/16 s. In C major E dips to
    // D for a mordent, goes up to F for an upper mordent; a turn on E plays F,
    // E, D, then E for the rest of the half note.
    Song song = pianoSong(1);
    const auto ornamented = [](int key, Ornament ornament) {
        Note result = note(key);
        result.effects.ornament = ornament;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Half, {ornamented(64, Ornament::Turn)}),
                      beat(NoteValue::Quarter, {ornamented(64, Ornament::Mordent)}),
                      beat(NoteValue::Quarter, {ornamented(64, Ornament::UpperMordent)})};
    const Sequence sequence(song);
    std::vector<int> keys;
    for (const auto& played : sequence.notes()) {
        keys.push_back(played.key);
    }
    CHECK(keys == std::vector<int>{65, 64, 62, 64, 64, 62, 64, 64, 65, 64});
    constexpr double kQuick = 1.0 / 16.0;
    for (std::size_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK_THAT(sequence.notes()[i].start, WithinAbs(kQuick * static_cast<double>(i), kEpsilon));
        CHECK_THAT(sequence.notes()[i].end,
                   WithinAbs(kQuick * static_cast<double>(i + 1), kEpsilon));
    }
    CHECK_THAT(sequence.notes()[3].start, WithinAbs(3 * kQuick, kEpsilon));
    CHECK_THAT(sequence.notes()[3].end, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(sequence.notes()[4].start, WithinAbs(1.0, kEpsilon));
    CHECK_THAT(sequence.notes()[6].start, WithinAbs(1.0 + 2 * kQuick, kEpsilon));

    // A sixteenth shares itself out: an inverted turn in four equal notes.
    voice(song, 0) = {beat(NoteValue::Sixteenth, {ornamented(64, Ornament::InvertedTurn)})};
    const Sequence shortNote(song);
    REQUIRE(shortNote.notes().size() == 4);
    CHECK(shortNote.notes()[0].key == 62);
    CHECK(shortNote.notes()[2].key == 65);
    CHECK_THAT(shortNote.notes()[3].start, WithinAbs(3 * 0.125 / 4, kEpsilon));
    CHECK_THAT(shortNote.notes()[3].end, WithinAbs(0.125, kEpsilon));
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

TEST_CASE("Sequence: the sustain pedal holds the notes of its track until it comes up",
          "[audio][sequence][articulation]") {
    // ADR-0092: two seconds a measure. The right hand's pedal goes down on C4,
    // changes on E4 at 1 s and comes up at 3 s. The left hand's A3, held over
    // the change, rings on to 3 s with E4; C4 and D4 stop at the change. F4,
    // struck as the pedal comes up, and the strings' B4 sound as written.
    Song song;
    song.addTrack(Track("Piano", Instrument{.name = "Piano", .kind = InstrumentKind::Keyboard},
                        {Staff{}, Staff{}}));
    song.addTrack(Track("Strings", Instrument{.name = "Strings", .kind = InstrumentKind::Pitched},
                        {Staff{}}));
    song.appendMeasures(2);
    const auto pedalled = [](NoteValue value, std::vector<Note> notes, Pedal pedal) {
        Beat result = beat(value, std::move(notes));
        result.effects.pedal = pedal;
        return result;
    };
    Track& piano = song.track(0);
    piano.staff(0).measure(0).voices[0].beats = {
        pedalled(NoteValue::Quarter, {note(60)}, Pedal::Down),
        pedalled(NoteValue::Quarter, {note(62)}, Pedal::Down),
        pedalled(NoteValue::Half, {note(64)}, Pedal::Change)};
    piano.staff(1).measure(0).voices[0].beats = {
        beat(NoteValue::Quarter), beat(NoteValue::Half, {note(57)}), beat(NoteValue::Quarter)};
    piano.staff(0).measure(1).voices[0].beats = {pedalled(NoteValue::Half, {}, Pedal::Down),
                                                 beat(NoteValue::Half, {note(65)})};
    song.track(1).staff(0).measure(0).voices[0].beats = {
        beat(NoteValue::Quarter, {note(71)}), beat(NoteValue::Quarter), beat(NoteValue::Half)};
    const Sequence sequence(song);
    REQUIRE(sequence.notes().size() == 6);
    const auto endOf = [&](int key) {
        const auto found = std::ranges::find(sequence.notes(), key, &PlayedNote::key);
        REQUIRE(found != sequence.notes().end());
        return found->end;
    };
    CHECK_THAT(endOf(60), WithinAbs(1.0, kEpsilon));
    CHECK_THAT(endOf(62), WithinAbs(1.0, kEpsilon));
    CHECK_THAT(endOf(64), WithinAbs(3.0, kEpsilon));
    CHECK_THAT(endOf(57), WithinAbs(3.0, kEpsilon));
    CHECK_THAT(endOf(65), WithinAbs(4.0, kEpsilon));
    CHECK_THAT(endOf(71), WithinAbs(0.5, kEpsilon));

    // |: 0 :| 1: the pedal comes up where the repeat goes back and goes down
    // again with the measure; the second time it goes on into measure 1.
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).repeatTimes = 2;
    const Sequence repeated(song);
    REQUIRE(repeated.notes().size() == 11);
    std::vector<double> ends;
    for (const PlayedNote& played : repeated.notes()) {
        if (played.key == 64) {
            ends.push_back(played.end);
        }
    }
    REQUIRE(ends.size() == 2);
    CHECK_THAT(ends[0], WithinAbs(2.0, kEpsilon));
    CHECK_THAT(ends[1], WithinAbs(5.0, kEpsilon));
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

// Pitch articulations (ADR-0023).
TEST_CASE("Sequence: hammer-ons and pull-offs sound on the voice of the note before them",
          "[audio][sequence][pitch]") {
    Song song = guitarSong(2);
    const auto legato = [&](int string, int fret) {
        Note result = fretted(song, string, fret);
        result.effects.legatoFromPrevious = true;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Quarter, {fretted(song, 3, 2)}), // A3
                      beat(NoteValue::Quarter, {legato(3, 4)}),        // hammer-on to B3
                      beat(NoteValue::Quarter, {legato(3, 2)}),        // pull-off to A3
                      beat(NoteValue::Quarter, {fretted(song, 3, 2)})};
    // After a rest, or on a string that has not sounded, legato strikes.
    voice(song, 1) = {beat(NoteValue::Quarter), beat(NoteValue::Quarter, {legato(3, 4)}),
                      beat(NoteValue::Quarter, {legato(2, 1)}), beat(NoteValue::Quarter)};
    const Sequence sequence(song);
    std::vector<bool> struck;
    for (const PlayedNote& note : sequence.notes()) {
        struck.push_back(note.struck);
    }
    CHECK(struck == std::vector<bool>{true, false, false, true, true, true});
    // The joined notes are Legato events on the first note's key, at the
    // offset of their own pitch, with their own velocity.
    CHECK(eventsOf(sequence) == std::vector<Event>{on(0.0, 57), joined(0.5, 57, 200),
                                                   joined(1.0, 57, 0), off(1.5, 57), on(1.5, 57),
                                                   off(2.0, 57), on(2.5, 59), off(3.0, 59),
                                                   on(3.0, 60), off(3.5, 60)});
    for (const NoteEvent& event : sequence.events()) {
        if (event.kind == EventKind::Legato) {
            const auto note = std::ranges::find(sequence.notes(), event.time, &PlayedNote::start);
            REQUIRE(note != sequence.notes().end());
            CHECK(event.velocity == note->velocity);
        }
    }
}

TEST_CASE("Sequence: a bend follows its curve over the note", "[audio][sequence][pitch]") {
    Song song = guitarSong(1);
    const auto bent = [&](BendCurve curve) {
        Note result = fretted(song, 3, 2); // A3
        result.effects.bend = std::move(curve);
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Half, {bent(BendCurve({{.position = 0, .cents = 0},
                                                             {.position = 50, .cents = 200},
                                                             {.position = 100, .cents = 200}}))}),
                      beat(NoteValue::Quarter, {bent(BendCurve::preBendRelease(100))}),
                      beat(NoteValue::Quarter, {fretted(song, 3, 2)})};
    // The whammy bar dips every note of its beat.
    voice(song, 0)[2].effects.whammyBar = BendCurve({{.position = 0, .cents = 0},
                                                     {.position = 50, .cents = -200},
                                                     {.position = 100, .cents = 0}});
    const Sequence sequence(song);
    // Up a whole tone over half the note and held; bent a semitone before the
    // attack, held half the note and released; dipped and back.
    CHECK(eventsOf(sequence) == std::vector<Event>{on(0.0, 57), pitch(0.0, 57, 200, 0.5),
                                                   off(1.0, 57), on(1.0, 57), pitch(1.0, 57, 100),
                                                   pitch(1.25, 57, 0, 0.25), off(1.5, 57),
                                                   on(1.5, 57), pitch(1.5, 57, -200, 0.25),
                                                   pitch(1.75, 57, 0, 0.25), off(2.0, 57)});
}

TEST_CASE("Sequence: slides glide into, between and off notes", "[audio][sequence][pitch]") {
    Song song = guitarSong(2);
    const auto sliding = [&](int fret, SlideIn in, SlideOut out) {
        Note result = fretted(song, 3, fret);
        result.effects.slideIn = in;
        result.effects.slideOut = out;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Quarter, {sliding(2, SlideIn::FromBelow, SlideOut::None)}),
                      beat(NoteValue::Quarter, {sliding(2, SlideIn::None, SlideOut::Shift)}),
                      beat(NoteValue::Quarter, {sliding(6, SlideIn::None, SlideOut::Downwards)}),
                      beat(NoteValue::Quarter)};
    voice(song, 1) = {beat(NoteValue::Quarter, {sliding(2, SlideIn::None, SlideOut::Legato)}),
                      beat(NoteValue::Quarter, {sliding(6, SlideIn::None, SlideOut::None)}),
                      beat(NoteValue::Half)};
    const Sequence sequence(song);
    // Into a note from five frets below in 0.1 s; a shift slide over the last
    // 0.2 s to the next note, which is struck; off the string downwards; a
    // legato slide to a note that is not struck again.
    CHECK(eventsOf(sequence) ==
          std::vector<Event>{on(0.0, 57), pitch(0.0, 57, -500), pitch(0.0, 57, 0, 0.1),
                             off(0.5, 57), on(0.5, 57), pitch(0.8, 57, 400, 0.2), off(1.0, 57),
                             on(1.0, 61), pitch(1.3, 61, -500, 0.2), off(1.5, 61), on(2.0, 57),
                             pitch(2.3, 57, 400, 0.2), joined(2.5, 57, 400), off(3.0, 57)});
}

TEST_CASE("Sequence: vibrato swings the pitch either side of the note",
          "[audio][sequence][pitch]") {
    Song song = guitarSong(1);
    const auto shaking = [&](Vibrato vibrato) {
        Note result = fretted(song, 3, 2);
        result.effects.vibrato = vibrato;
        return result;
    };
    voice(song, 0) = {beat(NoteValue::Half, {shaking(Vibrato::Slight)}),
                      beat(NoteValue::Half, {shaking(Vibrato::Wide)})};
    const Sequence sequence(song);
    std::vector<int> slight;
    std::vector<int> wide;
    for (const NoteEvent& event : sequence.events()) {
        if (event.kind == EventKind::Pitch) {
            CHECK(event.glide <= 1.0f / 22.0f + 1e-6f); // a quarter of a 5.5 Hz cycle
            (event.time < 1.0 ? slight : wide).push_back(event.cents);
        }
    }
    REQUIRE(slight.size() >= 8);
    REQUIRE(wide.size() >= 8);
    CHECK(std::vector<int>(slight.begin(), slight.begin() + 4) == std::vector<int>{20, 0, -20, 0});
    CHECK(std::vector<int>(wide.begin(), wide.begin() + 4) == std::vector<int>{50, 0, -50, 0});
}

namespace {

// A click of the metronome: when it is struck and on which block.
struct Click {
    double time = 0.0;
    int key = 0;
    friend bool operator==(const Click& a, const Click& b) {
        return std::abs(a.time - b.time) < 1e-6 && a.key == b.key;
    }
};

std::vector<Click> clicksOf(const Sequence& sequence) {
    std::vector<Click> result;
    for (const NoteEvent& event : sequence.events()) {
        if (event.track == sequence.metronomeTrack() && event.kind == EventKind::NoteOn) {
            result.push_back({.time = event.time, .key = event.key});
        }
    }
    return result;
}

constexpr int kHigh = elara::audio::kMetronomeAccentKey;
constexpr int kLow = elara::audio::kMetronomeKey;

} // namespace

TEST_CASE("Sequence: a slower or faster speed scales every tempo", "[audio][sequence][practice]") {
    Song song = pianoSong(2);
    voice(song, 0) = {beat(NoteValue::Half, {note(60)}), beat(NoteValue::Half, {note(62)})};
    voice(song, 1) = {beat(NoteValue::Whole, {note(64)})};
    const Sequence half(song, {.speed = 0.5});

    CHECK(half.speed() == 0.5);
    CHECK_THAT(half.measures()[1].start, WithinAbs(4.0, kEpsilon));
    CHECK_THAT(half.duration(), WithinAbs(8.0, kEpsilon));
    CHECK(eventsOf(half) == std::vector<Event>{on(0.0, 60), off(2.0, 60), on(2.0, 62), off(4.0, 62),
                                               on(4.0, 64), off(8.0, 64)});
    CHECK_THAT(Sequence(song, {.speed = 2.0}).duration(), WithinAbs(2.0, kEpsilon));
    CHECK(Sequence(song).speed() == 1.0);

    SECTION("from a quarter of the tempo to twice it") {
        CHECK(Sequence(song, {.speed = 0.1}).speed() == elara::audio::kMinSpeed);
        CHECK_THAT(Sequence(song, {.speed = 0.1}).duration(), WithinAbs(16.0, kEpsilon));
        CHECK(Sequence(song, {.speed = 5.0}).speed() == elara::audio::kMaxSpeed);
        CHECK(Sequence(song, {.speed = std::nan("")}).speed() == 1.0);
    }
    SECTION("effects timed in seconds keep their pace") {
        // A vibrato cycle lasts 1 / 5.5 s at any speed.
        Song guitar = guitarSong(1);
        Note shaking = fretted(guitar, 3, 2);
        shaking.effects.vibrato = Vibrato::Wide;
        voice(guitar, 0) = {beat(NoteValue::Whole, {shaking})};
        const Sequence slow(guitar, {.speed = 0.5});
        const auto first = std::ranges::find(slow.events(), EventKind::Pitch, &NoteEvent::kind);
        REQUIRE(first != slow.events().end());
        CHECK_THAT(first->glide, WithinAbs(1.0 / 22.0, 1e-6));
        CHECK(first->cents == 50);
    }
}

TEST_CASE("Sequence: the metronome clicks every beat, higher on the first of a measure",
          "[audio][sequence][practice]") {
    // 4/4, then 3/4, at quarter = 120.
    Song song = pianoSong(2);
    song.measureHeader(1).timeSignature = TimeSignature(3, 4);
    voice(song, 0) = {beat(NoteValue::Whole, {note(60)})};
    const Sequence plain(song);
    const Sequence clicking(song, {.metronome = true});

    CHECK(clicking.metronomeTrack() == 1);
    CHECK(clicksOf(plain).empty());
    CHECK(clicksOf(clicking) == std::vector<Click>{{0.0, kHigh},
                                                   {0.5, kLow},
                                                   {1.0, kLow},
                                                   {1.5, kLow},
                                                   {2.0, kHigh},
                                                   {2.5, kLow},
                                                   {3.0, kLow}});
    CHECK_THAT(clicking.duration(), WithinAbs(3.5, kEpsilon));
    // The song plays as before, and every click is released before the next.
    CHECK(clicking.notes() == plain.notes());
    std::vector<NoteEvent> songEvents;
    std::vector<Click> releases;
    for (const NoteEvent& event : clicking.events()) {
        if (event.track != clicking.metronomeTrack()) {
            songEvents.push_back(event);
        } else if (event.kind == EventKind::NoteOff) {
            releases.push_back({.time = event.time, .key = event.key});
        }
    }
    CHECK(songEvents == plain.events());
    REQUIRE(releases.size() == 7);
    CHECK(releases[0].key == kHigh);
    CHECK(releases[0].time > 0.0);
    CHECK(releases[0].time <= 0.5);
    CHECK(releases[6].time > 3.0);

    SECTION("a compound meter clicks its dotted beats") {
        Song compound = pianoSong(1);
        compound.measureHeader(0).timeSignature = TimeSignature(6, 8);
        CHECK(clicksOf(Sequence(compound, {.metronome = true})) ==
              std::vector<Click>{{0.0, kHigh}, {0.75, kLow}});
    }
    SECTION("clicks follow tempo changes inside a measure") {
        Song changing = pianoSong(1);
        changing.measureHeader(0).tempoChanges.push_back(
            TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(60.0)});
        CHECK(clicksOf(Sequence(changing, {.metronome = true})) ==
              std::vector<Click>{{0.0, kHigh}, {0.5, kLow}, {1.0, kLow}, {2.0, kLow}});
    }
    SECTION("every played measure clicks, at the speed asked for") {
        Song repeated = pianoSong(1);
        repeated.measureHeader(0).repeatStart = true;
        repeated.measureHeader(0).repeatTimes = 2;
        const std::vector<Click> clicks =
            clicksOf(Sequence(repeated, {.speed = 0.5, .metronome = true}));
        REQUIRE(clicks.size() == 8);
        CHECK(clicks[1] == Click{1.0, kLow});
        CHECK(clicks[4] == Click{4.0, kHigh});
    }
}

TEST_CASE("Sequence: a count-in is a measure of beats at the tempo where playback starts",
          "[audio][sequence][practice]") {
    // 4/4 (0 to 2 s), 6/8 (2 to 3.5 s), then 4/4 slowing to quarter = 60
    // halfway (3.5 to 4.5 s, then to 6.5 s).
    Song song = pianoSong(3);
    song.measureHeader(1).timeSignature = TimeSignature(6, 8);
    song.measureHeader(2).tempoChanges.push_back(
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(60.0)});
    const Sequence sequence(song);
    const auto countIn = [](const Sequence& played, double seconds) {
        const elara::audio::CountIn result = played.countInAt(seconds);
        return std::pair(result.clicks, result.interval);
    };
    using Expected = std::pair<int, double>;

    CHECK(countIn(sequence, 0.0) == Expected{4, 0.5});
    CHECK(countIn(sequence, 1.9) == Expected{4, 0.5});
    CHECK(countIn(sequence, 2.5) == Expected{2, 0.75}); // dotted quarters
    CHECK(countIn(sequence, 4.0) == Expected{4, 0.5});
    CHECK(countIn(sequence, 5.0) == Expected{4, 1.0}); // after the tempo change
    CHECK(countIn(sequence, -1.0) == Expected{4, 0.5});
    CHECK(countIn(sequence, 99.0) == Expected{4, 1.0});
    CHECK(countIn(Sequence(song, {.speed = 0.5}), 0.0) == Expected{4, 1.0});
    CHECK(countIn(Sequence(Song{}), 0.0) == Expected{0, 0.0});
}

TEST_CASE("Sequence: playback keeps its place in the music when the timing changes",
          "[audio][sequence][practice]") {
    // Two seconds per measure at full speed, four at half speed.
    Song song = pianoSong(3);
    const Sequence full(song);
    const Sequence half(song, {.speed = 0.5});

    SECTION("another speed: the same point of the same measure") {
        CHECK_THAT(elara::audio::carriedPosition(full, 3.0, half), WithinAbs(6.0, kEpsilon));
        CHECK_THAT(elara::audio::carriedPosition(half, 6.0, full), WithinAbs(3.0, kEpsilon));
        CHECK_THAT(elara::audio::carriedPosition(full, 0.0, half), WithinAbs(0.0, kEpsilon));
        CHECK_THAT(elara::audio::carriedPosition(full, 4.0, half), WithinAbs(8.0, kEpsilon));
    }
    SECTION("the same timing: exactly the same time") {
        Song edited = song;
        voice(edited, 1) = {beat(NoteValue::Whole, {note(64)})};
        CHECK(elara::audio::carriedPosition(full, 2.7, Sequence(edited)) == 2.7);
    }
    SECTION("a tempo edit: the same point of the same measure") {
        Song faster = song;
        faster.measureHeader(1).tempoChanges.push_back(TempoChange{.tempo = Tempo(240.0)});
        const Sequence edited(faster);
        CHECK_THAT(elara::audio::carriedPosition(full, 3.0, edited), WithinAbs(2.5, kEpsilon));
    }
    SECTION("at or past the end: the end") {
        CHECK_THAT(elara::audio::carriedPosition(full, 6.0, half), WithinAbs(12.0, kEpsilon));
        CHECK_THAT(elara::audio::carriedPosition(full, 99.0, half), WithinAbs(12.0, kEpsilon));
    }
    SECTION("other measures: the same time, inside the new song") {
        Song longer = song;
        longer.appendMeasures(1);
        CHECK_THAT(elara::audio::carriedPosition(full, 3.0, Sequence(longer)),
                   WithinAbs(3.0, kEpsilon));
        Song shorter = pianoSong(1);
        CHECK_THAT(elara::audio::carriedPosition(full, 5.0, Sequence(shorter)),
                   WithinAbs(2.0, kEpsilon));
    }
}

namespace {

// Each note as (key, start), in the order notes() lists them.
std::vector<std::pair<int, double>> attacks(const Sequence& sequence) {
    std::vector<std::pair<int, double>> result;
    for (const PlayedNote& played : sequence.notes()) {
        result.emplace_back(played.key, std::round(played.start * 1e6) / 1e6);
    }
    return result;
}

} // namespace

// Strums and tremolo picking (ADR-0025).
TEST_CASE("Sequence: a strum strikes a chord string by string", "[audio][sequence][articulation]") {
    Song song = guitarSong(1);
    const auto chord = [&] { // E2, B2 and E3 on strings 6, 5 and 4
        return std::vector<Note>{fretted(song, 6, 0), fretted(song, 4, 2), fretted(song, 5, 2)};
    };
    voice(song, 0) = {beat(NoteValue::Quarter, chord()), beat(NoteValue::Quarter, chord()),
                      beat(NoteValue::Half, chord())};
    voice(song, 0)[0].effects.strum = elara::core::Strum::Down;
    voice(song, 0)[1].effects.strum = elara::core::Strum::Up;
    const Sequence sequence(song);

    // Down from the lowest string, up from the highest, 10 ms apart; listed
    // by start. A chord without a strum is struck at once.
    using Attack = std::pair<int, double>;
    CHECK(attacks(sequence) == std::vector<Attack>{{40, 0.0},
                                                   {47, 0.01},
                                                   {52, 0.02},
                                                   {52, 0.5},
                                                   {47, 0.51},
                                                   {40, 0.52},
                                                   {40, 1.0},
                                                   {47, 1.0},
                                                   {52, 1.0}});
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK_THAT(sequence.notes()[i].end, WithinAbs(i < 3 ? 0.5 : 1.0, kEpsilon)); // ends kept
    }

    SECTION("within half of a short chord") {
        Song fast = guitarSong(1);
        std::vector<Note> six;
        for (int string = 6; string >= 1; --string) {
            six.push_back(fretted(fast, string, 0));
        }
        voice(fast, 0) = {beat(NoteValue::ThirtySecond, six)}; // 1/16 s
        voice(fast, 0)[0].effects.strum = elara::core::Strum::Down;
        std::vector<double> starts;
        const Sequence strummed(fast);
        for (const PlayedNote& played : strummed.notes()) {
            starts.push_back(played.start);
        }
        REQUIRE(starts.size() == 6);
        CHECK_THAT(starts[3], WithinAbs(0.03, kEpsilon));
        CHECK_THAT(starts[5], WithinAbs(1.0 / 32.0, kEpsilon)); // half of the note
    }
}

TEST_CASE("Sequence: tremolo picking strikes a note again and again at its speed",
          "[audio][sequence][articulation]") {
    Song song = guitarSong(1);
    voice(song, 0) = {beat(NoteValue::Half, {fretted(song, 3, 2)}),
                      beat(NoteValue::Half, {fretted(song, 3, 4)})};
    voice(song, 0)[0].effects.tremoloPicking = NoteValue::Sixteenth;
    const Sequence sequence(song);

    std::vector<Event> expected;
    for (int i = 0; i < 8; ++i) { // sixteenths over the half note
        if (i > 0) {
            expected.push_back(off(0.125 * i, 57));
        }
        expected.push_back(on(0.125 * i, 57));
    }
    expected.push_back(off(1.0, 57));
    expected.push_back(on(1.0, 59));
    expected.push_back(off(2.0, 59));
    CHECK(eventsOf(sequence) == expected);
    REQUIRE(sequence.notes().size() == 9);
    CHECK_THAT(sequence.notes()[7].start, WithinAbs(0.875, kEpsilon));
    CHECK_THAT(sequence.notes()[7].end, WithinAbs(1.0, kEpsilon));

    SECTION("the pitch moves over the whole note, each attack taking its part") {
        // At 100 BPM the half note lasts 1.2 s, struck every 0.15 s. A slide
        // off its end spans the last 0.2 s: the end of the seventh attack and
        // all of the eighth, which starts where the seventh got to.
        song.measureHeader(0).tempoChanges.push_back(TempoChange{.tempo = Tempo(100.0)});
        voice(song, 0)[0].notes[0].effects.slideOut = SlideOut::Downwards;
        std::vector<Event> pitches;
        for (const Event& event : eventsOf(Sequence(song))) {
            if (event.kind == EventKind::Pitch) {
                pitches.push_back(event);
            }
        }
        CHECK(pitches == std::vector<Event>{pitch(1.0, 57, -125, 0.05), pitch(1.05, 57, -125),
                                            pitch(1.05, 57, -500, 0.15)});
    }
    SECTION("a hammer-on into the note joins its first attack only") {
        voice(song, 0)[0].notes[0].effects.legatoFromPrevious = true;
        voice(song, 0).insert(voice(song, 0).begin(),
                              beat(NoteValue::Quarter, {fretted(song, 3, 0)}));
        voice(song, 0).back() = beat(NoteValue::Quarter);
        std::vector<bool> struck;
        const Sequence picked(song);
        for (const PlayedNote& played : picked.notes()) {
            struck.push_back(played.struck);
        }
        CHECK(struck == std::vector<bool>{true, false, true, true, true, true, true, true, true});
    }
    SECTION("a drum roll") {
        Song drums;
        drums.addTrack(Track("Drums", Instrument{.kind = InstrumentKind::Percussion}, {Staff{}}));
        drums.appendMeasures(1);
        voice(drums, 0) = {beat(NoteValue::Quarter, {note(38)}), beat(NoteValue::Quarter),
                           beat(NoteValue::Half)};
        voice(drums, 0)[0].effects.tremoloPicking = NoteValue::ThirtySecond;
        CHECK(Sequence(drums).notes().size() == 8);
    }
}
