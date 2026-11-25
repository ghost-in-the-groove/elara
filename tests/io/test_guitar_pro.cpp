// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/validation.hpp>
#include <elara/io/guitar_pro.hpp>

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace core = elara::core;
using core::Duration;
using core::NoteValue;
using elara::io::importGpif;
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

std::vector<std::uint8_t> zipOf(const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_heap(&zip, 0, 4096) != 0);
    for (const auto& [name, content] : entries) {
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(),
                                      static_cast<mz_uint>(MZ_DEFAULT_LEVEL)) != 0);
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) != 0);
    const auto* begin = static_cast<const std::uint8_t*>(buffer);
    std::vector<std::uint8_t> bytes(begin, begin + size);
    mz_free(buffer);
    mz_zip_writer_end(&zip);
    return bytes;
}

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

// A one-track GPIF score: a guitar in standard tuning with `capo`, one 4/4 bar
// whose only voice holds `beats`, and the given notes, rhythms and tempos.
std::string guitarScore(int capo, const std::string& beats, const std::string& notes,
                        const std::string& rhythms, const std::string& tempos = "") {
    return R"(<?xml version="1.0" encoding="utf-8"?><GPIF><Score><Title><![CDATA[T]]></Title></Score>)"
           "<MasterTrack><Tracks>0</Tracks><Automations>" +
           tempos +
           "</Automations></MasterTrack><Tracks><Track id=\"0\"><Name><![CDATA[Gtr]]></Name>"
           "<InstrumentSet><Name>Electric Guitar</Name><Type>electricGuitar</Type></InstrumentSet>"
           "<Transpose><Chromatic>0</Chromatic><Octave>-1</Octave></Transpose>"
           "<Sounds><Sound><MIDI><Program>27</Program></MIDI></Sound></Sounds><Staves><Staff>"
           "<Properties><Property name=\"CapoFret\"><Fret>" +
           std::to_string(capo) +
           "</Fret></Property><Property name=\"Tuning\"><Pitches>40 45 50 55 59 64</Pitches>"
           "</Property></Properties></Staff></Staves></Track></Tracks><MasterBars><MasterBar>"
           "<Key><AccidentalCount>0</AccidentalCount><Mode>Major</Mode></Key><Time>4/4</Time>"
           "<Bars>0</Bars></MasterBar></MasterBars><Bars><Bar id=\"0\"><Clef>G2</Clef>"
           "<Voices>0 -1 -1 -1</Voices></Bar></Bars><Voices><Voice id=\"0\"><Beats>" +
           beats + "</Beats></Voice></Voices><Beats>" + notes + "</Beats><Notes>" +
           R"(<Note id="0"><Properties><Property name="Fret"><Fret>3</Fret></Property>)"
           R"(<Property name="String"><String>0</String></Property></Properties></Note>)"
           "</Notes><Rhythms>" +
           rhythms + "</Rhythms></GPIF>";
}

// A one-track score as Guitar Pro writes it for any instrument: the staff has a
// tuning (a guitar's when the instrument has no strings) and the note a string,
// a fret and its MIDI pitch (E5: the highest string at fret 12).
std::string trackScore(int program) {
    return R"(<GPIF><Score /><MasterTrack><Tracks>0</Tracks></MasterTrack><Tracks>)"
           R"(<Track id="0"><Name><![CDATA[T]]></Name><Sounds><Sound><MIDI><Program>)" +
           std::to_string(program) +
           R"(</Program></MIDI></Sound></Sounds><Staves><Staff><Properties>)"
           R"(<Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>)"
           R"(</Properties></Staff></Staves></Track></Tracks><MasterBars><MasterBar>)"
           R"(<Time>4/4</Time><Bars>0</Bars></MasterBar></MasterBars><Bars><Bar id="0">)"
           R"(<Clef>G2</Clef><Voices>0 -1 -1 -1</Voices></Bar></Bars><Voices><Voice id="0">)"
           R"(<Beats>0</Beats></Voice></Voices><Beats><Beat id="0"><Rhythm ref="0"/>)"
           R"(<Notes>0</Notes></Beat></Beats><Notes><Note id="0"><Properties>)"
           R"(<Property name="String"><String>5</String></Property>)"
           R"(<Property name="Fret"><Fret>12</Fret></Property>)"
           R"(<Property name="Midi"><Number>76</Number></Property></Properties></Note></Notes>)"
           R"(<Rhythms><Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm></Rhythms></GPIF>)";
}

