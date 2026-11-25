// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro 3, 4 and 5 files: the corpus files written by
// tests/corpus/gp/generate_binary.py.
#include <elara/core/validation.hpp>
#include <elara/io/guitar_pro.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core = elara::core;
using core::Duration;
using core::NoteValue;
using elara::io::importGuitarPro;
using elara::io::importGuitarProFile;
using elara::io::ImportResult;

namespace {

std::filesystem::path corpusDir() {
    return std::filesystem::path(ELARA_CORPUS_DIR) / "gp";
}

ImportResult load(const std::string& name) {
    auto result = importGuitarProFile(corpusDir() / name);
    if (!result) {
        FAIL(result.error().message);
    }
    return *result;
}

std::vector<std::uint8_t> bytesOf(const std::string& name) {
    std::ifstream in(corpusDir() / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

std::vector<int> midiOf(const core::Beat& beat) {
    std::vector<int> pitches;
    for (const core::Note& note : beat.notes) {
        pitches.push_back(note.pitch.midiNumber());
    }
    return pitches;
}

} // namespace

TEST_CASE("Guitar Pro 5: a guitar keeps its tuning, capo, fret positions and techniques",
          "[io][guitarpro]") {
    const ImportResult result = load("techniques.gp5");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());
    CHECK(core::validate(song).empty());
    CHECK(song.info().title == "Corpus Techniques");
    // A marker names the section that starts at its measure (ADR-0062).
    CHECK(song.measureHeader(0).section == "Intro");
    CHECK(song.measureHeader(1).section.empty());
    // Volume 14 and balance 10 of 16 on its channel; Guitar Pro 5 flags solo.
    CHECK(song.track(0).mix() ==
          core::TrackMix{.volume = 0.875, .pan = 0.25, .mute = false, .solo = true});
    CHECK(song.info().subtitle == "Guitar Pro 5");
    CHECK(song.info().music == "Ghost in the Groove");
    CHECK(song.info().words.empty());
    CHECK(song.info().transcriber == "Elara contributors");
    CHECK(song.info().notes == "Play with a pick\nSelf-made test file");
    // Intro |: 2 |1. 3 :|2. 4 |
    CHECK(song.measureHeader(1).repeatStart);
    CHECK(song.measureHeader(2).repeatTimes == 2);
    CHECK(song.measureHeader(2).alternateEndings == 0b01);
    CHECK(song.measureHeader(3).alternateEndings == 0b10);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 2, 1, 3});

    REQUIRE(song.measureCount() == 4);
    CHECK(song.startTempo() == core::Tempo(100));
    REQUIRE(song.measureHeader(0).tempoChanges.size() == 1);
    CHECK(song.measureHeader(0).tempoChanges[0].offset == core::Fraction(1, 2));
    CHECK(song.measureHeader(0).tempoChanges[0].tempo == core::Tempo(80));
    CHECK(song.measureHeader(0).keySignature == core::KeySignature(1, core::KeyMode::Major));
    CHECK(song.measureHeader(2).keySignature == core::KeySignature(2, core::KeyMode::Major));
    CHECK(song.measureHeader(2).timeSignature == core::TimeSignature(3, 4));
    CHECK(song.measureHeader(3).timeSignature == core::TimeSignature(4, 4));

    REQUIRE(song.trackCount() == 1);
    const core::Track& guitar = song.track(0);
    CHECK(guitar.name() == "Lead Guitar");
    CHECK(guitar.instrument() == core::Instrument{.name = "Overdriven Guitar",
                                                  .kind = core::InstrumentKind::Fretted,
                                                  .midiProgram = 29,
                                                  .writtenOffset = 12});
    const core::Staff& staff = guitar.staff(0);
    CHECK(staff.clef() == core::Clef::Treble8vb);
    CHECK(staff.capo() == 2);
    REQUIRE(staff.tuning().has_value());
    CHECK(staff.tuning()->toString() == "E2 A2 D3 G3 B3 E4");

    // Measure 1: frets count from the capo; the hammer-on joins the next note
    // on its string; the second voice holds only an empty beat.
    const auto& bar1 = staff.measure(0).voices[0].beats;
    REQUIRE(bar1.size() == 6);
    CHECK(midiOf(bar1[0]) == std::vector<int>{42});
    CHECK(bar1[0].notes[0].position == core::FretPosition{.string = 6, .fret = 0});
    CHECK(bar1[0].notes[0].effects.palmMute);
    CHECK(bar1[0].notes[0].effects.dynamic == core::Dynamic::F);
    CHECK(bar1[1].duration == Duration(NoteValue::Eighth, 0, core::Tuplet(3, 2)));
    CHECK_FALSE(bar1[1].notes[0].effects.legatoFromPrevious);
    CHECK(bar1[2].notes[0].effects.legatoFromPrevious);
    CHECK(bar1[2].notes[0].position == core::FretPosition{.string = 5, .fret = 4});
    CHECK_FALSE(bar1[3].notes[0].effects.legatoFromPrevious);
    CHECK(bar1[4].notes[0].effects.bend == core::BendCurve({{.position = 0, .cents = 0},
                                                            {.position = 50, .cents = 200},
                                                            {.position = 100, .cents = 200}}));
    CHECK(bar1[4].notes[0].effects.vibrato == core::Vibrato::Slight);
    CHECK(midiOf(bar1[5]) == std::vector<int>{78});
    CHECK(bar1[5].notes[0].effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Natural, .fretTenths = 120});
    CHECK(bar1[5].notes[0].effects.letRing);
    CHECK(staff.measure(0).voices[1].isEmpty());

    // Measure 2: the tie takes the fret before it; a dead note with a heavy
    // accent, louder; a second voice with a rest.
    const auto& bar2 = staff.measure(1).voices[0].beats;
    REQUIRE(bar2.size() == 3);
    CHECK(bar2[0].notes[0].effects.dynamic == core::Dynamic::MF);
    CHECK(bar2[1].notes[0].effects.tiedFromPrevious);
    CHECK(bar2[1].notes[0].position == core::FretPosition{.string = 3, .fret = 5});
    CHECK(midiOf(bar2[1]) == std::vector<int>{62});
    CHECK(bar2[2].notes[0].effects.deadNote);
    CHECK(bar2[2].notes[0].effects.accent == core::Accent::Marcato);
    CHECK(bar2[2].notes[0].effects.dynamic == core::Dynamic::FF);
    const auto& low = staff.measure(1).voices[1].beats;
    REQUIRE(low.size() == 2);
    CHECK(midiOf(low[0]) == std::vector<int>{42});
    CHECK(low[1].isRest());

    // Measure 3: an on-beat sixteenth grace note at fret 1 of the main note's
    // string slides into it (ADR-0079).
    const auto& bar3 = staff.measure(2).voices[0].beats;
    REQUIRE(bar3.size() == 3);
    REQUIRE(bar3[0].graces.size() == 1);
    const core::Grace& grace = bar3[0].graces[0];
    CHECK(grace.duration == Duration(NoteValue::Sixteenth));
    CHECK(grace.onBeat);
    REQUIRE(grace.notes.size() == 1);
    CHECK(grace.notes[0].position == core::FretPosition{.string = 2, .fret = 1});
    CHECK(grace.notes[0].pitch.midiNumber() == 62);
    CHECK(grace.notes[0].effects.slideOut == core::SlideOut::Legato);
    CHECK(grace.notes[0].effects.dynamic == core::Dynamic::F);
    CHECK_FALSE(bar3[0].notes[0].effects.legatoFromPrevious);
    CHECK(bar3[1].graces.empty());
    CHECK(bar3[0].duration == Duration(NoteValue::Quarter, 1));
    CHECK(bar3[0].effects.pickStroke == core::PickStroke::Down);
    CHECK(bar3[0].notes[0].effects.slideOut == core::SlideOut::Shift);
    CHECK(bar3[0].notes[0].effects.staccato);
    CHECK(bar3[2].isRest());

    // Measure 4: strum, tremolo picking, a tapped harmonic twelve frets up and
    // a whammy-bar dip of a semitone.
    const auto& bar4 = staff.measure(3).voices[0].beats;
    REQUIRE(bar4.size() == 3);
    CHECK(midiOf(bar4[0]) == std::vector<int>{68, 64, 59});
    CHECK(bar4[0].effects.strum == core::Strum::Up);
    CHECK(bar4[1].effects.tremoloPicking == NoteValue::Sixteenth);
    CHECK(bar4[1].notes[0].effects.slideOut == core::SlideOut::Downwards);
    CHECK(bar4[2].notes[0].effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Tapped, .fretTenths = 120});
    CHECK(bar4[2].effects.whammyBar == core::BendCurve({{.position = 0, .cents = 0},
                                                        {.position = 50, .cents = -100},
                                                        {.position = 100, .cents = 0}}));
}

