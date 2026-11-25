// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::Fraction;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::SoundingNote;
using elara::core::soundingNotes;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuning;
using elara::core::Tuplet;

namespace {

Song pianoSong(std::size_t measures) {
    Song song;
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano", .kind = InstrumentKind::Keyboard}, {Staff{}}));
    song.appendMeasures(measures);
    return song;
}

Note note(int midi, bool tied = false, Dynamic dynamic = Dynamic::MF) {
    Note result{.pitch = Pitch::fromMidi(midi)};
    result.effects.tiedFromPrevious = tied;
    result.effects.dynamic = dynamic;
    return result;
}

Beat beat(NoteValue value, std::vector<Note> notes = {}) {
    return Beat{.duration = Duration(value), .notes = std::move(notes)};
}

} // namespace

TEST_CASE("Sounding notes start where their beat starts and last their written length",
          "[core][performance]") {
    Song song = pianoSong(2);
    auto& first = song.track(0).staff(0).measure(0).voices[0].beats;
    first = {beat(NoteValue::Quarter, {note(60)}), beat(NoteValue::Quarter),
             beat(NoteValue::Half, {note(64), note(67, false, Dynamic::FF)})};
    song.track(0).staff(0).measure(1).voices[1].beats = {
        beat(NoteValue::Eighth, {note(48)}),
        Beat{.duration = Duration(NoteValue::Eighth, 0, Tuplet(3, 2)), .notes = {note(50)}}};

    // Each note also says where it was written: its measure, beat and note.
    const std::vector<SoundingNote> expected{
        {.start = Fraction(0), .length = Fraction(1, 4), .key = 60, .velocity = 80},
        {.start = Fraction(1, 2), .length = Fraction(1, 2), .key = 64, .velocity = 80, .beat = 2},
        {.start = Fraction(1, 2),
         .length = Fraction(1, 2),
         .key = 67,
         .velocity = 112,
         .beat = 2,
         .note = 1},
        {.voice = 1,
         .start = Fraction(1),
         .length = Fraction(1, 8),
         .key = 48,
         .velocity = 80,
         .measure = 1},
        {.voice = 1,
         .start = Fraction(9, 8),
         .length = Fraction(1, 12),
         .key = 50,
         .velocity = 80,
         .beat = 1,
         .measure = 1},
    };
    CHECK(soundingNotes(song) == expected);
}

TEST_CASE("A sounding note stays small", "[core][performance]") {
    // The sequencer streams hundreds of thousands of these: the address of
    // the written note must not make them larger.
    STATIC_CHECK(sizeof(SoundingNote) == 80);
}

TEST_CASE("Tied notes sound as one note, across measures and only on the same pitch",
          "[core][performance]") {
    Song song = pianoSong(2);
    auto& voice0 = song.track(0).staff(0).measure(0).voices[0].beats;
    voice0 = {beat(NoteValue::Half), beat(NoteValue::Half, {note(60), note(64)})};
    song.track(0).staff(0).measure(1).voices[0].beats = {
        beat(NoteValue::Quarter, {note(60, true), note(65, true)}), // 65 has nothing to join
        beat(NoteValue::Quarter, {note(60, true)}), beat(NoteValue::Quarter),
        beat(NoteValue::Quarter, {note(60, true)})}; // after a rest: a new note

    // A tied note keeps the place of its attack.
    const std::vector<SoundingNote> expected{
        {.start = Fraction(1, 2), .length = Fraction(1), .key = 60, .velocity = 80, .beat = 1},
        {.start = Fraction(1, 2),
         .length = Fraction(1, 2),
         .key = 64,
         .velocity = 80,
         .beat = 1,
         .note = 1},
        {.start = Fraction(1),
         .length = Fraction(1, 4),
         .key = 65,
         .velocity = 80,
         .note = 1,
         .measure = 1},
        {.start = Fraction(7, 4),
         .length = Fraction(1, 4),
         .key = 60,
         .velocity = 80,
         .beat = 3,
         .measure = 1},
    };
    CHECK(soundingNotes(song) == expected);
}