// Guitar Pro's PartConfiguration for a full score whose tracks show `flags`
// (1 standard notation, 2 tablature): the number of score views, then for each
// view a byte, its number of tracks and a byte per track; numbers are big-endian.
std::string partConfiguration(std::initializer_list<std::uint8_t> flags) {
    std::string bytes{0, 0, 0, 1, 0, 0, 0, 0, static_cast<char>(flags.size())};
    for (const std::uint8_t flag : flags) {
        bytes.push_back(static_cast<char>(flag));
    }
    return bytes + std::string(4, '\0'); // the score view on screen
}

} // namespace

TEST_CASE("Guitar Pro: a guitar keeps its tuning, capo, fret positions and techniques",
          "[io][guitarpro]") {
    const ImportResult result = load("guitar-techniques.gp");
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{
              "Lead Guitar: chords missing from the diagram collection dropped (measure 2)"});
    // |: 1 |1.2. 2 :|3. 3 |, played three times
    CHECK(song.measureHeader(0).repeatStart);
    CHECK(song.measureHeader(1).repeatTimes == 3);
    CHECK(song.measureHeader(1).alternateEndings == 0b011);
    CHECK(song.measureHeader(2).alternateEndings == 0b100);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 0, 1, 0, 2});
    CHECK(core::validate(song).empty());
    CHECK(song.info().title == "Corpus Guitar Techniques");
    CHECK(song.info().transcriber == "Elara contributors");
    CHECK(song.startTempo() == core::Tempo(100));
    REQUIRE(song.measureHeader(1).tempoChanges.size() == 1);
    CHECK(song.measureHeader(1).tempoChanges[0].offset == core::Fraction(1, 2));
    CHECK(song.measureHeader(1).tempoChanges[0].tempo == core::Tempo(80));
    CHECK(song.measureHeader(0).keySignature == core::KeySignature(1, core::KeyMode::Major));
    CHECK(song.measureHeader(2).timeSignature == core::TimeSignature(3, 4));

    REQUIRE(song.trackCount() == 1);
    const core::Track& guitar = song.track(0);
    CHECK(guitar.name() == "Lead Guitar");
    CHECK(guitar.instrument() == core::Instrument{.name = "Electric Guitar",
                                                  .kind = core::InstrumentKind::Fretted,
                                                  .midiProgram = 27,
                                                  .writtenOffset = 12});
    const core::Staff& staff = guitar.staff(0);
    CHECK(staff.clef() == core::Clef::Treble8vb);
    CHECK(staff.capo() == 2);
    REQUIRE(staff.tuning().has_value());
    CHECK(staff.tuning()->toString() == "D2 A2 D3 G3 B3 E4");

    // Bar 1: palm-muted open low string (E2 with the capo), a triplet with a
    // hammer-on, a bent note with vibrato, a natural harmonic left ringing.
    const auto& bar1 = staff.measure(0).voices[0].beats;
    REQUIRE(bar1.size() == 6);
    CHECK(bar1[0].notes[0].pitch.midiNumber() == 40);
    CHECK(bar1[0].notes[0].position == core::FretPosition{.string = 6, .fret = 0});
    CHECK(bar1[0].notes[0].effects.palmMute);
    CHECK(bar1[1].duration == Duration(NoteValue::Eighth, 0, core::Tuplet(3, 2)));
    CHECK(bar1[2].notes[0].position == core::FretPosition{.string = 5, .fret = 2});
    CHECK(bar1[2].notes[0].effects.legatoFromPrevious);
    CHECK(bar1[4].notes[0].effects.bend ==
          core::BendCurve({{.position = 0, .cents = 0}, {.position = 60, .cents = 200}}));
    CHECK(bar1[4].notes[0].effects.vibrato == core::Vibrato::Slight);
    CHECK(bar1[5].notes[0].effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Natural, .fretTenths = 120});
    CHECK(bar1[5].notes[0].effects.letRing);
    // A crescendo over the last two beats.
    CHECK(bar1[3].effects.hairpin == core::Hairpin::None);
    CHECK(bar1[4].effects.hairpin == core::Hairpin::Crescendo);
    CHECK(bar1[5].effects.hairpin == core::Hairpin::Crescendo);

    // Bar 2: a tie, then a dead note with an accent, louder.
    const auto& bar2 = staff.measure(1).voices[0].beats;
    CHECK(bar2[1].notes[0].effects.tiedFromPrevious);
    CHECK(bar2[1].notes[0].effects.dynamic == core::Dynamic::F);
    CHECK(bar2[2].notes[0].effects.deadNote);
    CHECK(bar2[2].notes[0].effects.accent == core::Accent::Accent);
    CHECK(bar2[2].notes[0].effects.dynamic == core::Dynamic::FF);

    // Bar 3: an eighth grace note two frets up on its string leads into a
    // dotted, staccato, downstroked note that slides to the next (ADR-0079).
    const auto& bar3 = staff.measure(2).voices[0].beats;
    REQUIRE(bar3.size() == 3);
    REQUIRE(bar3[0].graces.size() == 1);
    const core::Grace& grace = bar3[0].graces[0];
    CHECK(grace.duration == Duration(NoteValue::Eighth));
    CHECK_FALSE(grace.onBeat);
    REQUIRE(grace.notes.size() == 1);
    CHECK(grace.notes[0].position->string == bar3[0].notes[0].position->string);
    CHECK(grace.notes[0].position->fret == 7);
    CHECK(grace.notes[0].pitch.midiNumber() == bar3[0].notes[0].pitch.midiNumber() + 2);
    CHECK(bar3[0].duration == Duration(NoteValue::Quarter, 1));
    CHECK(bar3[0].effects.pickStroke == core::PickStroke::Down);
    CHECK(bar3[0].notes[0].effects.slideOut == core::SlideOut::Shift);
    CHECK(bar3[0].notes[0].effects.staccato);
    CHECK(bar3[2].isRest());
    // A diminuendo (a "decrescendo" in the file) over the two notes.
    CHECK(bar3[0].effects.hairpin == core::Hairpin::Diminuendo);
    CHECK(bar3[1].effects.hairpin == core::Hairpin::Diminuendo);
    CHECK(bar3[2].effects.hairpin == core::Hairpin::None);
}