TEST_CASE("Guitar Pro 5: a piano reaching into both clefs becomes a grand staff split at middle C",
          "[io][guitarpro]") {
    const ImportResult result = load("piano.gp5");
    CHECK(core::validate(result.song).empty());
    const core::Track& piano = result.song.track(0);
    CHECK(piano.instrument().kind == core::InstrumentKind::Keyboard);
    REQUIRE(piano.staffCount() == 2);
    CHECK(piano.staff(0).clef() == core::Clef::Treble);
    CHECK(piano.staff(1).clef() == core::Clef::Bass);
    const auto sorted = [](const core::Beat& beat) {
        std::vector<int> pitches = midiOf(beat);
        std::ranges::sort(pitches);
        return pitches;
    };

    // Measure 1: the strummed chord splits between the hands, both keeping the
    // strum; a beat with nothing below middle C is a rest on the bass staff.
    const auto& upper = piano.staff(0).measure(0).voices[0].beats;
    const auto& lower = piano.staff(1).measure(0).voices[0].beats;
    REQUIRE(upper.size() == 3);
    REQUIRE(lower.size() == 3);
    CHECK(sorted(upper[0]) == std::vector{64, 72});
    CHECK(sorted(lower[0]) == std::vector{41});
    CHECK(upper[0].effects.strum == core::Strum::Up);
    CHECK(lower[0].effects.strum == core::Strum::Up);
    CHECK(sorted(upper[1]) == std::vector{69});
    CHECK(lower[1].isRest());
    CHECK(lower[1].effects == core::BeatEffects{});
    CHECK(sorted(upper[2]) == std::vector{67});
    CHECK(sorted(lower[2]) == std::vector{48});
    for (std::size_t b = 0; b < upper.size(); ++b) {
        CHECK(upper[b].duration == lower[b].duration);
    }
    const auto accented = std::ranges::find_if(
        upper[0].notes, [](const core::Note& note) { return note.pitch.midiNumber() == 72; });
    REQUIRE(accented != upper[0].notes.end());
    CHECK(accented->effects.accent == core::Accent::Accent);
    CHECK(piano.staff(1).measure(0).voices[1].isEmpty());

    // Measure 2: the melody of the first voice leaves no rests on the bass
    // staff, where the left hand keeps the second voice: voices are numbered
    // the same in every measure, and the first voice had notes there before.
    const core::Measure& right = piano.staff(0).measure(1);
    const core::Measure& left = piano.staff(1).measure(1);
    CHECK(right.voices[0].beats.size() == 7);
    CHECK(right.voices[1].isEmpty());
    CHECK(left.voices[0].isEmpty());
    REQUIRE(left.voices[1].beats.size() == 1);
    CHECK(sorted(left.voices[1].beats[0]) == std::vector{41, 48});
    CHECK(left.voices[1].beats[0].duration == Duration(NoteValue::Whole));

    // The left hand always in the second voice: it becomes the bass staff's
    // first voice, and the treble staff keeps only the right hand.
    const core::Track& electric = result.song.track(1);
    REQUIRE(electric.staffCount() == 2);
    for (std::size_t m = 0; m < 2; ++m) {
        CHECK(electric.staff(0).measure(m).voices[1].isEmpty());
        CHECK(electric.staff(1).measure(m).voices[1].isEmpty());
    }
    CHECK(electric.staff(0).measure(0).voices[0].beats.size() == 2);
    const auto& bass = electric.staff(1).measure(1).voices[0].beats;
    REQUIRE(bass.size() == 2);
    CHECK(sorted(bass[0]) == std::vector{48});
    CHECK(sorted(bass[1]) == std::vector{43});
}

