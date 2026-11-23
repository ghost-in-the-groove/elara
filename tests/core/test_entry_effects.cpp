// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace elara::core;

namespace {

const Duration kQuarter(NoteValue::Quarter);

/// An electric guitar in 4/4 over two measures. The first has a chord (fret 5
/// on string 2, fret 7 on string 3), fret 12 on string 1, then a rest; the
/// second starts with fret 6 on string 3.
Song guitarSong() {
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    Staff& staff = song.track(0).staff(0);
    staff.measure(0).voices[0].beats = {
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(2, 5), staff.frettedNote(3, 7)}},
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(1, 12)}},
        Beat{.duration = kQuarter}};
    staff.measure(1).voices[0].beats = {
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(3, 6)}}};
    return song;
}

/// A song of one track of `preset` whose first beat holds `notes`.
Song oneBeat(const char* preset, std::vector<Note> notes) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track::fromPreset(*findInstrumentPreset(preset)));
    song.track(0).staff(0).measure(0).voices[0].beats = {
        Beat{.duration = kQuarter, .notes = std::move(notes)}};
    return song;
}

Caret at(std::size_t measure, std::size_t beat, int string = 1) {
    return Caret{.measure = measure, .beat = beat, .string = string};
}

const std::vector<Note>& notesAt(const Song& song, std::size_t measure, std::size_t beat) {
    return song.track(0).staff(0).measure(measure).voices[0].beats.at(beat).notes;
}

const Note& onString(const Song& song, std::size_t measure, std::size_t beat, int string) {
    const auto& notes = notesAt(song, measure, beat);
    const auto found = std::ranges::find_if(notes, [string](const Note& note) {
        return note.position && note.position->string == string;
    });
    REQUIRE(found != notes.end());
    return *found;
}

Caret run(Document& doc, Entry entry) {
    REQUIRE(entry.command != nullptr);
    doc.execute(std::move(entry.command));
    return entry.caret;
}

} // namespace

TEST_CASE("Effects: a toggle on a fretted staff changes the caret's string only",
          "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret caret = at(0, 0, 3);
    CHECK(run(doc, toggleEffect(doc.song(), caret, NoteEffect::PalmMute)) == caret);
    CHECK(onString(doc.song(), 0, 0, 3).effects.palmMute);
    CHECK_FALSE(onString(doc.song(), 0, 0, 2).effects.palmMute);
    CHECK(doc.undoName() == "Change effect");

    // Again: off.
    run(doc, toggleEffect(doc.song(), caret, NoteEffect::PalmMute));
    CHECK_FALSE(onString(doc.song(), 0, 0, 3).effects.palmMute);

    // A string without a note, a rest and the free slot change nothing.
    CHECK(toggleEffect(doc.song(), at(0, 0, 6), NoteEffect::PalmMute).command == nullptr);
    CHECK(toggleEffect(doc.song(), at(0, 2, 1), NoteEffect::PalmMute).command == nullptr);
    CHECK(toggleEffect(doc.song(), at(0, 3, 1), NoteEffect::PalmMute).command == nullptr);
}

TEST_CASE("Effects: without strings every note of the beat, on when any lacks it",
          "[core][entry][effects]") {
    Note low{.pitch = Pitch::fromMidi(60)};
    Note high{.pitch = Pitch::fromMidi(64)};
    high.effects.staccato = true;
    Document doc(oneBeat("piano", {high, low}));
    run(doc, toggleEffect(doc.song(), at(0, 0), NoteEffect::Staccato));
    for (const Note& note : notesAt(doc.song(), 0, 0)) {
        CHECK(note.effects.staccato);
    }
    run(doc, toggleEffect(doc.song(), at(0, 0), NoteEffect::Staccato));
    for (const Note& note : notesAt(doc.song(), 0, 0)) {
        CHECK_FALSE(note.effects.staccato);
    }
}