TEST_CASE("Guitar Pro: chords come from the staff's diagram collection", "[io][guitarpro]") {
    // ADR-0080. A diagram counts its strings from the lowest; the model from
    // string 1, the highest.
    constexpr std::int8_t x = core::ChordDiagram::kMuted;
    const ImportResult result = load("guitar-techniques.gp");
    const core::Staff& staff = result.song.track(0).staff(0);
    const auto& bar1 = staff.measure(0).voices[0].beats;
    const auto& bar2 = staff.measure(1).voices[0].beats;
    const auto& bar3 = staff.measure(2).voices[0].beats;
    REQUIRE(bar1[0].chord);
    CHECK(*bar1[0].chord == core::ChordSymbol{.name = "D5",
                                              .diagram = core::ChordDiagram{
                                                  .firstFret = 1, .frets = {x, x, x, 0, 0, 0}}});
    CHECK_FALSE(bar1[1].chord);
    REQUIRE(bar2[0].chord);
    CHECK(*bar2[0].chord == core::ChordSymbol{.name = "A",
                                              .diagram = core::ChordDiagram{
                                                  .firstFret = 2, .frets = {0, 2, 2, 2, 0, x}}});
    CHECK_FALSE(bar2[2].chord); // its diagram is missing
    // The beat after the grace note names the first chord again: one symbol.
    REQUIRE(bar3[0].chord);
    CHECK(&*bar3[0].chord == &*bar1[0].chord);
}