TEST_CASE("Ties on a fretted staff join notes on the same string", "[core][performance]") {
    Song song;
    song.addTrack(Track("Guitar", Instrument{.kind = InstrumentKind::Fretted},
                        {Staff(elara::core::Clef::Treble8vb, Tuning::preset("guitar"))}));
    song.appendMeasures(1);
    const Staff& staff = song.track(0).staff(0);
    Note open = staff.frettedNote(1, 0);    // E4 on string 1
    Note fretted = staff.frettedNote(2, 5); // E4 on string 2
    fretted.effects.tiedFromPrevious = true;
    song.track(0).staff(0).measure(0).voices[0].beats = {beat(NoteValue::Half, {open}),
                                                         beat(NoteValue::Half, {fretted})};
    const auto notes = soundingNotes(song);
    REQUIRE(notes.size() == 2); // a different string: struck again
    CHECK(notes[1].start == Fraction(1, 2));
}

TEST_CASE("Sounding notes are ordered by time, then track, staff, voice and key",
          "[core][performance]") {
    Song song = pianoSong(1);
    song.addTrack(Track("Bass", Instrument{.kind = InstrumentKind::Pitched}, {Staff{}}));
    song.track(1).staff(0).measure(0).voices[0].beats = {beat(NoteValue::Whole, {note(40)})};
    song.track(0).staff(0).measure(0).voices[1].beats = {beat(NoteValue::Whole, {note(55)})};
    song.track(0).staff(0).measure(0).voices[0].beats = {
        beat(NoteValue::Half, {note(72), note(60)}), beat(NoteValue::Half, {note(62)})};
    song.measureHeader(0).timeSignature = TimeSignature(4, 4);

    std::vector<std::pair<int, Fraction>> order;
    for (const SoundingNote& n : soundingNotes(song)) {
        order.emplace_back(n.key, n.start);
    }
    const std::vector<std::pair<int, Fraction>> expected{{60, Fraction(0)},
                                                         {72, Fraction(0)},
                                                         {55, Fraction(0)},
                                                         {40, Fraction(0)},
                                                         {62, Fraction(1, 2)}};
    CHECK(order == expected);
}

TEST_CASE("A song without tracks sounds nothing", "[core][performance]") {
    Song song;
    song.appendMeasures(3);
    CHECK(soundingNotes(song).empty());
}

TEST_CASE("A hairpin ramps the velocity of the notes under it", "[core][performance]") {
    using elara::core::Hairpin;
    Song song = pianoSong(3);
    Staff& staff = song.track(0).staff(0);
    // p: C <D E F | G (f): the crescendo leads from p (48) to f (96) over three
    // quarters; voice 2 holds a note struck under it.
    auto& rising = staff.measure(0).voices[0].beats;
    rising = {beat(NoteValue::Quarter, {note(60, false, Dynamic::P)}),
              beat(NoteValue::Quarter, {note(62, false, Dynamic::P)}),
              beat(NoteValue::Quarter, {note(64, false, Dynamic::P)}),
              beat(NoteValue::Quarter, {note(65, false, Dynamic::P)})};
    for (std::size_t b = 1; b < 4; ++b) {
        rising[b].effects.hairpin = Hairpin::Crescendo;
    }
    staff.measure(0).voices[1].beats = {beat(NoteValue::Half),
                                        beat(NoteValue::Half, {note(55, false, Dynamic::P)})};
    staff.measure(1).voices[0].beats = {beat(NoteValue::Whole, {note(67, false, Dynamic::F)})};
    // mf: >A B> C: no softer dynamic follows, so the diminuendo falls one step
    // (16) and the note after it is played as written.
    auto& falling = staff.measure(2).voices[0].beats;
    falling = {beat(NoteValue::Quarter, {note(69)}), beat(NoteValue::Quarter, {note(71)}),
               beat(NoteValue::Half, {note(72)})};
    falling[0].effects.hairpin = Hairpin::Diminuendo;
    falling[1].effects.hairpin = Hairpin::Diminuendo;

    std::vector<std::pair<int, int>> velocities; // key, velocity
    for (const SoundingNote& sounding : soundingNotes(song)) {
        velocities.emplace_back(sounding.key, sounding.velocity);
    }
    const std::vector<std::pair<int, int>> expected{
        {60, 48}, {62, 48}, {64, 64}, {55, 64}, {65, 80}, {67, 96}, {69, 80}, {71, 72}, {72, 80}};
    CHECK(velocities == expected);
}