TEST_CASE("Guitar Pro 5: directions become signs and jumps", "[io][guitarpro]") {
    // 1 | 2 segno | 3 da coda | 4 D.S. al Coda | 5 coda | 6 (ADR-0049)
    const ImportResult result = load("dal-segno.gp5");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());
    CHECK(song.measureHeader(1).segno);
    CHECK(song.measureHeader(2).toCoda);
    CHECK(song.measureHeader(3).jump == core::JumpTo::Segno);
    CHECK(song.measureHeader(3).jumpThen == core::JumpThen::Coda);
    CHECK(song.measureHeader(4).coda);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 2, 3, 1, 2, 4, 5});
    // Nothing else: the other measures carry no marks.
    CHECK(song.measureHeader(0) == song.measureHeader(5));
}

TEST_CASE("Guitar Pro 4: a band keeps its instruments, drums and tempo changes",
          "[io][guitarpro]") {
    const ImportResult result = load("quartet.gp4");
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{"triplet feel dropped",
                                   "Drums: instrument and volume changes dropped (measure 2)",
                                   "Melody: lyrics after the last note dropped"});
    CHECK(core::validate(song).empty());
    // Each track reads its own channel: the bass at 12 of 16, balance 4 (half
    // left), the drums full, the others at Guitar Pro's default 13.
    REQUIRE(song.trackCount() == 4);
    CHECK(song.track(0).mix() == core::TrackMix{.volume = 0.75, .pan = -0.5});
    CHECK(song.track(1).mix() == core::TrackMix{.volume = 0.8125, .pan = 0.0});
    CHECK(song.track(2).mix() == core::TrackMix{.volume = 1.0, .pan = 0.0});
    CHECK(song.track(3).mix() == core::TrackMix{.volume = 0.8125, .pan = 0.0});
    // |:1. 1 :|2. 2 |: one more time, and the second ending takes the times the
    // first one leaves.
    CHECK(song.measureHeader(0).repeatStart);
    CHECK(song.measureHeader(0).repeatTimes == 2);
    CHECK(song.measureHeader(0).alternateEndings == 0b01);
    CHECK(song.measureHeader(1).alternateEndings == 0b10);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1});
    // Guitar Pro 4 has one author for words and music.
    CHECK(song.info().words == "Ghost in the Groove");
    CHECK(song.info().music == "Ghost in the Groove");
    CHECK(song.startTempo() == core::Tempo(90));
    REQUIRE(song.measureHeader(1).tempoChanges.size() == 1);
    CHECK(song.measureHeader(1).tempoChanges[0].offset == core::Fraction(0));
    CHECK(song.measureHeader(1).tempoChanges[0].tempo == core::Tempo(120));
    CHECK(song.measureHeader(1).keySignature == core::KeySignature(-1, core::KeyMode::Major));

    // Lyrics (ADR-0084): "[Verse] Sing a-long  hey there" from measure 1 and
    // "oh" from measure 2. The double space leaves a beat blank, the tied note
    // takes no syllable, and "there" finds no beat.
    using Verses = std::vector<core::Syllable>;
    const core::Staff& singer = song.track(3).staff(0);
    const auto& first = singer.measure(0).voices[0].beats;
    const auto& second = singer.measure(1).voices[0].beats;
    REQUIRE(first.size() == 3);
    REQUIRE(second.size() == 3);
    REQUIRE(first[0].lyrics);
    CHECK(*first[0].lyrics == Verses{{.text = "Sing", .hyphen = false}});
    CHECK(*first[1].lyrics == Verses{{.text = "a", .hyphen = true}});
    CHECK(*first[2].lyrics == Verses{{.text = "long", .hyphen = false}});
    REQUIRE(second[0].lyrics);
    CHECK(*second[0].lyrics == Verses{{}, {.text = "oh", .hyphen = false}});
    CHECK_FALSE(second[1].lyrics);
    REQUIRE(second[2].lyrics);
    CHECK(*second[2].lyrics == Verses{{.text = "hey", .hyphen = false}});

    REQUIRE(song.trackCount() == 4);
    const core::Track& bass = song.track(0);
    CHECK(bass.instrument() == core::Instrument{.name = "Electric Bass (finger)",
                                                .kind = core::InstrumentKind::Fretted,
                                                .midiProgram = 33,
                                                .writtenOffset = 12});
    CHECK(bass.staff(0).clef() == core::Clef::Bass8vb);
    CHECK(bass.staff(0).tuning()->toString() == "E1 A1 D2 G2");
    const auto& bassBar = bass.staff(0).measure(0).voices[0].beats;
    REQUIRE(bassBar.size() == 4);
    CHECK(midiOf(bassBar[0]) == std::vector<int>{36});
    CHECK(bassBar[0].effects.bassTechnique == core::BassTechnique::Slap);
    CHECK(bassBar[1].effects.bassTechnique == core::BassTechnique::Pop);
    CHECK(bassBar[2].notes[0].effects.slideIn == core::SlideIn::FromBelow);
    CHECK(bassBar[3].notes[0].effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Natural, .fretTenths = 120});
    CHECK(bass.staff(0).measure(1).voices[0].beats[0].notes[0].effects.letRing);

    // A piano program makes a keyboard: the pitches stay, the fret positions go.
    const core::Track& piano = song.track(1);
    CHECK(piano.instrument().kind == core::InstrumentKind::Keyboard);
    CHECK(piano.staff(0).clef() == core::Clef::Treble);
    CHECK_FALSE(piano.staff(0).tuning().has_value());
    const auto& chord = piano.staff(0).measure(0).voices[0].beats[0];
    CHECK(midiOf(chord) == std::vector<int>{67, 64, 60});
    CHECK_FALSE(chord.notes[0].position.has_value());
    CHECK(piano.staff(0).measure(1).voices[0].beats[0].notes[0].effects.accent ==
          core::Accent::Accent);

    // Drum notes are their General MIDI keys.
    const core::Track& drums = song.track(2);
    CHECK(drums.instrument().kind == core::InstrumentKind::Percussion);
    CHECK(drums.staff(0).clef() == core::Clef::Percussion);
    const auto& groove = drums.staff(0).measure(0).voices[0].beats;
    REQUIRE(groove.size() == 4);
    CHECK(midiOf(groove[0]) == std::vector<int>{42, 36});
    CHECK(midiOf(groove[1]) == std::vector<int>{42, 38});

    const core::Track& melody = song.track(3);
    CHECK(melody.instrument() == core::Instrument{.name = "Flute",
                                                  .kind = core::InstrumentKind::Pitched,
                                                  .midiProgram = 73,
                                                  .writtenOffset = 0});
    const auto& tune = melody.staff(0).measure(0).voices[0].beats;
    CHECK(tune[1].notes[0].effects.vibrato == core::Vibrato::Slight);
    CHECK(tune[2].notes[0].effects.bend ==
          core::BendCurve({{.position = 0, .cents = 0}, {.position = 100, .cents = 100}}));
    CHECK(melody.staff(0).measure(1).voices[0].beats[1].notes[0].effects.tiedFromPrevious);
    // A trill from fret 5 to fret 7 in thirty-seconds (ADR-0090).
    CHECK(melody.staff(0).measure(1).voices[0].beats[2].notes[0].effects.trill ==
          core::Trill{.semitones = 2, .speed = NoteValue::ThirtySecond});
}