TEST_CASE("Guitar Pro: a band keeps its instruments, staves, voices and drums", "[io][guitarpro]") {
    const ImportResult result = load("band.gp");
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{"Clarinet: volume and pan changes dropped",
                                   "Clarinet: short voices filled with rests (measure 2)"});
    CHECK(core::validate(song).empty());
    CHECK(song.info().words == "Ghost in the Groove");

    // The mixer: the channel strip's volume and pan, or the automation that
    // sets them at the start; the clarinet's later change is dropped.
    REQUIRE(song.trackCount() == 4);
    CHECK(song.track(0).mix() == core::TrackMix{.volume = 0.7, .pan = -0.4});
    CHECK(song.track(1).mix() == core::TrackMix{.volume = 0.795, .pan = 0.0});
    CHECK(song.track(2).mix() == core::TrackMix{.volume = 0.9, .pan = 0.0});
    CHECK(song.track(3).mix() == core::TrackMix{.volume = 0.6, .pan = 0.5});
    CHECK(song.info().music == "Ghost in the Groove");
    REQUIRE(song.measureHeader(1).tempoChanges.size() == 1);
    CHECK(song.measureHeader(1).tempoChanges[0].tempo ==
          core::Tempo(60, Duration(NoteValue::Quarter, 1)));
    CHECK(song.measureHeader(1).tempoChanges[0].gradual); // linear, with nowhere to go
    REQUIRE(song.trackCount() == 4);

    const core::Track& bass = song.track(0);
    CHECK(bass.instrument().kind == core::InstrumentKind::Fretted);
    CHECK(bass.staff(0).clef() == core::Clef::Bass8vb);
    CHECK(bass.staff(0).tuning()->toString() == "E1 A1 D2 G2");
    CHECK(bass.staff(0).measure(0).voices[0].beats[0].notes[0].position ==
          core::FretPosition{.string = 4, .fret = 1});

    const core::Track& piano = song.track(1);
    // Guitar Pro's default tunings on the piano and the clarinet are not theirs.
    CHECK(piano.instrument().kind == core::InstrumentKind::Keyboard);
    REQUIRE(piano.staffCount() == 2);
    CHECK_FALSE(piano.staff(0).isFretted());
    CHECK_FALSE(piano.staff(1).isFretted());
    CHECK(piano.staff(1).clef() == core::Clef::Bass);
    const core::Measure& right = piano.staff(0).measure(0);
    CHECK(right.voices[0].beats[0].notes.size() == 3);
    CHECK(right.voices[0].beats[1].notes[0].pitch == core::Pitch(core::Step::B, -1, 4));
    CHECK(right.voices[1].beats[0].notes[0].pitch.midiNumber() == 77);
    // Rolled chords: upwards sounds from the lowest note, as a downstroke does.
    CHECK(right.voices[0].beats[0].effects.strum == core::Strum::Down);
    CHECK(right.voices[0].beats[1].effects.strum == core::Strum::None);
    CHECK(piano.staff(0).measure(1).voices[0].beats[0].effects.strum == core::Strum::Up);
    // A guitar chord over the piano keeps the diagram's own six strings (ADR-0080).
    REQUIRE(right.voices[0].beats[0].chord);
    CHECK(*right.voices[0].beats[0].chord ==
          core::ChordSymbol{.name = "F",
                            .diagram =
                                core::ChordDiagram{.firstFret = 1, .frets = {1, 1, 2, 3, 3, 1}}});

    // Drum notes name the kit's articulations: hi-hat, kick, snare.
    const core::Track& drums = song.track(2);
    CHECK(drums.instrument().kind == core::InstrumentKind::Percussion);
    CHECK(drums.staff(0).clef() == core::Clef::Percussion);
    const auto& kit = drums.staff(0).measure(0).voices[0].beats;
    REQUIRE(kit.size() == 8);
    CHECK(kit[0].notes[0].pitch.midiNumber() == 42);
    CHECK(kit[0].notes[1].pitch.midiNumber() == 36);
    CHECK(kit[2].notes[1].pitch.midiNumber() == 38);

    // A B-flat clarinet: notes given only by their concert pitch, spelled as written.
    const core::Track& clarinet = song.track(3);
    CHECK(clarinet.instrument().writtenOffset == 2);
    CHECK(clarinet.instrument().kind == core::InstrumentKind::Pitched);
    CHECK_FALSE(clarinet.staff(0).isFretted());
    const auto& horn = clarinet.staff(0).measure(0).voices[0].beats;
    CHECK(horn[0].notes[0].pitch == core::Pitch(core::Step::B, -1, 4));
    CHECK(horn[2].notes[0].pitch == core::Pitch(core::Step::E, -1, 5));
    CHECK(clarinet.staff(0).measure(1).voices[0].beats.back().isRest());
    // The lyrics Guitar Pro dispatched to the clarinet's beats (ADR-0084).
    using Verses = std::vector<core::Syllable>;
    REQUIRE(horn[1].lyrics);
    CHECK(*horn[1].lyrics == Verses{{.text = "a", .hyphen = true}});
    REQUIRE(horn[2].lyrics);
    CHECK(*horn[2].lyrics == Verses{{.text = "long", .hyphen = false}});
}

