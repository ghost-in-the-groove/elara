// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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
                                    NoteEffect::Ghost, NoteEffect::Staccato, NoteEffect::Tenuto}) {
        run(doc, toggleEffect(doc.song(), caret, effect));
    }
    const NoteEffects on = effects();
    CHECK(on.legatoFromPrevious);
    CHECK(on.deadNote);
    CHECK(on.letRing);
    CHECK(on.ghost);
    CHECK(on.staccato);
    CHECK(on.tenuto); // with the staccato: portato (ADR-0088)

    for (const NoteEffect effect :
         {NoteEffect::Bend, NoteEffect::Slide, NoteEffect::Vibrato, NoteEffect::Harmonic,
          NoteEffect::Accent, NoteEffect::Legato, NoteEffect::DeadNote, NoteEffect::LetRing,
          NoteEffect::Ghost, NoteEffect::Staccato, NoteEffect::Tenuto}) {
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

TEST_CASE("Effects: techniques of strings go on fretted staves only", "[core][entry][effects]") {
    constexpr std::array kOfStrings{NoteEffect::Legato, NoteEffect::PalmMute, NoteEffect::DeadNote,
                                    NoteEffect::Harmonic};
    for (int e = 0; e <= static_cast<int>(NoteEffect::Tenuto); ++e) {
        const auto effect = static_cast<NoteEffect>(e);
        CHECK(needsStrings(effect) == (std::ranges::find(kOfStrings, effect) != kOfStrings.end()));
    }

    // On a piano they do not go on; the others do.
    Note held{.pitch = Pitch::fromMidi(60)};
    held.effects.palmMute = true; // as a file may have it
    Document doc(oneBeat("piano", {held}));
    for (const NoteEffect effect :
         {NoteEffect::Legato, NoteEffect::DeadNote, NoteEffect::Harmonic}) {
        CHECK_THROWS_AS(toggleEffect(doc.song(), at(0, 0), effect), std::logic_error);
    }
    run(doc, toggleEffect(doc.song(), at(0, 0), NoteEffect::Bend));
    CHECK_FALSE(notesAt(doc.song(), 0, 0).front().effects.bend.isEmpty());
    // One already there can still be taken off.
    run(doc, toggleEffect(doc.song(), at(0, 0), NoteEffect::PalmMute));
    CHECK_FALSE(notesAt(doc.song(), 0, 0).front().effects.palmMute);
    CHECK_THROWS_AS(toggleEffect(doc.song(), at(0, 0), NoteEffect::PalmMute), std::logic_error);
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

TEST_CASE("Effects: the caret has an effect when all its notes have it", "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret string3 = at(0, 0, 3);
    CHECK_FALSE(hasEffectAtCaret(doc.song(), string3, NoteEffect::PalmMute));
    run(doc, toggleEffect(doc.song(), string3, NoteEffect::PalmMute));
    CHECK(hasEffectAtCaret(doc.song(), string3, NoteEffect::PalmMute));
    CHECK_FALSE(hasEffectAtCaret(doc.song(), at(0, 0, 2), NoteEffect::PalmMute));
    CHECK_FALSE(hasEffectAtCaret(doc.song(), string3, NoteEffect::LetRing));
    // A rest, a string without a note and the free slot have no effects.
    CHECK_FALSE(hasEffectAtCaret(doc.song(), at(0, 2, 3), NoteEffect::PalmMute));
    CHECK_FALSE(hasEffectAtCaret(doc.song(), at(0, 1, 3), NoteEffect::PalmMute));
    CHECK_FALSE(hasEffectAtCaret(doc.song(), at(1, 1, 3), NoteEffect::PalmMute));

    // Without strings, every note of the beat must have it.
    Document piano(
        oneBeat("piano", {Note{.pitch = Pitch::fromMidi(60)}, Note{.pitch = Pitch::fromMidi(64)}}));
    const Caret caret = at(0, 0);
    run(piano, toggleEffect(piano.song(), caret, NoteEffect::Staccato));
    CHECK(hasEffectAtCaret(piano.song(), caret, NoteEffect::Staccato));
    Song half = piano.song();
    half.track(0).staff(0).measure(0).voices[0].beats[0].notes[1].effects.staccato = false;
    CHECK_FALSE(hasEffectAtCaret(half, caret, NoteEffect::Staccato));
}

TEST_CASE("Effects: a bend of any form on the caret's notes", "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret caret = at(0, 1, 1); // fret 12 on string 1
    CHECK(bendAtCaret(doc.song(), caret).isEmpty());

    CHECK(run(doc, setBend(doc.song(), caret, BendCurve::bendRelease(100))) == caret);
    CHECK(doc.undoName() == "Change bend");
    CHECK(onString(doc.song(), 0, 1, 1).effects.bend == BendCurve::bendRelease(100));
    CHECK(bendAtCaret(doc.song(), caret) == BendCurve::bendRelease(100));
    CHECK(hasEffectAtCaret(doc.song(), caret, NoteEffect::Bend));

    // The same bend again changes nothing; another replaces it, and none removes it.
    CHECK(setBend(doc.song(), caret, BendCurve::bendRelease(100)).command == nullptr);
    run(doc, setBend(doc.song(), caret, BendCurve::preBend(300)));
    CHECK(bendAtCaret(doc.song(), caret) == BendCurve::preBend(300));
    run(doc, setBend(doc.song(), caret, BendCurve{}));
    CHECK(onString(doc.song(), 0, 1, 1).effects == NoteEffects{});
    doc.undo();
    CHECK(bendAtCaret(doc.song(), caret) == BendCurve::preBend(300));

    // On a fretted staff only the caret's string; nothing on a rest, a string
    // without a note or the free slot.
    run(doc, setBend(doc.song(), at(0, 0, 3), BendCurve::bend(50)));
    CHECK(onString(doc.song(), 0, 0, 3).effects.bend == BendCurve::bend(50));
    CHECK(onString(doc.song(), 0, 0, 2).effects.bend.isEmpty());
    CHECK(bendAtCaret(doc.song(), at(0, 0, 2)).isEmpty());
    for (const Caret& nothing : {at(0, 2, 1), at(0, 1, 3), at(1, 1, 1)}) {
        CHECK(setBend(doc.song(), nothing, BendCurve::bend(200)).command == nullptr);
        CHECK(bendAtCaret(doc.song(), nothing).isEmpty());
    }

    // Without strings, every note of the beat; the caret reads the first's bend.
    Document piano(
        oneBeat("piano", {Note{.pitch = Pitch::fromMidi(60)}, Note{.pitch = Pitch::fromMidi(64)}}));
    run(piano, setBend(piano.song(), at(0, 0), BendCurve::bend(100)));
    for (const Note& note : notesAt(piano.song(), 0, 0)) {
        CHECK(note.effects.bend == BendCurve::bend(100));
    }
    CHECK(bendAtCaret(piano.song(), at(0, 0)) == BendCurve::bend(100));
}

TEST_CASE("Effects: strums, pick strokes and tremolo picking on the caret's beat",
          "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret chord = at(0, 0, 2); // frets 5 and 7
    CHECK(beatEffectsAt(doc.song(), chord) == BeatEffects{});

    BeatEffects strummed;
    strummed.strum = Strum::Down;
    strummed.tremoloPicking = NoteValue::Sixteenth;
    CHECK(run(doc, setBeatEffects(doc.song(), chord, strummed)) == chord);
    CHECK(doc.undoName() == "Change beat effects");
    const Beat& beat = doc.song().track(0).staff(0).measure(0).voices[0].beats[0];
    CHECK(beat.effects.strum == Strum::Down);
    CHECK(beat.effects.tremoloPicking == NoteValue::Sixteenth);
    CHECK(beatEffectsAt(doc.song(), chord) == strummed);
    // The same caret on another string reads the same beat.
    CHECK(beatEffectsAt(doc.song(), at(0, 0, 6)) == strummed);
    // Its notes keep their own effects.
    CHECK(onString(doc.song(), 0, 0, 2).effects == NoteEffects{});

    // The same effects again change nothing; others replace them.
    CHECK(setBeatEffects(doc.song(), chord, strummed).command == nullptr);
    BeatEffects picked;
    picked.pickStroke = PickStroke::Up;
    run(doc, setBeatEffects(doc.song(), chord, picked));
    CHECK(beatEffectsAt(doc.song(), chord) == picked);
    doc.undo();
    CHECK(beatEffectsAt(doc.song(), chord) == strummed);

    // A rest and the free slot have no notes to strike.
    for (const Caret& nothing : {at(0, 2, 1), at(1, 1, 1)}) {
        CHECK(setBeatEffects(doc.song(), nothing, picked).command == nullptr);
        CHECK(beatEffectsAt(doc.song(), nothing) == BeatEffects{});
    }

    // A rest can be held, slurred over and fall under a hairpin or an octave
    // line, nothing else (ADR-0085, ADR-0086, ADR-0089).
    const Caret rest = at(0, 2, 1);
    BeatEffects held = picked;
    held.fermata = Fermata::Long;
    held.hairpin = Hairpin::Diminuendo;
    held.slur = true;
    held.ottava = Ottava::Alta;
    run(doc, setBeatEffects(doc.song(), rest, held));
    BeatEffects onRest;
    onRest.fermata = Fermata::Long;
    onRest.hairpin = Hairpin::Diminuendo;
    onRest.slur = true;
    onRest.ottava = Ottava::Alta;
    CHECK(beatEffectsAt(doc.song(), rest) == onRest);
    CHECK(doc.song().track(0).staff(0).measure(0).voices[0].beats[2].effects == onRest);
    CHECK(setBeatEffects(doc.song(), rest, onRest).command == nullptr);
    CHECK(setBeatEffects(doc.song(), at(1, 1, 1), held).command == nullptr); // the free slot
}

TEST_CASE("Effects: a trill to the next step of the key, each note of a chord its own",
          "[core][entry][effects]") {
    // ADR-0090: in D major E trills with F#, two semitones up, C# with D, one.
    Song song =
        oneBeat("piano", {Note{.pitch = *Pitch::parse("E4")}, Note{.pitch = *Pitch::parse("C#5")}});
    song.measureHeader(0).keySignature = KeySignature(2, KeyMode::Major);
    Document doc(std::move(song));
    run(doc, setTrillToNextStep(doc.song(), at(0, 0), NoteValue::Sixteenth));
    CHECK(doc.undoName() == "Change trill");
    CHECK(notesAt(doc.song(), 0, 0)[0].effects.trill ==
          Trill{.semitones = 2, .speed = NoteValue::Sixteenth});
    CHECK(notesAt(doc.song(), 0, 0)[1].effects.trill ==
          Trill{.semitones = 1, .speed = NoteValue::Sixteenth});
    CHECK(setTrillToNextStep(doc.song(), at(0, 0), NoteValue::Sixteenth).command == nullptr);

    // On a fretted staff the caret's string only: fret 5 on string 2 is E4.
    Document guitar(guitarSong());
    run(guitar, setTrillToNextStep(guitar.song(), at(0, 0, 2), NoteValue::ThirtySecond));
    CHECK(onString(guitar.song(), 0, 0, 2).effects.trill.semitones == 1); // to F in C major
    CHECK(!onString(guitar.song(), 0, 0, 3).effects.trill.isTrill());
    // A rest has none to trill.
    CHECK(setTrillToNextStep(guitar.song(), at(0, 2), NoteValue::ThirtySecond).command == nullptr);
}

TEST_CASE("Effects: slides, harmonics, vibrato and accents of any kind on the caret's notes",
          "[core][entry][effects]") {
    Document doc(guitarSong());
    const Caret caret = at(0, 1, 1); // fret 12 on string 1
    CHECK(noteEffectsAt(doc.song(), caret) == NoteEffects{});

    // Slides in and out, each kept when the other changes.
    CHECK(run(doc, setSlides(doc.song(), caret, SlideIn::FromBelow, SlideOut::None)) == caret);
    CHECK(doc.undoName() == "Change slide");
    run(doc, setSlides(doc.song(), caret, SlideIn::FromBelow, SlideOut::Downwards));
    CHECK(onString(doc.song(), 0, 1, 1).effects.slideIn == SlideIn::FromBelow);
    CHECK(onString(doc.song(), 0, 1, 1).effects.slideOut == SlideOut::Downwards);
    CHECK(hasEffectAtCaret(doc.song(), caret, NoteEffect::Slide));
    CHECK(setSlides(doc.song(), caret, SlideIn::FromBelow, SlideOut::Downwards).command == nullptr);

    // Vibrato and accents in either strength.
    run(doc, setVibrato(doc.song(), caret, Vibrato::Wide));
    CHECK(doc.undoName() == "Change vibrato");
    run(doc, setAccent(doc.song(), caret, Accent::Marcato));
    CHECK(doc.undoName() == "Change accent");
    const NoteEffects fx = noteEffectsAt(doc.song(), caret);
    CHECK(fx.vibrato == Vibrato::Wide);
    CHECK(fx.accent == Accent::Marcato);
    CHECK(fx.slideOut == SlideOut::Downwards);
    CHECK(setVibrato(doc.song(), caret, Vibrato::Wide).command == nullptr);
    CHECK(setAccent(doc.song(), caret, Accent::Marcato).command == nullptr);

    // Trills to any note, at any speed (ADR-0090).
    const Trill trill{.semitones = 2, .speed = NoteValue::Sixteenth};
    run(doc, setTrill(doc.song(), caret, trill));
    CHECK(doc.undoName() == "Change trill");
    CHECK(noteEffectsAt(doc.song(), caret).trill == trill);
    CHECK(noteEffectsAt(doc.song(), caret).vibrato == Vibrato::Wide);
    CHECK(setTrill(doc.song(), caret, trill).command == nullptr);
    CHECK_THROWS_AS(setTrill(doc.song(), caret, Trill{.semitones = 13}), std::out_of_range);
    run(doc, setTrill(doc.song(), caret, Trill{}));
    CHECK(!noteEffectsAt(doc.song(), caret).trill.isTrill());

    // Harmonics: natural at the note's fret, the others an octave above the
    // fretted note; none removes it.
    run(doc, setHarmonic(doc.song(), caret, HarmonicKind::Natural));
    CHECK(doc.undoName() == "Change harmonic");
    CHECK(noteEffectsAt(doc.song(), caret).harmonic ==
          Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 120});
    for (const HarmonicKind kind : {HarmonicKind::Artificial, HarmonicKind::Pinch,
                                    HarmonicKind::Tapped, HarmonicKind::Semi}) {
        run(doc, setHarmonic(doc.song(), caret, kind));
        CHECK(noteEffectsAt(doc.song(), caret).harmonic ==
              Harmonic{.kind = kind, .fretTenths = 120});
    }
    CHECK(setHarmonic(doc.song(), caret, HarmonicKind::Semi).command == nullptr);
    run(doc, setHarmonic(doc.song(), caret, HarmonicKind::None));
    CHECK(noteEffectsAt(doc.song(), caret).harmonic == Harmonic{});
    // Fret 7 has a natural harmonic and fret 6 none; an artificial one goes anywhere.
    run(doc, setHarmonic(doc.song(), at(0, 0, 3), HarmonicKind::Natural));
    CHECK(onString(doc.song(), 0, 0, 3).effects.harmonic.fretTenths == 70);
    CHECK(onString(doc.song(), 0, 0, 2).effects.harmonic == Harmonic{});
    CHECK_THROWS_AS(setHarmonic(doc.song(), at(1, 0, 3), HarmonicKind::Natural), std::out_of_range);
    run(doc, setHarmonic(doc.song(), at(1, 0, 3), HarmonicKind::Artificial));
    CHECK(onString(doc.song(), 1, 0, 3).effects.harmonic.kind == HarmonicKind::Artificial);

    // Undo goes back one change at a time.
    doc.undo();
    doc.undo();
    doc.undo();
    CHECK(noteEffectsAt(doc.song(), caret).harmonic ==
          Harmonic{.kind = HarmonicKind::Semi, .fretTenths = 120});

    // Nothing on a rest, a string without a note or the free slot.
    for (const Caret& nothing : {at(0, 2, 1), at(0, 1, 3), at(1, 1, 1)}) {
        CHECK(setVibrato(doc.song(), nothing, Vibrato::Slight).command == nullptr);
        CHECK(setSlides(doc.song(), nothing, SlideIn::FromAbove, SlideOut::Shift).command ==
              nullptr);
        CHECK(noteEffectsAt(doc.song(), nothing) == NoteEffects{});
    }

    // Without strings every note of the beat, and no harmonics.
    Document piano(
        oneBeat("piano", {Note{.pitch = Pitch::fromMidi(60)}, Note{.pitch = Pitch::fromMidi(64)}}));
    run(piano, setAccent(piano.song(), at(0, 0), Accent::Accent));
    for (const Note& note : notesAt(piano.song(), 0, 0)) {
        CHECK(note.effects.accent == Accent::Accent);
    }
    CHECK_THROWS_AS(setHarmonic(piano.song(), at(0, 0), HarmonicKind::Natural), std::logic_error);
}