TEST_CASE("Guitar Pro 3: text is Windows-1252, harmonics and whammy dips belong to the beat",
          "[io][guitarpro]") {
    const ImportResult result = load("melody.gp3");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());
    CHECK(core::validate(song).empty());
    CHECK(song.track(0).mix() == core::TrackMix{.volume = 0.8125, .pan = 0.0});
    CHECK(song.info().title == "Corpus Melod\xc3\xad"
                               "a");
    CHECK(song.info().notes == "Self-made test file\nSecond line");
    CHECK(song.startTempo() == core::Tempo(75));
    CHECK(song.measureHeader(0).keySignature == core::KeySignature(0, core::KeyMode::Minor));
    CHECK(song.measureHeader(1).timeSignature == core::TimeSignature(6, 8));

    const core::Staff& staff = song.track(0).staff(0);
    CHECK(song.track(0).instrument().name == "Acoustic Guitar (nylon)");
    const auto& bar1 = staff.measure(0).voices[0].beats;
    REQUIRE(bar1.size() == 8);
    CHECK(bar1[0].notes[0].effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Natural, .fretTenths = 120});
    CHECK(bar1[2].notes[0].effects.legatoFromPrevious);
    CHECK(bar1[2].notes[0].effects.slideOut == core::SlideOut::Shift);
    CHECK(bar1[3].duration == Duration(NoteValue::Sixteenth, 0, core::Tuplet(5, 4)));
    CHECK(bar1[3].effects.bassTechnique == core::BassTechnique::Tap);
    CHECK(midiOf(bar1[5]) == std::vector<int>{59});

    const auto& bar2 = staff.measure(1).voices[0].beats;
    REQUIRE(bar2.size() == 3);
    CHECK(bar2[0].notes[0].effects.vibrato == core::Vibrato::Wide);
    CHECK(bar2[0].notes[0].effects.letRing);
    CHECK(bar2[1].effects.whammyBar == core::BendCurve({{.position = 0, .cents = 0},
                                                        {.position = 50, .cents = -100},
                                                        {.position = 100, .cents = 0}}));
    CHECK(bar2[2].effects.strum == core::Strum::Down);
    // A thirty-second grace note at fret 3, before the beat, pulled off to the
    // open string (ADR-0079).
    REQUIRE(bar2[2].graces.size() == 1);
    CHECK(bar2[2].graces[0].duration == Duration(NoteValue::ThirtySecond));
    CHECK_FALSE(bar2[2].graces[0].onBeat);
    REQUIRE(bar2[2].graces[0].notes.size() == 1);
    CHECK(bar2[2].graces[0].notes[0].position == core::FretPosition{.string = 6, .fret = 3});
    CHECK(bar2[2].graces[0].notes[0].pitch.midiNumber() == 43);
    CHECK(bar2[2].notes[0].effects.legatoFromPrevious);
}