TEST_CASE("Guitar Pro: a tuning that is never shown is dropped unless the instrument is fretted",
          "[io][guitarpro]") {
    // Guitar Pro gives every track a tuning, a flute's too. Its part
    // configuration says which tracks the full score shows as tablature.
    const auto open = [](int program, const std::string& parts) {
        std::vector<std::pair<std::string, std::string>> entries{
            {"Content/score.gpif", trackScore(program)}};
        if (!parts.empty()) {
            entries.emplace_back("Content/PartConfiguration", parts);
        }
        const auto imported = importGuitarPro(zipOf(entries));
        REQUIRE(imported.has_value());
        CHECK(imported->warnings.empty());
        return imported->song.track(0);
    };
    constexpr int kFlute = 73;
    constexpr int kGuitar = 27;

    // A flute shown as notation only: the tuning is not its own.
    const core::Track flute = open(kFlute, partConfiguration({1}));
    CHECK(flute.instrument().kind == core::InstrumentKind::Pitched);
    CHECK_FALSE(flute.staff(0).isFretted());
    CHECK(flute.staff(0).clef() == core::Clef::Treble);
    const core::Note& note = flute.staff(0).measure(0).voices[0].beats[0].notes[0];
    CHECK(note.pitch.midiNumber() == 76);
    CHECK_FALSE(note.position.has_value());

    // A guitar shown as notation only keeps its strings and frets.
    const core::Track guitar = open(kGuitar, partConfiguration({1}));
    CHECK(guitar.instrument().kind == core::InstrumentKind::Fretted);
    REQUIRE(guitar.staff(0).isFretted());
    CHECK(guitar.staff(0).measure(0).voices[0].beats[0].notes[0].position ==
          core::FretPosition{.string = 1, .fret = 12});

    // Shown as tablature, without a part configuration, or with one that does
    // not fit the score, the tuning stays.
    CHECK(open(kFlute, partConfiguration({3})).staff(0).isFretted());
    CHECK(open(kFlute, partConfiguration({2})).staff(0).isFretted());
    CHECK(open(kFlute, "").staff(0).isFretted());
    CHECK(open(kFlute, partConfiguration({1, 1})).staff(0).isFretted());
    CHECK(open(kFlute, std::string("\0\0\0\1\0\0\0", 7)).staff(0).isFretted());
}

TEST_CASE("Guitar Pro: directions become signs and jumps", "[io][guitarpro]") {
    // 1 | 2 segno | 3 segno segno | 4 da coda, da double coda
    // | 5 D.S.S. al Double Coda | 6 coda | 7 double coda (ADR-0049)
    const ImportResult result = load("double-coda.gp");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());
    CHECK(song.measureHeader(1).segno);
    CHECK(song.measureHeader(2).segnoSegno);
    CHECK(song.measureHeader(3).toCoda);
    CHECK(song.measureHeader(3).toDoubleCoda);
    CHECK(song.measureHeader(4).jump == core::JumpTo::SegnoSegno);
    CHECK(song.measureHeader(4).jumpThen == core::JumpThen::DoubleCoda);
    CHECK(song.measureHeader(5).coda);
    CHECK(song.measureHeader(6).doubleCoda);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 2, 3, 4, 2, 3, 6});
    // Sections are named by their text, or by their letter without one (ADR-0062).
    CHECK(song.measureHeader(0).section == "Intro");
    CHECK(song.measureHeader(1).section.empty());
    CHECK(song.measureHeader(5).section == "B");
}

TEST_CASE("Guitar Pro: frets count from the capo", "[io][guitarpro]") {
    const std::string beat = R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>)";
    const std::string whole = R"(<Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm>)";
    const auto imported = importGpif(guitarScore(5, "0", beat, whole));
    REQUIRE(imported.has_value());
    const core::Note& note =
        imported->song.track(0).staff(0).measure(0).voices[0].beats[0].notes[0];
    CHECK(note.position == core::FretPosition{.string = 6, .fret = 3});
    CHECK(note.pitch.midiNumber() == 40 + 5 + 3);
}

TEST_CASE("Guitar Pro: a bar whose clef differs from the bar before changes it",
          "[io][guitarpro][clef]") {
    // ADR-0087: the change keeps the staff's octave (the guitar's treble clef
    // sounds an octave down, and so does its bass clef); a neutral clef on a
    // pitched staff is dropped.
    const std::string beat = R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>)";
    const std::string whole = R"(<Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm>)";
    std::string score = guitarScore(0, "0", beat, whole);
    const std::array clefs{"F4", "F4", "G2", "Neutral"};
    std::string masterBars;
    std::string bars;
    for (std::size_t m = 1; m <= clefs.size(); ++m) {
        masterBars += std::format("<MasterBar><Time>4/4</Time><Bars>{}</Bars></MasterBar>", m);
        bars += std::format(R"(<Bar id="{}"><Clef>{}</Clef><Voices>-1 -1 -1 -1</Voices></Bar>)", m,
                            clefs[m - 1]);
    }
    score.insert(score.find("</MasterBars>"), masterBars);
    score.insert(score.find("</Bars><Voices>"), bars);
    const auto imported = importGpif(score);
    REQUIRE(imported.has_value());
    const core::Staff& staff = imported->song.track(0).staff(0);
    REQUIRE(imported->song.measureCount() == 5);
    CHECK(staff.clef() == core::Clef::Treble8vb);
    CHECK(staff.measure(0).clef == std::nullopt);
    CHECK(staff.measure(1).clef == core::Clef::Bass8vb);
    CHECK(staff.measure(2).clef == std::nullopt);
    CHECK(staff.measure(3).clef == core::Clef::Treble8vb);
    CHECK(staff.measure(4).clef == std::nullopt);
    CHECK(imported->warnings == std::vector<std::string>{"Gtr: clef changes dropped (measure 5)"});
}