TEST_CASE("Effects: each toggle sets the effect's usual form and clears it",
          "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret caret = at(0, 1, 1); // fret 12 on string 1
    const auto effects = [&] {
        return onString(doc.song(), 0, 1, 1).effects;
    };

    run(doc, toggleEffect(doc.song(), caret, NoteEffect::Bend));
    CHECK(effects().bend == BendCurve::bend(200)); // a full-step bend
    run(doc, toggleEffect(doc.song(), caret, NoteEffect::Slide));
    CHECK(effects().slideOut == SlideOut::Shift);
    run(doc, toggleEffect(doc.song(), caret, NoteEffect::Vibrato));
    CHECK(effects().vibrato == Vibrato::Slight);
    run(doc, toggleEffect(doc.song(), caret, NoteEffect::Harmonic));
    CHECK(effects().harmonic == Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 120});
    run(doc, toggleEffect(doc.song(), caret, NoteEffect::Accent));
    CHECK(effects().accent == Accent::Accent);
    for (const NoteEffect effect : {NoteEffect::Legato, NoteEffect::DeadNote, NoteEffect::LetRing,
                                    NoteEffect::Ghost, NoteEffect::Staccato}) {
        run(doc, toggleEffect(doc.song(), caret, effect));
    }
    const NoteEffects on = effects();
    CHECK(on.legatoFromPrevious);
    CHECK(on.deadNote);
    CHECK(on.letRing);
    CHECK(on.ghost);
    CHECK(on.staccato);

    for (const NoteEffect effect :
         {NoteEffect::Bend, NoteEffect::Slide, NoteEffect::Vibrato, NoteEffect::Harmonic,
          NoteEffect::Accent, NoteEffect::Legato, NoteEffect::DeadNote, NoteEffect::LetRing,
          NoteEffect::Ghost, NoteEffect::Staccato}) {
        run(doc, toggleEffect(doc.song(), caret, effect));
    }
    CHECK(effects() == NoteEffects{});

    // Any slide off the note counts as a slide, and goes.
    Song slid = guitarSong();
    slid.track(0).staff(0).measure(0).voices[0].beats[1].notes[0].effects.slideOut =
        SlideOut::Downwards;
    Document other(std::move(slid));
    run(other, toggleEffect(other.song(), caret, NoteEffect::Slide));
    CHECK(onString(other.song(), 0, 1, 1).effects.slideOut == SlideOut::None);
}

TEST_CASE("Effects: a natural harmonic needs strings and a node", "[core][entry][effects]") {
    const Song song = guitarSong();
    // Frets 5 and 7 are near nodes of the string; fret 6 is not.
    CHECK(toggleEffect(song, at(0, 0, 2), NoteEffect::Harmonic).command != nullptr);
    CHECK_THROWS_AS(toggleEffect(song, at(1, 0, 3), NoteEffect::Harmonic), std::out_of_range);
    const Song flute = oneBeat("flute", {Note{.pitch = Pitch::fromMidi(72)}});
    CHECK_THROWS_AS(toggleEffect(flute, at(0, 0), NoteEffect::Harmonic), std::logic_error);
}

TEST_CASE("Effects: a tie takes the previous note on the string", "[core][entry][effects]") {
    Song song = guitarSong();
    Staff& staff = song.track(0).staff(0);
    staff.measure(0).voices[0].beats.back() =
        Beat{.duration = kQuarter, .notes = {staff.frettedNote(3, 7)}};
    Document doc(std::move(song));

    // Fret 6 on string 3 after fret 7 on it, across the barline: tied, at fret 7.
    run(doc, toggleEffect(doc.song(), at(1, 0, 3), NoteEffect::Tie));
    const Note& tied = onString(doc.song(), 1, 0, 3);
    CHECK(tied.effects.tiedFromPrevious);
    CHECK(tied.position->fret == 7);
    CHECK(tied.pitch == staff.frettedNote(3, 7).pitch);
    // Off again keeps the fret it took.
    run(doc, toggleEffect(doc.song(), at(1, 0, 3), NoteEffect::Tie));
    CHECK_FALSE(onString(doc.song(), 1, 0, 3).effects.tiedFromPrevious);

    // Nothing to tie to: the first beat of the song, a string the previous beat
    // leaves silent.
    CHECK_THROWS_AS(toggleEffect(doc.song(), at(0, 0, 3), NoteEffect::Tie), std::logic_error);
    CHECK_THROWS_AS(toggleEffect(doc.song(), at(0, 1, 1), NoteEffect::Tie), std::logic_error);
}

TEST_CASE("Effects: a tie without strings takes the previous single note",
          "[core][entry][effects]") {
    Song song = oneBeat("flute", {Note{.pitch = Pitch::fromMidi(72)}});
    song.track(0).staff(0).measure(0).voices[0].beats.push_back(
        Beat{.duration = kQuarter, .notes = {Note{.pitch = Pitch::fromMidi(74)}}});
    Document doc(std::move(song));
    run(doc, toggleEffect(doc.song(), at(0, 1), NoteEffect::Tie));
    const Note& tied = notesAt(doc.song(), 0, 1).front();
    CHECK(tied.effects.tiedFromPrevious);
    CHECK(tied.pitch.midiNumber() == 72);
}

TEST_CASE("Effects: on a drum staff the lane's hit only", "[core][entry][effects]") {
    constexpr int kSnare = 38;
    constexpr int kBassDrum = 36;
    Document doc(oneBeat("drum-kit", {Note{.pitch = Pitch::fromMidi(kSnare)},
                                      Note{.pitch = Pitch::fromMidi(kBassDrum)}}));
    const auto pieces = drumPieces();
    const int snareLane =
        static_cast<int>(std::ranges::find(pieces, kSnare, &DrumPiece::key) - pieces.begin()) + 1;
    run(doc, toggleEffect(doc.song(), at(0, 0, snareLane), NoteEffect::Accent));
    for (const Note& note : notesAt(doc.song(), 0, 0)) {
        CHECK((note.effects.accent == Accent::Accent) == (note.pitch.midiNumber() == kSnare));
    }
}