TEST_CASE("Guitar Pro 3-5: chords keep their names and diagrams", "[io][guitarpro]") {
    // ADR-0080. Frets go from string 1, the highest; -1 is a string not played.
    constexpr std::int8_t x = core::ChordDiagram::kMuted;
    const auto chordOf = [](const core::Song& song, std::size_t m, std::size_t b) {
        const core::Beat& beat = song.track(0).staff(0).measure(m).voices[0].beats.at(b);
        REQUIRE(beat.chord);
        return *beat.chord;
    };
    const auto diagram = [](std::vector<std::int8_t> frets) {
        return core::ChordDiagram{.firstFret = 1, .frets = std::move(frets)};
    };

    // Guitar Pro 3: the newer format, then the older one with six frets.
    const ImportResult melody = load("melody.gp3");
    CHECK(chordOf(melody.song, 0, 2) ==
          core::ChordSymbol{.name = "Am", .diagram = diagram({0, 1, 2, 2, 0, x})});
    CHECK(chordOf(melody.song, 1, 0) ==
          core::ChordSymbol{.name = "E", .diagram = diagram({0, 0, 1, 2, 2, 0})});
    CHECK_FALSE(melody.song.track(0).staff(0).measure(0).voices[0].beats[0].chord);

    // Guitar Pro 4 and 5: the newer format, seven frets for six strings.
    const ImportResult quartet = load("quartet.gp4");
    const core::Beat& melodyBeat = quartet.song.track(3).staff(0).measure(0).voices[0].beats[0];
    REQUIRE(melodyBeat.chord);
    CHECK(*melodyBeat.chord ==
          core::ChordSymbol{.name = "D/F#", .diagram = diagram({2, 3, 2, 0, x, 2})});
    const ImportResult techniques = load("techniques.gp5");
    CHECK(chordOf(techniques.song, 2, 0) ==
          core::ChordSymbol{.name = "G", .diagram = diagram({3, 0, 0, 0, 2, 3})});
    // A chord symbol does not sound.
    CHECK(techniques.song.track(0).staff(0).measure(2).voices[0].beats[0].notes.size() == 1);
}