TEST_CASE("Guitar Pro: a tapped note taps its beat", "[io][guitarpro]") {
    // ADR-0060: the model taps beats, Guitar Pro notes, with either hand.
    const std::string beats = R"(<Beat id="0"><Rhythm ref="0"/><Notes>1</Notes></Beat>)"
                              R"(<Beat id="1"><Rhythm ref="0"/><Notes>0</Notes></Beat>)"
                              R"(<Beat id="2"><Rhythm ref="0"/><Notes>2</Notes></Beat>)"
                              R"(<Beat id="3"><Rhythm ref="0"/><Notes>0</Notes></Beat>)";
    const std::string quarter = R"(<Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>)";
    const auto tappedNote = [](int id, const char* property) {
        return std::format(R"(<Note id="{}"><Properties><Property name="Fret"><Fret>12</Fret>)"
                           R"(</Property><Property name="String"><String>5</String></Property>)"
                           R"(<Property name="{}"><Enable /></Property></Properties></Note>)",
                           id, property);
    };
    std::string score = guitarScore(0, "0 1 2 3", beats, quarter);
    score.insert(score.find("</Notes><Rhythms>"),
                 tappedNote(1, "Tapped") + tappedNote(2, "LeftHandTapped"));
    const auto imported = importGpif(score);
    REQUIRE(imported.has_value());
    CHECK(imported->warnings.empty());
    const auto& read = imported->song.track(0).staff(0).measure(0).voices[0].beats;
    REQUIRE(read.size() == 4);
    CHECK(read[0].effects.bassTechnique == core::BassTechnique::Tap);
    CHECK(read[0].notes.at(0).position == core::FretPosition{.string = 1, .fret = 12});
    CHECK(read[1].effects.bassTechnique == core::BassTechnique::None);
    CHECK(read[2].effects.bassTechnique == core::BassTechnique::Tap);
    CHECK(read[3].effects.bassTechnique == core::BassTechnique::None);
}

TEST_CASE("Guitar Pro: beats keep their free text", "[io][guitarpro]") {
    // ADR-0081: the text as written; an empty one is none.
    const std::string beats =
        R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes><FreeText><![CDATA[let ring]]></FreeText></Beat>)"
        R"(<Beat id="1"><Rhythm ref="0"/><FreeText><![CDATA[]]></FreeText></Beat>)";
    const std::string half = R"(<Rhythm id="0"><NoteValue>Half</NoteValue></Rhythm>)";
    const auto imported = importGpif(guitarScore(0, "0 1", beats, half));
    REQUIRE(imported.has_value());
    CHECK(imported->warnings.empty());
    const auto& read = imported->song.track(0).staff(0).measure(0).voices[0].beats;
    REQUIRE(read.size() == 2);
    REQUIRE(read[0].text);
    CHECK(*read[0].text == "let ring");
    CHECK_FALSE(read[1].text);
}

TEST_CASE("Guitar Pro: beats keep the syllables Guitar Pro dispatched to them", "[io][guitarpro]") {
    // ADR-0084: once dispatched, the beats hold the lyrics and the track's text
    // is not placed again.
    const std::string lyrics =
        R"(<Lyrics dispatched="true"><Line><Text><![CDATA[not this]]></Text><Offset>0</Offset>)"
        R"(</Line></Lyrics><InstrumentSet>)";
    const std::string beats =
        R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes><Lyrics><Line><![CDATA[be-]]></Line>)"
        R"(<Line><![CDATA[]]></Line><Line><![CDATA[low]]></Line><Line><![CDATA[]]></Line>)"
        R"(</Lyrics></Beat>)"
        R"(<Beat id="1"><Rhythm ref="0"/><Notes>0</Notes><Lyrics><Line><![CDATA[hind]]></Line>)"
        R"(<Line><![CDATA[]]></Line></Lyrics></Beat>)";
    const std::string half = R"(<Rhythm id="0"><NoteValue>Half</NoteValue></Rhythm>)";
    std::string score = guitarScore(0, "0 1", beats, half);
    score.replace(score.find("<InstrumentSet>"), 15, lyrics);
    const auto imported = importGpif(score);
    REQUIRE(imported.has_value());
    CHECK(imported->warnings.empty());
    const auto& read = imported->song.track(0).staff(0).measure(0).voices[0].beats;
    REQUIRE(read.size() == 2);
    using Verses = std::vector<core::Syllable>;
    REQUIRE(read[0].lyrics);
    CHECK(*read[0].lyrics ==
          Verses{{.text = "be", .hyphen = true}, {}, {.text = "low", .hyphen = false}});
    REQUIRE(read[1].lyrics);
    CHECK(*read[1].lyrics == Verses{{.text = "hind", .hyphen = false}});
}

