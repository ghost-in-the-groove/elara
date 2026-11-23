// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro 3, 4 and 5 files: the corpus files written by
// tests/corpus/gp/generate_binary.py.
#include <elara/core/validation.hpp>
#include <elara/io/guitar_pro.hpp>

#include <catch2/catch_test_macros.hpp>

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
    CHECK(result.warnings ==
          std::vector<std::string>{"section marks dropped (measure 1)",
                                   "Lead Guitar: chord names and diagrams dropped (measure 3)",
                                   "Lead Guitar: grace notes dropped (measure 3)",
                                   "Lead Guitar: text dropped (measure 3)"});
    CHECK(core::validate(song).empty());
    CHECK(song.info().title == "Corpus Techniques");
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

    // Measure 3: the grace note is gone, its main note stays.
    const auto& bar3 = staff.measure(2).voices[0].beats;
    REQUIRE(bar3.size() == 3);
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

TEST_CASE("Guitar Pro 4: a band keeps its instruments, drums and tempo changes",
          "[io][guitarpro]") {
    const ImportResult result = load("quartet.gp4");
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{"triplet feel dropped", "Melody: lyrics dropped",
                                   "Drums: instrument and volume changes dropped (measure 2)",
                                   "Melody: trills dropped (measure 2)"});
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
}

TEST_CASE("Guitar Pro 3: text is Windows-1252, harmonics and whammy dips belong to the beat",
          "[io][guitarpro]") {
    const ImportResult result = load("melody.gp3");
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{
              "Nylon Guitar: chord names and diagrams dropped (2 times, first in measure 1)",
              "Nylon Guitar: text dropped (measure 2)"});
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