TEST_CASE("Guitar Pro 3-5: notes keep the fingers of both hands", "[io][guitarpro]") {
    // ADR-0093: the fretting hand's finger, then the plucking hand's.
    const auto noteOf = [](const core::Song& song, std::size_t m, std::size_t b) {
        return song.track(0).staff(0).measure(m).voices[0].beats.at(b).notes.at(0);
    };
    const core::Note plucked = noteOf(load("melody.gp3").song, 0, 1);
    CHECK(plucked.effects.fingering == core::Finger::Index);
    CHECK(plucked.effects.pluck == core::Finger::Middle);
    const core::Note fretted = noteOf(load("techniques.gp5").song, 1, 0);
    CHECK(fretted.effects.fingering == core::Finger::Ring);
    CHECK(fretted.effects.pluck == core::Finger::None);
    CHECK(fretted.effects.dynamic == core::Dynamic::MF); // what follows the fingers
    CHECK(noteOf(load("techniques.gp5").song, 0, 0).effects.fingering == core::Finger::None);
}

TEST_CASE("Guitar Pro 3-5: beats keep their text", "[io][guitarpro]") {
    // ADR-0081.
    const ImportResult melody = load("melody.gp3");
    const auto& bar2 = melody.song.track(0).staff(0).measure(1).voices[0].beats;
    REQUIRE(bar2.at(0).text);
    CHECK(*bar2[0].text == "ad lib.");
    CHECK(bar2[0].chord); // next to a chord symbol
    CHECK_FALSE(bar2.at(1).text);

    const ImportResult techniques = load("techniques.gp5");
    const auto& bar3 = techniques.song.track(0).staff(0).measure(2).voices[0].beats;
    REQUIRE(bar3.at(1).text);
    CHECK(*bar3[1].text == "let it ring");
    CHECK_FALSE(bar3[0].text);
    // Text does not sound.
    CHECK(bar3[1].notes.size() == 1);
}

TEST_CASE("Guitar Pro 3-5: other versions and damaged files are errors with a reason",
          "[io][guitarpro]") {
    const auto error = [](std::span<const std::uint8_t> bytes) {
        const auto imported = importGuitarPro(bytes);
        REQUIRE_FALSE(imported.has_value());
        return imported.error().message;
    };
    CHECK(error(bytesOf(std::string_view("\x18"
                                         "FICHIER GUITAR PRO v2.21"))) ==
          "Guitar Pro: Guitar Pro 1 and 2 files are not supported");
    CHECK(error(bytesOf(std::string_view("\x18"
                                         "FICHIER GUITAR PRO v7.00"))) ==
          "Guitar Pro: unknown version \"FICHIER GUITAR PRO v7.00\"");

    std::vector<std::uint8_t> cut = bytesOf(std::string("techniques.gp5"));
    REQUIRE(cut.size() > 600);
    cut.resize(600);
    CHECK(error(cut) == "Guitar Pro: damaged file: it ends early (byte 600)");
}