TEST_CASE("Guitar Pro: lyrics not yet dispatched go on the beats with notes", "[io][guitarpro]") {
    // ADR-0084: past the rests, from the bar of each line (Offset 0 for the
    // first). This score has one bar: "late", from the second, finds no beat,
    // and neither does "three".
    const std::string lyrics =
        R"(<Lyrics><Line><Text><![CDATA[[Intro] one two three]]></Text><Offset>0</Offset></Line>)"
        R"(<Line><Text><![CDATA[late]]></Text><Offset>1</Offset></Line></Lyrics><InstrumentSet>)";
    const std::string beats = R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>)"
                              R"(<Beat id="1"><Rhythm ref="0"/></Beat>)";
    const std::string quarter = R"(<Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>)";
    std::string score = guitarScore(0, "0 1 0 1", beats, quarter);
    score.replace(score.find("<InstrumentSet>"), 15, lyrics);
    const auto imported = importGpif(score);
    REQUIRE(imported.has_value());
    CHECK(imported->warnings ==
          std::vector<std::string>{"Gtr: lyrics after the last note dropped"});
    const auto& read = imported->song.track(0).staff(0).measure(0).voices[0].beats;
    REQUIRE(read.size() == 4);
    using Verses = std::vector<core::Syllable>;
    REQUIRE(read[0].lyrics);
    CHECK(*read[0].lyrics == Verses{{.text = "one", .hyphen = false}});
    CHECK_FALSE(read[1].lyrics);
    REQUIRE(read[2].lyrics);
    CHECK(*read[2].lyrics == Verses{{.text = "two", .hyphen = false}});
    CHECK_FALSE(read[3].lyrics);
}

TEST_CASE("Guitar Pro: tempo changes sit at their share of the bar", "[io][guitarpro]") {
    const std::string beat = R"(<Beat id="0"><Rhythm ref="0"/></Beat>)";
    const std::string whole = R"(<Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm>)";
    const std::string tempos =
        "<Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>72.5 2</Value>"
        "</Automation><Automation><Type>Tempo</Type><Bar>0</Bar><Position>0.75</Position>"
        "<Value>50 4</Value></Automation>";
    const auto imported = importGpif(guitarScore(0, "0", beat, whole, tempos));
    REQUIRE(imported.has_value());
    CHECK(imported->song.startTempo() == core::Tempo(72.5));
    const auto& changes = imported->song.measureHeader(0).tempoChanges;
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].offset == core::Fraction(3, 4));
    CHECK(changes[0].tempo == core::Tempo(50, Duration(NoteValue::Half)));
}

TEST_CASE("Guitar Pro: a linear tempo point moves to the next one", "[io][guitarpro]") {
    // ADR-0082: Guitar Pro repeats the tempo in force at the linear point;
    // the next point's tempo is reached where it stands.
    const std::string beat = R"(<Beat id="0"><Rhythm ref="0"/></Beat>)";
    const std::string whole = R"(<Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm>)";
    const std::string tempos =
        "<Automation><Type>Tempo</Type><Linear>false</Linear><Bar>0</Bar><Position>0</Position>"
        "<Value>72 2</Value></Automation><Automation><Type>Tempo</Type><Linear>true</Linear>"
        "<Bar>0</Bar><Position>0.5</Position><Value>72 2</Value></Automation>"
        "<Automation><Type>Tempo</Type><Linear>false</Linear><Bar>0</Bar>"
        "<Position>0.75</Position><Value>80 2</Value></Automation>";
    const auto imported = importGpif(guitarScore(0, "0", beat, whole, tempos));
    REQUIRE(imported.has_value());
    CHECK(imported->warnings.empty());
    CHECK(imported->song.startTempo() == core::Tempo(72));
    const auto& changes = imported->song.measureHeader(0).tempoChanges;
    REQUIRE(changes.size() == 2);
    CHECK(changes[0] == core::TempoChange{.offset = core::Fraction(1, 2),
                                          .tempo = core::Tempo(72),
                                          .gradual = true});
    CHECK(changes[1] ==
          core::TempoChange{.offset = core::Fraction(3, 4), .tempo = core::Tempo(80)});

    // A linear point at the very start is a gradual change there.
    const std::string fromStart =
        "<Automation><Type>Tempo</Type><Linear>true</Linear><Bar>0</Bar><Position>0</Position>"
        "<Value>72 2</Value></Automation>";
    const auto started = importGpif(guitarScore(0, "0", beat, whole, fromStart));
    REQUIRE(started.has_value());
    CHECK(started->song.startTempo() == core::Tempo(72));
    REQUIRE(started->song.measureHeader(0).tempoChanges.size() == 1);
    CHECK(started->song.measureHeader(0).tempoChanges[0].gradual);
}

TEST_CASE("Guitar Pro: other versions and damaged files are errors with a reason",
          "[io][guitarpro]") {
    const auto error = [](std::span<const std::uint8_t> bytes) {
        const auto imported = importGuitarPro(bytes);
        REQUIRE_FALSE(imported.has_value());
        return imported.error().message;
    };
    CHECK(error(bytesOf("\x18"
                        "FICHIER GUITAR PRO v5.00")) ==
          "Guitar Pro: damaged file: it ends early (byte 25)");
    CHECK(error(bytesOf("BCFZ....")) ==
          "Guitar Pro: damaged Guitar Pro 6 file: it claims more than 64 MB");
    CHECK(error(bytesOf("hello")) == "Guitar Pro: not a Guitar Pro file");
    CHECK(error(zipOf({{"other.txt", "x"}})) ==
          "Guitar Pro: no Content/score.gpif in the archive (not a Guitar Pro 7 or 8 file)");
    CHECK(error(zipOf({{"Content/score.gpif", "<GPIF><Score>"}}))
              .starts_with("Guitar Pro: damaged score: "));

    const std::string missingRhythm = R"(<Beat id="0"><Rhythm ref="9"/></Beat>)";
    const auto imported = importGpif(guitarScore(0, "0", missingRhythm, ""));
    REQUIRE_FALSE(imported.has_value());
    CHECK(imported.error().message == "Guitar Pro: measure 1 refers to missing rhythm 9");
}

TEST_CASE("Guitar Pro: the result does not depend on the number of threads", "[io][guitarpro]") {
    for (const char* name : {"band.gp", "guitar-techniques.gp", "duet.gpx"}) {
        const auto one = importGuitarProFile(corpusDir() / name, {.threads = 1});
        const auto many = importGuitarProFile(corpusDir() / name, {.threads = 8});
        REQUIRE(one.has_value());
        REQUIRE(many.has_value());
        CHECK(one->song == many->song);
        CHECK(one->warnings == many->warnings);
    }

    // Two tracks, two measures: the second track's first bar and the first
    // track's second bar name missing voices. The first in reading order wins.
    const std::string damaged =
        R"(<GPIF><Score /><Tracks><Track id="0"><Name>A</Name></Track>)"
        R"(<Track id="1"><Name>B</Name></Track></Tracks><MasterBars>)"
        "<MasterBar><Time>4/4</Time><Bars>0 1</Bars></MasterBar>"
        "<MasterBar><Time>4/4</Time><Bars>2 3</Bars></MasterBar></MasterBars><Bars>"
        R"(<Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar><Bar id="1"><Voices>9</Voices></Bar>)"
        R"(<Bar id="2"><Voices>8</Voices></Bar><Bar id="3"><Voices>0</Voices></Bar></Bars>)"
        R"(<Voices><Voice id="0"><Beats>0</Beats></Voice></Voices><Beats><Beat id="0">)"
        R"(<Rhythm ref="0"/></Beat></Beats><Rhythms><Rhythm id="0"><NoteValue>Whole</NoteValue>)"
        "</Rhythm></Rhythms></GPIF>";
    for (const unsigned threads : {1U, 2U}) {
        const auto imported = importGpif(damaged, {.threads = threads});
        REQUIRE_FALSE(imported.has_value());
        CHECK(imported.error().message == "Guitar Pro: measure 1 refers to missing voice 9");
    }
}
