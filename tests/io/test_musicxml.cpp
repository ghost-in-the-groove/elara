// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/validation.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = elara::core;
using elara::io::importMusicXml;
using elara::io::importMusicXmlBytes;
using elara::io::importMusicXmlFile;
using elara::io::ImportResult;

namespace {

std::filesystem::path corpusDir() {
    return std::filesystem::path(ELARA_CORPUS_DIR) / "musicxml";
}

ImportResult load(const char* name) {
    auto result = importMusicXmlFile(corpusDir() / name);
    if (!result) {
        FAIL(result.error().message);
    }
    return std::move(*result);
}

ImportResult parse(std::string_view xml) {
    auto result = importMusicXml(xml);
    if (!result) {
        FAIL(result.error().message);
    }
    return std::move(*result);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// "4=C4 8*3:2=B2 2=C5+E5 4=r": duration, then the pitches of each beat.
std::string summary(const core::Voice& voice) {
    std::string text;
    for (const core::Beat& beat : voice.beats) {
        if (!text.empty()) {
            text += ' ';
        }
        text += beat.duration.toString() + '=';
        if (beat.isRest()) {
            text += 'r';
        }
        for (std::size_t n = 0; n < beat.notes.size(); ++n) {
            text += (n > 0 ? "+" : "") + beat.notes[n].pitch.toString();
        }
    }
    return text;
}

// The verses sung on each beat, spelled as in song.json: "be-" joins the next
// syllable, "low_" is held over the notes after it, "" sings nothing.
std::vector<std::vector<std::string>> lyricsOf(const core::Voice& voice) {
    std::vector<std::vector<std::string>> out;
    for (const core::Beat& beat : voice.beats) {
        std::vector<std::string>& verses = out.emplace_back();
        if (beat.lyrics) {
            for (const core::Syllable& syllable : *beat.lyrics) {
                verses.push_back(syllable.text + (syllable.hyphen ? "-" : "") +
                                 (syllable.extend ? "_" : ""));
            }
        }
    }
    return out;
}

const core::Voice& voiceOf(const core::Song& song, std::size_t track, std::size_t staff,
                           std::size_t measure, std::size_t voice = 0) {
    return song.track(track).staff(staff).measure(measure).voices.at(voice);
}

const core::Note& noteOf(const core::Song& song, std::size_t measure, std::size_t beat) {
    return voiceOf(song, 0, 0, measure).beats.at(beat).notes.at(0);
}

bool hasWarning(const ImportResult& result, std::string_view feature, std::string_view where) {
    return std::ranges::any_of(result.warnings, [&](const std::string& warning) {
        return warning.find(feature) != std::string::npos &&
               warning.find(where) != std::string::npos;
    });
}

// A one-part score with the given measures (after a 4/4, divisions 4 header).
std::string onePart(std::string_view measures) {
    return std::string(R"(<score-partwise version="4.0"><part-list>)"
                       R"(<score-part id="P1"><part-name>Test</part-name></score-part>)"
                       R"(</part-list><part id="P1"><measure number="1"><attributes>)"
                       R"(<divisions>4</divisions><time><beats>4</beats><beat-type>4</beat-type>)"
                       R"(</time></attributes>)") +
           std::string(measures) + "</measure></part></score-partwise>";
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

} // namespace

TEST_CASE("MusicXML: a melody keeps its info, measure headers, tempo and notes", "[musicxml]") {
    const ImportResult result = load("melody.musicxml");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());

    CHECK(song.info().title == "Corpus Melody");
    CHECK(song.info().music == "Ghost in the Groove");
    CHECK(song.info().copyright == "CC0 1.0");

    REQUIRE(song.trackCount() == 1);
    const core::Track& track = song.track(0);
    CHECK(track.name() == "Flute");
    CHECK(track.instrument().kind == core::InstrumentKind::Pitched);
    CHECK(track.instrument().midiProgram == 73);
    CHECK(track.instrument().writtenOffset == 0);
    REQUIRE(track.staffCount() == 1);
    CHECK(track.staff(0).clef() == core::Clef::Treble);
    CHECK_FALSE(track.staff(0).isFretted());

    REQUIRE(song.measureCount() == 4);
    CHECK(song.measureHeader(0).timeSignature == core::TimeSignature(4, 4));
    CHECK(song.measureHeader(0).keySignature == core::KeySignature());
    CHECK(song.measureHeader(1).timeSignature == core::TimeSignature(4, 4));
    CHECK(song.measureHeader(2).timeSignature == core::TimeSignature(3, 4));
    CHECK(song.measureHeader(2).keySignature == core::KeySignature(1, core::KeyMode::Major));
    CHECK(song.measureHeader(3).timeSignature == core::TimeSignature(3, 4));
    CHECK(song.startTempo() == core::Tempo(100));
    CHECK(song.measureHeader(0).tempoChanges.empty());

    CHECK(summary(voiceOf(song, 0, 0, 0)) == "4=C4 4=D4 8=E4 8=F#4 4=G4");
    CHECK(summary(voiceOf(song, 0, 0, 1)) == "2=A4 4=r 4=Bb4");
    CHECK(summary(voiceOf(song, 0, 0, 2)) == "4.=B4 8=A4 4=G4");
    CHECK(voiceOf(song, 0, 0, 3).isEmpty()); // whole-measure rest
}

TEST_CASE("MusicXML: a guitar's TAB staff is merged into one fretted staff", "[musicxml]") {
    const ImportResult result = load("guitar-tab.musicxml");
    const core::Song& song = result.song;
    CHECK(result.warnings.empty());

    REQUIRE(song.trackCount() == 1);
    const core::Track& track = song.track(0);
    CHECK(track.name() == "Electric Guitar");
    CHECK(track.instrument().kind == core::InstrumentKind::Fretted);
    CHECK(track.instrument().midiProgram == 27);
    CHECK(track.instrument().writtenOffset == 12); // like the electric-guitar preset
    REQUIRE(track.staffCount() == 1);
    const core::Staff& staff = track.staff(0);
    CHECK(staff.clef() == core::Clef::Treble8vb);
    CHECK(staff.tuning() == core::Tuning::preset("guitar-drop-d"));
    CHECK(staff.capo() == 2);
    CHECK(song.measureHeader(0).keySignature == core::KeySignature(2, core::KeyMode::Major));

    CHECK(summary(voiceOf(song, 0, 0, 0)) == "4=E2 8*3:2=B2 8*3:2=C#3 8*3:2=E3 4=B3 4=C#5");
    CHECK(voiceOf(song, 0, 0, 0, 1).isEmpty()); // the TAB voice is not a second voice
    const std::vector<core::FretPosition> expected{{6, 0}, {5, 0}, {5, 2}, {4, 0}, {3, 2}, {2, 12}};
    for (std::size_t b = 0; b < expected.size(); ++b) {
        CAPTURE(b);
        CHECK(noteOf(song, 0, b).position == expected[b]);
    }

    // Measure 2 has string and fret on the TAB staff only.
    CHECK(summary(voiceOf(song, 0, 0, 1)) == "2=A4 4=A4 4=E2");
    CHECK(noteOf(song, 1, 0).position == core::FretPosition{1, 3});
    CHECK(noteOf(song, 1, 1).position == core::FretPosition{1, 3});
    CHECK(noteOf(song, 1, 2).position == core::FretPosition{6, 0});
}

TEST_CASE("MusicXML: guitar notations become note effects", "[musicxml]") {
    const core::Song song = load("guitar-tab.musicxml").song;

    CHECK(noteOf(song, 0, 0).effects.staccato);
    CHECK(noteOf(song, 0, 0).effects.slideOut == core::SlideOut::Legato);
    CHECK_FALSE(noteOf(song, 0, 1).effects.legatoFromPrevious);
    CHECK(noteOf(song, 0, 2).effects.legatoFromPrevious); // hammer-on
    CHECK(noteOf(song, 0, 3).effects.accent == core::Accent::Accent);
    CHECK(noteOf(song, 0, 4).effects.bend == core::BendCurve::bendRelease(200));
    CHECK(noteOf(song, 0, 5).effects.harmonic ==
          core::Harmonic{.kind = core::HarmonicKind::Natural, .fretTenths = 120});

    CHECK_FALSE(noteOf(song, 1, 0).effects.tiedFromPrevious);
    CHECK(noteOf(song, 1, 1).effects.tiedFromPrevious);
    CHECK(noteOf(song, 1, 2).effects.deadNote);
    // The ff comes after the first note of measure 2.
    CHECK(noteOf(song, 0, 0).effects.dynamic == core::Dynamic::MF);
    CHECK(noteOf(song, 1, 0).effects.dynamic == core::Dynamic::MF);
    CHECK(noteOf(song, 1, 1).effects.dynamic == core::Dynamic::FF);
    CHECK(noteOf(song, 1, 2).effects.dynamic == core::Dynamic::FF);
}

TEST_CASE("MusicXML: voices are renumbered per staff and late voices padded", "[musicxml]") {
    const ImportResult result = load("piano-voices.musicxml");
    const core::Song& song = result.song;

    CHECK(song.info().title == "Corpus Piano");
    REQUIRE(song.trackCount() == 1);
    const core::Track& track = song.track(0);
    CHECK(track.instrument().kind == core::InstrumentKind::Keyboard);
    CHECK(track.instrument().midiProgram == 0);
    REQUIRE(track.staffCount() == 2);
    CHECK(track.staff(0).clef() == core::Clef::Treble);
    CHECK(track.staff(1).clef() == core::Clef::Bass);

    REQUIRE(song.measureCount() == 2);
    CHECK(song.measureHeader(0).keySignature == core::KeySignature(-1, core::KeyMode::Major));
    CHECK(song.measureHeader(1).timeSignature == core::TimeSignature(6, 8));
    CHECK(song.startTempo() == core::Tempo());
    CHECK(song.measureHeader(1).tempoChanges ==
          std::vector<core::TempoChange>{{.offset = 0, .tempo = core::Tempo(80)}});

    CHECK(summary(voiceOf(song, 0, 0, 0, 0)) == "2=C5+E5 2=F5");
    CHECK(summary(voiceOf(song, 0, 0, 0, 1)) == "2=r 2=A4"); // starts after a forward
    CHECK(summary(voiceOf(song, 0, 1, 0, 0)) == "1=F3");     // voice 5 -> staff 2, voice 1
    CHECK(summary(voiceOf(song, 0, 0, 1, 0)) == "4.=C5 4.=r");
    CHECK(voiceOf(song, 0, 0, 1, 1).isEmpty());
    CHECK(summary(voiceOf(song, 0, 1, 1, 0)) == "2.=F3");
    CHECK(voiceOf(song, 0, 1, 1, 0).beats[0].graces.size() == 1); // ADR-0079
}

TEST_CASE("MusicXML: what the model cannot hold yet is reported, not lost silently", "[musicxml]") {
    const ImportResult result = load("piano-voices.musicxml");
    CHECK_FALSE(hasWarning(result, "chord symbol", "measure 1"));
    const core::Beat& first = voiceOf(result.song, 0, 0, 0).beats[0];
    REQUIRE(first.chord);
    CHECK(first.chord->name == "F");

    // Words are the beat's text (ADR-0081), lyrics its syllables (ADR-0084).
    CHECK_FALSE(hasWarning(result, "text", "measure 2"));
    REQUIRE(voiceOf(result.song, 0, 0, 1).beats.at(0).text);
    CHECK(*voiceOf(result.song, 0, 0, 1).beats[0].text == "Calmer");
    CHECK_FALSE(hasWarning(result, "lyric", "measure 2"));
    CHECK(lyricsOf(voiceOf(result.song, 0, 0, 1)) ==
          std::vector<std::vector<std::string>>{{"la"}, {}});
}

TEST_CASE("MusicXML: words are the text of the beat they start on", "[musicxml]") {
    // ADR-0081: words of one direction join; two directions on one beat join
    // with a space; words next to a tempo name it; words between beats are
    // dropped.
    const std::string quarter =
        R"(<duration>4</duration><voice>1</voice><type>quarter</type></note>)";
    const ImportResult result = parse(onePart(
        R"(<direction placement="above"><direction-type><words>let </words></direction-type>)"
        R"(<direction-type><words>ring</words></direction-type></direction>)"
        R"(<note><pitch><step>A</step><octave>4</octave></pitch>)" +
        quarter +
        R"(<direction><direction-type><words>Allegro</words></direction-type>)"
        R"(<direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>132</per-minute>)"
        R"(</metronome></direction-type></direction>)"
        R"(<direction placement="below"><direction-type><words>dolce</words></direction-type></direction>)"
        R"(<note><pitch><step>B</step><octave>4</octave></pitch>)" +
        quarter + R"(<note><rest/>)" + quarter +
        R"(<direction><direction-type><words>late</words></direction-type><offset>2</offset></direction>)"
        R"(<note><pitch><step>C</step><octave>5</octave></pitch>)" +
        quarter));
    const auto& beats = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(beats.size() == 4);
    REQUIRE(beats[0].text);
    CHECK(*beats[0].text == "let ring");
    REQUIRE(beats[1].text);
    CHECK(*beats[1].text == "Allegro dolce");
    CHECK(result.song.tempoAt(0, core::Fraction(1, 4)) == core::Tempo(132));
    CHECK_FALSE(beats[2].text);
    CHECK_FALSE(beats[3].text);
    CHECK(hasWarning(result, "text directions dropped", "measure 1"));
}

// A quarter note of voice 1 in octave 4 with what follows its type.
std::string quarterNote(std::string_view step, std::string_view rest) {
    return std::format("<note><pitch><step>{}</step><octave>4</octave></pitch>"
                       "<duration>4</duration><voice>1</voice><type>quarter</type>{}</note>",
                       step, rest);
}

TEST_CASE("MusicXML: tenuto marks hold their notes, detached-legato is portato", "[musicxml]") {
    // ADR-0088: a tenuto with a staccato, as one mark or two, is portato.
    const auto marked = [](std::string_view step, std::string_view marks) {
        return quarterNote(step, std::format("<notations><articulations>{}</articulations>"
                                             "</notations>",
                                             marks));
    };
    const ImportResult result =
        parse(onePart(marked("C", "<tenuto/>") + marked("D", "<detached-legato/>") +
                      marked("E", "<staccato/><tenuto/>") + quarterNote("F", "")));
    const std::vector<core::Beat>& beats = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(beats.size() == 4);
    const auto marks = [&](std::size_t b) {
        const core::NoteEffects& fx = beats[b].notes.at(0).effects;
        return std::pair(fx.tenuto, fx.staccato);
    };
    CHECK(marks(0) == std::pair(true, false));
    CHECK(marks(1) == std::pair(true, true));
    CHECK(marks(2) == std::pair(true, true));
    CHECK(marks(3) == std::pair(false, false));
    CHECK(result.warnings.empty());
}

TEST_CASE("MusicXML: mordents and turns are the notes' ornaments", "[musicxml]") {
    // ADR-0091: MusicXML's inverted mordent is the upper one; a delayed turn
    // is none of them.
    const auto marked = [](std::string_view ornaments) {
        return std::format("<note><pitch><step>E</step><octave>5</octave></pitch>"
                           "<duration>4</duration><voice>1</voice><type>quarter</type>"
                           "<notations><ornaments>{}</ornaments></notations></note>",
                           ornaments);
    };
    const ImportResult result =
        parse(onePart(marked("<mordent/>") + marked("<inverted-mordent/>") + marked("<turn/>") +
                      marked("<inverted-turn/><delayed-turn/>")));
    const std::vector<core::Beat>& beats = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(beats.size() == 4);
    const auto ornamentOf = [&](std::size_t b) {
        return beats[b].notes.at(0).effects.ornament;
    };
    CHECK(ornamentOf(0) == core::Ornament::Mordent);
    CHECK(ornamentOf(1) == core::Ornament::UpperMordent);
    CHECK(ornamentOf(2) == core::Ornament::Turn);
    CHECK(ornamentOf(3) == core::Ornament::InvertedTurn);
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].find("ornaments dropped") != std::string::npos);
}

TEST_CASE("MusicXML: trill marks trill to the next step of the key", "[musicxml]") {
    // ADR-0090: in D major E trills with F#, two semitones up, and C# with D,
    // one; an accidental mark changes the other note; the wavy line after a
    // trill mark is the trill's, not a vibrato.
    const auto trilled = [](std::string_view step, std::string_view alter,
                            std::string_view ornaments) {
        return std::format("<note><pitch><step>{}</step>{}<octave>5</octave></pitch>"
                           "<duration>4</duration><voice>1</voice><type>quarter</type>"
                           "<notations><ornaments>{}</ornaments></notations></note>",
                           step, alter, ornaments);
    };
    std::string xml = onePart(
        "<attributes><key><fifths>2</fifths></key></attributes>" +
        trilled("E", "", "<trill-mark/>") +
        trilled("C", "<alter>1</alter>", R"(<trill-mark/><wavy-line type="start"/>)") +
        trilled("C", "<alter>1</alter>", "<trill-mark/><accidental-mark>sharp</accidental-mark>") +
        trilled("G", "", "<schleifer/>"));
    const ImportResult result = parse(xml);
    const std::vector<core::Beat>& beats = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(beats.size() == 4);
    const auto trillOf = [&](std::size_t b) {
        return beats[b].notes.at(0).effects.trill;
    };
    CHECK(trillOf(0) == core::Trill{.semitones = 2, .speed = core::NoteValue::ThirtySecond});
    CHECK(trillOf(1).semitones == 1);
    CHECK(beats[1].notes.at(0).effects.vibrato == core::Vibrato::None);
    CHECK(trillOf(2).semitones == 2); // to D#
    CHECK(!trillOf(3).isTrill());
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].find("ornaments dropped") != std::string::npos);
}

TEST_CASE("MusicXML: octave shifts become the octave lines of the beats under them", "[musicxml]") {
    // ADR-0089: "down" writes the notes an octave (size 15: two) lower than
    // their pitch, 8va; every voice's beats from the start to the stop take
    // the line, which carries on into the next measure; pitches stay.
    const auto shift = [](std::string_view attributes) {
        return std::format("<direction><direction-type><octave-shift {}/></direction-type>"
                           "</direction>",
                           attributes);
    };
    const std::string lowerVoice =
        "<backup><duration>16</duration></backup>"
        "<note><pitch><step>G</step><octave>3</octave></pitch><duration>8</duration>"
        "<voice>2</voice><type>half</type></note>"
        "<note><pitch><step>B</step><octave>3</octave></pitch><duration>8</duration>"
        "<voice>2</voice><type>half</type></note>";
    const ImportResult result = parse(
        onePart(quarterNote("C", "") + shift(R"(type="down" size="8")") + quarterNote("D", "") +
                quarterNote("E", "") + quarterNote("F", "") + lowerVoice +
                R"(</measure><measure number="2">)" + quarterNote("G", "") +
                shift(R"(type="stop" size="8")") + quarterNote("A", "") +
                shift(R"(type="up" size="15")") + quarterNote("B", "") + quarterNote("C", "")));
    const auto lines = [&](std::size_t measure, std::size_t voice) {
        std::vector<core::Ottava> found;
        for (const core::Beat& beat : voiceOf(result.song, 0, 0, measure, voice).beats) {
            found.push_back(beat.effects.ottava);
        }
        return found;
    };
    using enum core::Ottava;
    CHECK(lines(0, 0) == std::vector{None, Alta, Alta, Alta});
    CHECK(lines(0, 1) == std::vector{None, Alta});
    CHECK(lines(1, 0) == std::vector{Alta, None, QuindicesimaBassa, QuindicesimaBassa});
    CHECK(voiceOf(result.song, 0, 0, 0).beats[1].notes.at(0).pitch ==
          core::Pitch(core::Step::D, 0, 4));
    CHECK(result.warnings.empty());
}

TEST_CASE("MusicXML: lyrics give each beat a syllable per verse", "[musicxml]") {
    // ADR-0084: <lyric number> is the verse; begin and middle syllables join
    // the next one with a hyphen; elided syllables share the beat; <extend/>
    // holds the syllable over the notes after it.
    const ImportResult result = parse(onePart(
        quarterNote("C",
                    R"(<lyric number="1"><syllabic>begin</syllabic><text>be</text></lyric>)"
                    R"(<lyric number="2"><syllabic>single</syllabic><text>oh</text></lyric>)") +
        quarterNote("D", R"(<lyric number="1"><syllabic>end</syllabic><text>low</text>)"
                         R"(<extend type="start"/></lyric>)") +
        quarterNote("E", R"(<lyric number="1"><extend type="stop"/></lyric>)") +
        quarterNote("F",
                    R"(<lyric number="3"><syllabic>begin</syllabic><text>ti</text>)"
                    R"(<elision>‿</elision><syllabic>middle</syllabic><text>a</text></lyric>)")));
    CHECK(lyricsOf(voiceOf(result.song, 0, 0, 0)) ==
          std::vector<std::vector<std::string>>{{"be-", "oh"}, {"low_"}, {}, {"", "", "ti‿a-"}});
    CHECK(result.warnings.empty());
}

TEST_CASE("MusicXML: lyrics without a number, on chords, hidden or on grace notes", "[musicxml]") {
    // No number is verse 1; a chord note's syllable goes on the beat when its
    // verse has none there yet; a hyphen typed after the text joins the next
    // syllable; hidden lyrics are not sung.
    const ImportResult result = parse(onePart(
        quarterNote("C", R"(<lyric name="verse"><text>one</text></lyric>)") +
        quarterNote("D", R"(<lyric number="1"><text>two</text></lyric>)") +
        R"(<note><chord/><pitch><step>F</step><octave>4</octave></pitch><duration>4</duration>)"
        R"(<voice>1</voice><type>quarter</type><lyric number="1"><text>dos</text></lyric>)"
        R"(<lyric number="2"><syllabic>begin</syllabic><text>deux-</text></lyric></note>)" +
        quarterNote("E", R"(<lyric number="1" print-object="no"><text>hidden</text></lyric>)"
                         R"(<lyric number="17"><text>far</text></lyric>)") +
        R"(<note><grace/><pitch><step>A</step><octave>4</octave></pitch><voice>1</voice>)"
        R"(<type>eighth</type><lyric number="1"><text>la</text></lyric></note>)" +
        quarterNote("G", R"(<lyric number="1"><text>end-</text></lyric>)")));
    CHECK(lyricsOf(voiceOf(result.song, 0, 0, 0)) ==
          std::vector<std::vector<std::string>>{{"one"}, {"two", "deux-"}, {}, {"end-"}});
    CHECK(result.warnings ==
          std::vector<std::string>{"Test: verses after the 16th dropped (measure 1)",
                                   "Test: lyrics on grace notes dropped (measure 1)"});

    // Verses named instead of numbered follow the order the part names them in.
    const ImportResult named = parse(
        onePart(quarterNote("C", R"(<lyric number="v-a"><text>a</text></lyric>)"
                                 R"(<lyric number="v-b"><text>b</text></lyric>)") +
                quarterNote("D", R"(<lyric number="v-b"><text>c</text></lyric>)") +
                R"(<note><rest/><duration>8</duration><voice>1</voice><type>half</type></note>)"));
    CHECK(lyricsOf(voiceOf(named.song, 0, 0, 0)) ==
          std::vector<std::vector<std::string>>{{"a", "b"}, {"", "c"}, {}});
}

TEST_CASE("MusicXML: rit. and accel. make the tempo change there gradual", "[musicxml]") {
    // ADR-0082: the words start a ramp to the next tempo of the part, at the
    // tempo in force when they come alone; with no tempo before "a tempo", the
    // next ramp or the end, they stay the text of the beat.
    const auto half = [](std::string_view step) {
        return std::format(R"(<note><pitch><step>{}</step><octave>4</octave></pitch>)"
                           R"(<duration>8</duration><voice>1</voice><type>half</type></note>)",
                           step);
    };
    const auto metronome = [](int bpm) {
        return std::format(R"(<direction-type><metronome><beat-unit>quarter</beat-unit>)"
                           R"(<per-minute>{}</per-minute></metronome></direction-type>)",
                           bpm);
    };
    const auto words = [](std::string_view text) {
        return std::format(R"(<direction-type><words>{}</words></direction-type>)", text);
    };
    const auto measure = [](int number) {
        return std::format(R"(</measure><measure number="{}">)", number);
    };
    const std::string start = R"(<direction-type><dashes type="start"/></direction-type>)";
    const std::string stop = R"(<direction-type><dashes type="stop"/></direction-type>)";
    const ImportResult result = parse(onePart(
        "<direction>" + metronome(100) + "</direction>" + half("A") + "<direction>" +
        words("rit.") + start + "</direction>" + half("B") + measure(2) + "<direction>" + stop +
        metronome(60) + "</direction>" + half("C") + "<direction>" + metronome(80) +
        words("Accelerando") + "</direction>" + half("D") + measure(3) + "<direction>" +
        metronome(120) + "</direction>" + half("E") + "<direction>" + words("poco rall.") +
        "</direction>" + half("F") + measure(4) + "<direction>" + words("a tempo") +
        "</direction>" + half("G") + "<direction>" + metronome(90) + "</direction>" + half("A") +
        measure(5) + "<direction>" + words("rit.") + "</direction>" + half("B") + half("C")));

    CHECK(result.song.startTempo() == core::Tempo(100));
    const auto changes = [&](std::size_t m) {
        return result.song.measureHeader(m).tempoChanges;
    };
    REQUIRE(changes(0).size() == 1);
    CHECK(changes(0)[0] == core::TempoChange{.offset = core::Fraction(1, 2),
                                             .tempo = core::Tempo(100),
                                             .gradual = true});
    REQUIRE(changes(1).size() == 2);
    CHECK(changes(1)[0] == core::TempoChange{.tempo = core::Tempo(60)});
    CHECK(changes(1)[1] == core::TempoChange{.offset = core::Fraction(1, 2),
                                             .tempo = core::Tempo(80),
                                             .gradual = true});
    REQUIRE(changes(2).size() == 1);
    CHECK_FALSE(changes(2)[0].gradual);
    REQUIRE(changes(3).size() == 1);
    CHECK_FALSE(changes(3)[0].gradual);
    CHECK(changes(4).empty());

    const auto text = [&](std::size_t m, std::size_t b) {
        const auto& beat = voiceOf(result.song, 0, 0, m).beats.at(b);
        return beat.text ? std::string(*beat.text) : std::string("(none)");
    };
    CHECK(text(0, 1) == "(none)");
    CHECK(text(1, 1) == "(none)");
    CHECK(text(2, 1) == "poco rall.");
    CHECK(text(3, 0) == "a tempo");
    CHECK(text(4, 0) == "rit.");
}

TEST_CASE("MusicXML: chord symbols are named by root, kind, degrees and bass", "[musicxml]") {
    // ADR-0080. A frame on a part without a tuning keeps its own strings.
    const ImportResult result = parse(onePart(R"(
<harmony><root><root-step>F</root-step><root-alter>1</root-alter></root><kind>minor-seventh</kind>
<frame><frame-strings>6</frame-strings><frame-frets>4</frame-frets><first-fret>2</first-fret>
<frame-note><string>1</string><fret>2</fret></frame-note><frame-note><string>2</string><fret>2</fret></frame-note>
<frame-note><string>3</string><fret>2</fret></frame-note><frame-note><string>4</string><fret>4</fret></frame-note>
<frame-note><string>6</string><fret>2</fret></frame-note></frame></harmony>
<note><pitch><step>A</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>quarter</type></note>
<harmony><root><root-step>B</root-step><root-alter>-1</root-alter></root><kind text="7">dominant</kind>
<bass><bass-step>D</bass-step></bass><degree><degree-value>9</degree-value><degree-alter>-1</degree-alter>
<degree-type>alter</degree-type></degree></harmony>
<note><pitch><step>D</step><octave>5</octave></pitch><duration>4</duration><voice>1</voice><type>quarter</type></note>
<harmony><root><root-step>C</root-step></root><kind>none</kind></harmony>
<note><rest/><duration>4</duration><voice>1</voice><type>quarter</type></note>
<harmony><root><root-step>G</root-step></root><kind>suspended-fourth</kind><offset>2</offset></harmony>
<harmony><numeral><numeral-root>5</numeral-root></numeral><kind>dominant</kind></harmony>
<note><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>quarter</type></note>)"));
    const auto& beats = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(beats.size() == 4);
    REQUIRE(beats[0].chord);
    constexpr std::int8_t x = core::ChordDiagram::kMuted;
    CHECK(*beats[0].chord == core::ChordSymbol{.name = "F#m7",
                                               .diagram = core::ChordDiagram{
                                                   .firstFret = 2, .frets = {2, 2, 2, 4, x, 2}}});
    REQUIRE(beats[1].chord);
    CHECK(*beats[1].chord == core::ChordSymbol{.name = "Bb7b9/D", .diagram = {}});
    REQUIRE(beats[2].chord);
    CHECK(beats[2].chord->name == "N.C.");
    CHECK(beats[2].isRest());
    CHECK_FALSE(beats[3].chord);
    CHECK(result.warnings ==
          std::vector<std::string>{"Test: chord symbols written as functions dropped (measure 1)",
                                   "Test: chord symbols between beats dropped (measure 1)"});
}

TEST_CASE("MusicXML: grace notes lead into the next note of their voice", "[musicxml]") {
    // A slashed eighth before the beat, an unslashed chord of sixteenths on
    // it, and one after the last note with nothing to lead into (ADR-0079).
    const ImportResult result = parse(R"(<score-partwise version="4.0">
<part-list><score-part id="P1"><part-name>Flute</part-name></score-part></part-list>
<part id="P1"><measure number="1">
<attributes><divisions>2</divisions><time><beats>4</beats><beat-type>4</beat-type></time></attributes>
<note><grace slash="yes"/><pitch><step>D</step><octave>5</octave></pitch><voice>1</voice><type>eighth</type></note>
<note><pitch><step>C</step><octave>5</octave></pitch><duration>4</duration><voice>1</voice><type>half</type></note>
<note><grace/><pitch><step>E</step><octave>5</octave></pitch><voice>1</voice><type>16th</type></note>
<note><grace/><chord/><pitch><step>G</step><octave>5</octave></pitch><voice>1</voice><type>16th</type></note>
<note><pitch><step>F</step><octave>5</octave></pitch><duration>4</duration><voice>1</voice><type>half</type></note>
<note><grace slash="yes"/><pitch><step>A</step><octave>5</octave></pitch><voice>1</voice><type>eighth</type></note>
</measure></part></score-partwise>)");
    const core::Voice& voice = voiceOf(result.song, 0, 0, 0, 0);
    CHECK(summary(voice) == "2=C5 2=F5");
    REQUIRE(voice.beats[0].graces.size() == 1);
    const core::Grace& before = voice.beats[0].graces[0];
    CHECK(before.duration == core::Duration(core::NoteValue::Eighth));
    CHECK_FALSE(before.onBeat);
    REQUIRE(before.notes.size() == 1);
    CHECK(before.notes[0].pitch.toString() == "D5");
    REQUIRE(voice.beats[1].graces.size() == 1);
    const core::Grace& on = voice.beats[1].graces[0];
    CHECK(on.duration == core::Duration(core::NoteValue::Sixteenth));
    CHECK(on.onBeat);
    REQUIRE(on.notes.size() == 2);
    CHECK(on.notes[1].pitch.toString() == "G5");
    CHECK(result.warnings ==
          std::vector<std::string>{
              "Flute: grace notes after the last note of a voice dropped (measure 1)"});
}

// <midi-instrument> holds a part's playback volume, as a percentage, and its pan
// in degrees: -90 hard left, 90 hard right, beyond that behind the listener.
// The first instrument of a part sets the track's mix.
TEST_CASE("MusicXML: a part's MIDI volume and pan set its track's mix", "[musicxml]") {
    const auto part = [](int number) {
        return std::format(R"(<part id="P{}"><measure number="1"><attributes>)"
                           R"(<divisions>1</divisions><time><beats>4</beats>)"
                           R"(<beat-type>4</beat-type></time></attributes>)"
                           R"(<note><rest measure="yes"/><duration>4</duration></note>)"
                           R"(</measure></part>)",
                           number);
    };
    const std::string xml =
        std::string(R"(<score-partwise version="4.0"><part-list>)"
                    R"(<score-part id="P1"><part-name>Left</part-name>)"
                    R"(<midi-instrument id="P1-I1"><midi-program>1</midi-program>)"
                    R"(<volume>100</volume><pan>-90</pan></midi-instrument></score-part>)"
                    R"(<score-part id="P2"><part-name>Behind</part-name>)"
                    R"(<midi-instrument id="P2-I1"><volume>62.5</volume><pan>135</pan>)"
                    R"(</midi-instrument><midi-instrument id="P2-I2"><volume>10</volume>)"
                    R"(<pan>-45</pan></midi-instrument></score-part>)"
                    R"(<score-part id="P3"><part-name>Odd</part-name>)"
                    R"(<midi-instrument id="P3-I1"><volume>loud</volume><pan>-200</pan>)"
                    R"(</midi-instrument></score-part>)"
                    R"(<score-part id="P4"><part-name>Default</part-name></score-part>)"
                    R"(</part-list>)") +
        part(1) + part(2) + part(3) + part(4) + "</score-partwise>";
    const ImportResult result = parse(xml);
    CHECK(result.warnings.empty());
    const core::Song& song = result.song;
    REQUIRE(song.trackCount() == 4);
    CHECK(song.track(0).mix() == core::TrackMix{.volume = 1.0, .pan = -1.0});
    // 135 degrees is behind the right shoulder: as far right as 45 in front.
    CHECK(song.track(1).mix() == core::TrackMix{.volume = 0.625, .pan = 0.5});
    // A volume that is not a number keeps the default; -200 is out of range.
    CHECK(song.track(2).mix() == core::TrackMix{});
    CHECK(song.track(3).mix() == core::TrackMix{});
}

TEST_CASE("MusicXML: drum notes use the General MIDI keys of their instruments", "[musicxml]") {
    const core::Song song = load("drums.musicxml").song;
    REQUIRE(song.trackCount() == 1);
    const core::Track& track = song.track(0);
    CHECK(track.name() == "Drumset");
    CHECK(track.instrument().kind == core::InstrumentKind::Percussion);
    CHECK(track.staff(0).clef() == core::Clef::Percussion);

    const core::Voice& voice = voiceOf(song, 0, 0, 0);
    REQUIRE(voice.beats.size() == 4);
    const auto keys = [&](std::size_t beat) {
        std::vector<int> midi;
        for (const core::Note& note : voice.beats[beat].notes) {
            midi.push_back(note.pitch.midiNumber());
        }
        return midi;
    };
    CHECK(keys(0) == std::vector<int>{36}); // bass drum
    CHECK(keys(1) == std::vector<int>{38}); // snare
    CHECK(keys(2) == std::vector<int>{36, 38});
    CHECK(keys(3) == std::vector<int>{38});
}

TEST_CASE("MusicXML: transposing parts keep sounding pitch, concert key and written offset",
          "[musicxml]") {
    const ImportResult result = load("transposing.musicxml");
    const core::Song& song = result.song;
    REQUIRE(song.trackCount() == 2);
    REQUIRE(song.measureCount() == 2);
    CHECK(song.measureHeader(0).timeSignature == core::TimeSignature(3, 4));
    CHECK(song.measureHeader(0).keySignature == core::KeySignature()); // written D, concert C

    const core::Track& clarinet = song.track(0);
    CHECK(clarinet.name() == "Clarinet in Bb");
    CHECK(clarinet.instrument().midiProgram == 71);
    CHECK(clarinet.instrument().writtenOffset == 2);
    CHECK(clarinet.staff(0).clef() == core::Clef::Treble);
    // The pickup is filled with leading rests: the model has no partial measures.
    CHECK(summary(voiceOf(song, 0, 0, 0)) == "2=r 4=D5");
    CHECK(summary(voiceOf(song, 0, 0, 1)) == "2=E5 4=C5");

    const core::Track& bass = song.track(1);
    CHECK(bass.instrument().kind == core::InstrumentKind::Pitched);
    CHECK(bass.instrument().midiProgram == 33);
    CHECK(bass.instrument().writtenOffset == 12);
    CHECK(bass.staff(0).clef() == core::Clef::Bass8vb);
    CHECK(summary(voiceOf(song, 1, 0, 0)) == "2=r 4=A1");
    CHECK(summary(voiceOf(song, 1, 0, 1)) == "2.=D2");

    CHECK(hasWarning(result, "pickup", "measure 1"));
}

TEST_CASE("MusicXML: every corpus file imports to a valid song that the native format keeps",
          "[musicxml]") {
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpusDir())) {
        if (entry.path().extension() != ".musicxml") {
            continue;
        }
        ++files;
        CAPTURE(entry.path().filename().string());
        const core::Song song = load(entry.path().filename().string().c_str()).song;
        CHECK(core::validate(song).empty());
        const auto reread = elara::io::songFromJson(elara::io::songToJson(song));
        REQUIRE(reread.has_value());
        CHECK(*reread == song);
    }
    CHECK(files >= 5);
}

TEST_CASE("MusicXML: parts read on several threads give the same result as on one", "[musicxml]") {
    // Six parts; each drops something different, so warning order is checked too.
    constexpr std::array<std::string_view, 6> kExtras{
        "",
        "<harmony><numeral><numeral-root>5</numeral-root></numeral><kind>major</kind></harmony>",
        "<figured-bass><figure><figure-number>6</figure-number></figure></figured-bass>",
        "",
        R"(<direction><direction-type><octave-shift type="down" size="22"/></direction-type></direction>)",
        "<barline><fermata/></barline>"};
    std::string xml = R"(<score-partwise version="4.0"><part-list>)";
    for (std::size_t p = 0; p < kExtras.size(); ++p) {
        xml +=
            std::format(R"(<score-part id="P{0}"><part-name>Part {0}</part-name></score-part>)", p);
    }
    xml += "</part-list>";
    for (std::size_t p = 0; p < kExtras.size(); ++p) {
        xml += std::format(R"(<part id="P{}">)", p);
        for (int m = 0; m < 40; ++m) {
            xml += std::format(R"(<measure number="{}">)", m + 1);
            if (m == 0) {
                xml += "<attributes><divisions>1</divisions><time><beats>4</beats>"
                       "<beat-type>4</beat-type></time></attributes>";
            }
            if (m % 7 == 3) {
                xml += kExtras[p];
            }
            for (int b = 0; b < 4; ++b) {
                xml += std::format(R"(<note><pitch><step>{}</step><octave>{}</octave></pitch>)"
                                   "<duration>1</duration><type>quarter</type></note>",
                                   "CDEFGAB"[(m + b + static_cast<int>(p)) % 7], 3 + p % 3);
            }
            xml += "</measure>";
        }
        xml += "</part>";
    }
    xml += "</score-partwise>";

    const auto serial = importMusicXml(xml, {.threads = 1});
    if (!serial) {
        FAIL(serial.error().message);
    }
    CHECK(serial->song.trackCount() == kExtras.size());
    CHECK(serial->warnings.size() == 4);
    for (const unsigned threads : {2U, 3U, 8U}) {
        CAPTURE(threads);
        const auto parallel = importMusicXml(xml, {.threads = threads});
        REQUIRE(parallel.has_value());
        CHECK(parallel->song == serial->song);
        CHECK(parallel->warnings == serial->warnings);
    }
    for (const auto& entry : std::filesystem::directory_iterator(corpusDir())) {
        CAPTURE(entry.path().filename().string());
        const auto one = importMusicXmlFile(entry.path(), {.threads = 1});
        const auto many = importMusicXmlFile(entry.path(), {.threads = 4});
        REQUIRE((one.has_value() && many.has_value()));
        CHECK(one->song == many->song);
        CHECK(one->warnings == many->warnings);
    }
}

TEST_CASE("MusicXML: notes without a type and gaps are written as standard values", "[musicxml]") {
    // divisions 4: 6 = dotted quarter, then a 5-division rest (quarter + 16th)
    // and a 5-division forward gap before the last note.
    const ImportResult result =
        parse(onePart(R"(<note><pitch><step>C</step><octave>4</octave></pitch>)"
                      R"(<duration>6</duration></note>)"
                      R"(<note><rest/><duration>5</duration></note>)"
                      R"(<forward><duration>4</duration></forward>)"
                      R"(<note><pitch><step>D</step><octave>4</octave></pitch>)"
                      R"(<duration>1</duration><type>16th</type></note>)"));
    CHECK(summary(voiceOf(result.song, 0, 0, 0)) == "4.=C4 4=r 16=r 4=r 16=D4");
    CHECK(result.warnings.empty());
}

TEST_CASE("MusicXML: wedges become hairpins on the beats under them", "[musicxml]") {
    const auto note = [](const char* step, int duration) {
        return std::string(R"(<note><pitch><step>)") + step +
               R"(</step><octave>4</octave></pitch><duration>)" + std::to_string(duration) +
               "</duration></note>";
    };
    const auto wedge = [](const char* type) {
        return std::string(R"(<direction placement="below"><direction-type><wedge type=")") + type +
               R"("/></direction-type></direction>)";
    };
    // C D <E F | G> A2., then a crescendo stopped at the end of the last measure.
    const ImportResult result = parse(onePart(
        note("C", 4) + note("D", 4) + wedge("diminuendo") + note("E", 4) + note("F", 4) +
        R"(</measure><measure number="2">)" + note("G", 4) + wedge("stop") + note("A", 12) +
        R"(</measure><measure number="3">)" + wedge("crescendo") + note("B", 16) + wedge("stop")));
    CHECK(result.warnings.empty());
    const auto hairpins = [&result](std::size_t m) {
        std::vector<core::Hairpin> kinds;
        for (const core::Beat& beat : voiceOf(result.song, 0, 0, m).beats) {
            kinds.push_back(beat.effects.hairpin);
        }
        return kinds;
    };
    using enum core::Hairpin;
    CHECK(hairpins(0) == std::vector{None, None, Diminuendo, Diminuendo});
    CHECK(hairpins(1) == std::vector{Diminuendo, None});
    CHECK(hairpins(2) == std::vector{Crescendo});
}

TEST_CASE("MusicXML: fermatas hold their beats", "[musicxml]") {
    // ADR-0085: by shape, on any note of a chord, on a rest and on a whole
    // measure's rest, which becomes a rest of its length to carry it.
    const auto fermata = [](const char* shape) {
        return std::string(R"(<notations><fermata type="upright">)") + shape +
               "</fermata></notations>";
    };
    const ImportResult result = parse(onePart(
        R"(<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration>)" +
        fermata("angled") + "</note>" +
        R"(<note><pitch><step>E</step><octave>4</octave></pitch><duration>4</duration></note>)"
        R"(<note><chord/><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration>)" +
        fermata("") + "</note>" + R"(<note><rest/><duration>4</duration>)" + fermata("square") +
        "</note>" +
        R"(<note><pitch><step>D</step><octave>4</octave></pitch><duration>4</duration>)"
        R"(<notations><fermata type="inverted">curlew</fermata></notations></note>)"
        R"(</measure><measure number="2"><note><rest measure="yes"/><duration>16</duration>)" +
        fermata("double-square") + "</note>"));
    CHECK(result.warnings.empty());
    using enum core::Fermata;
    const auto& first = voiceOf(result.song, 0, 0, 0).beats;
    REQUIRE(first.size() == 4);
    CHECK(first[0].effects.fermata == Short);
    CHECK(first[1].effects.fermata == Normal);
    CHECK(first[2].effects.fermata == Long);
    CHECK(first[2].isRest());
    CHECK(first[3].effects.fermata == Normal);
    const auto& second = voiceOf(result.song, 0, 0, 1).beats;
    REQUIRE(second.size() == 1);
    CHECK(second[0].isRest());
    CHECK(second[0].duration == core::Duration(core::NoteValue::Whole));
    CHECK(second[0].effects.fermata == Long);
}

TEST_CASE("MusicXML: slurs join the beats from their start to their stop", "[musicxml]") {
    // ADR-0086: every beat from a slur's start to the beat before its stop is
    // slurred to the next, across barlines; a note that ends one slur and
    // starts the next joins them, and a slur inside another adds nothing.
    const auto slurs = [](std::string_view marks) {
        return std::format("<notations>{}</notations>", marks);
    };
    const std::string start = R"(<slur type="start" number="1"/>)";
    const std::string stop = R"(<slur type="stop" number="1"/>)";
    const ImportResult result = parse(onePart(
        quarterNote("C", slurs(start)) + quarterNote("D", "") + quarterNote("E", slurs(stop)) +
        quarterNote("F", slurs(start)) + R"(</measure><measure number="2">)" +
        quarterNote("G", "") + quarterNote("A", slurs(stop)) + quarterNote("B", slurs(start)) +
        quarterNote("C", slurs(start + stop)) + R"(</measure><measure number="3">)" +
        quarterNote("D", slurs(stop)) + quarterNote("E", slurs(start)) +
        quarterNote("F", slurs(R"(<slur type="start" number="2" placement="below"/>)")) +
        quarterNote("G", slurs(R"(<slur type="stop" number="2"/>)")) +
        R"(</measure><measure number="4"><note><pitch><step>A</step><octave>4</octave></pitch>)"
        R"(<duration>16</duration><voice>1</voice><type>whole</type>)" +
        slurs(stop) + "</note>"));
    CHECK(result.warnings.empty());
    const auto slurred = [&](std::size_t m) {
        std::vector<bool> flags;
        for (const core::Beat& beat : voiceOf(result.song, 0, 0, m).beats) {
            flags.push_back(beat.effects.slur);
        }
        return flags;
    };
    CHECK(slurred(0) == std::vector{true, true, false, true});
    CHECK(slurred(1) == std::vector{true, false, true, true});
    CHECK(slurred(2) == std::vector{false, true, true, true});
    CHECK(slurred(3) == std::vector{false});
}

TEST_CASE("MusicXML: slurs from grace notes, between voices or without an end are dropped",
          "[musicxml]") {
    const ImportResult result = parse(onePart(
        R"(<note><grace slash="yes"/><pitch><step>B</step><octave>3</octave></pitch>)"
        R"(<voice>1</voice><type>eighth</type><notations><slur type="start"/></notations></note>)" +
        quarterNote("C", R"(<notations><slur type="stop"/></notations>)") +
        quarterNote("D", R"(<notations><slur type="start" number="3"/></notations>)") +
        quarterNote("E", "") + quarterNote("F", "") +
        R"(<backup><duration>16</duration></backup>)"
        R"(<note><pitch><step>C</step><octave>3</octave></pitch><duration>8</duration>)"
        R"(<voice>2</voice><type>half</type><notations><slur type="stop" number="3"/>)"
        R"(</notations></note><note><pitch><step>D</step><octave>3</octave></pitch>)"
        R"(<duration>8</duration><voice>2</voice><type>half</type><notations>)"
        R"(<slur type="start" number="4"/></notations></note>)"));
    CHECK(hasWarning(result, "slurs on grace notes dropped", "measure 1"));
    CHECK(hasWarning(result, "slurs between voices dropped", "measure 1"));
    CHECK(hasWarning(result, "slurs without an end dropped", "measure 1"));
    for (std::size_t v = 0; v < 2; ++v) {
        for (const core::Beat& beat : voiceOf(result.song, 0, 0, 0, v).beats) {
            CHECK_FALSE(beat.effects.slur);
        }
    }
}

TEST_CASE("MusicXML: clef changes hold from the measure they start", "[musicxml][clef]") {
    // ADR-0087: a change at a measure's start is its own; one after the
    // measure's notes, or in the middle of them (with a warning), goes to the
    // next measure.
    const auto clef = [](std::string_view sign, int line) {
        return std::format("<attributes><clef><sign>{}</sign><line>{}</line></clef></attributes>",
                           sign, line);
    };
    const std::string four =
        quarterNote("C", "") + quarterNote("D", "") + quarterNote("E", "") + quarterNote("F", "");
    const ImportResult result = parse(
        onePart(clef("G", 2) + four + R"(</measure><measure number="2">)" + clef("F", 4) + four +
                R"(</measure><measure number="3">)" + four + clef("C", 3) +
                R"(</measure><measure number="4">)" + quarterNote("G", "") + quarterNote("A", "") +
                clef("G", 2) + quarterNote("B", "") + quarterNote("C", "") +
                R"(</measure><measure number="5">)" + clef("G", 2) + four));
    const core::Staff& staff = result.song.track(0).staff(0);
    REQUIRE(result.song.measureCount() == 5);
    CHECK(staff.clef() == core::Clef::Treble);
    CHECK(staff.measure(0).clef == std::nullopt);
    CHECK(staff.measure(1).clef == core::Clef::Bass);
    CHECK(staff.measure(2).clef == std::nullopt);
    CHECK(staff.measure(3).clef == core::Clef::Alto);
    CHECK(staff.measure(4).clef == core::Clef::Treble);
    CHECK(staff.clefAt(2) == core::Clef::Bass);
    CHECK(result.warnings.size() == 1);
    CHECK(
        hasWarning(result, "clef changes inside a measure moved to the next measure", "measure 4"));
}

TEST_CASE("MusicXML: clef changes keep the staff's octave and its percussion", "[musicxml][clef]") {
    const auto part = [](std::string_view first, std::string_view change) {
        return parse(onePart(std::format("<attributes>{}</attributes>", first) +
                             quarterNote("C", "") + R"(</measure><measure number="2">)" +
                             std::format("<attributes>{}</attributes>", change) +
                             quarterNote("C", "")));
    };
    const std::string octaveTreble =
        "<clef><sign>G</sign><line>2</line><clef-octave-change>-1</clef-octave-change></clef>";
    const std::string octaveBass =
        "<clef><sign>F</sign><line>4</line><clef-octave-change>-1</clef-octave-change></clef>";

    // A guitar's octave clef changes to the bass clef an octave down.
    ImportResult result = part(octaveTreble, octaveBass);
    CHECK(result.warnings.empty());
    CHECK(result.song.track(0).staff(0).clef() == core::Clef::Treble8vb);
    CHECK(result.song.track(0).staff(0).measure(1).clef == core::Clef::Bass8vb);

    // Its octave is the staff's: a change without it keeps it, with a warning.
    result = part(octaveTreble, "<clef><sign>F</sign><line>4</line></clef>");
    CHECK(result.song.track(0).staff(0).measure(1).clef == core::Clef::Bass8vb);
    CHECK(hasWarning(result, "clef octave changes dropped", "measure 2"));

    // Percussion clefs do not change.
    result =
        part("<clef><sign>G</sign><line>2</line></clef>", "<clef><sign>percussion</sign></clef>");
    CHECK(result.song.track(0).staff(0).measure(1).clef == std::nullopt);
    CHECK(hasWarning(result, "clef changes dropped", "measure 2"));
}

TEST_CASE("MusicXML: repeats and alternate endings belong to their measures", "[musicxml]") {
    // |: 1 |1.2. 2 :|3. 3 |, played three times, as notation programs write it.
    const std::string rest = R"(<note><rest measure="yes"/><duration>16</duration></note>)";
    const ImportResult result = parse(
        onePart(R"(<barline location="left"><bar-style>heavy-light</bar-style>)"
                R"(<repeat direction="forward"/></barline>)" +
                rest +
                R"(</measure><measure number="2"><barline location="left">)"
                R"(<ending number="1, 2" type="start">1., 2.</ending></barline>)" +
                rest +
                R"(<barline location="right"><bar-style>light-heavy</bar-style>)"
                R"(<ending number="1, 2" type="stop"/><repeat direction="backward" times="3"/>)"
                R"(</barline></measure><measure number="3"><barline location="left">)"
                R"(<ending number="3" type="start">3.</ending></barline>)" +
                rest +
                R"(<barline location="right"><ending number="3" type="discontinue"/></barline>)"
                R"(</measure><measure number="4">)" +
                rest));
    CHECK(result.warnings.empty());
    const core::Song& song = result.song;
    REQUIRE(song.measureCount() == 4);
    CHECK(song.measureHeader(0).repeatStart);
    CHECK(song.measureHeader(1).repeatTimes == 3);
    CHECK(song.measureHeader(1).alternateEndings == 0b011);
    CHECK(song.measureHeader(2).alternateEndings == 0b100);
    CHECK(song.measureHeader(3).alternateEndings == 0);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 0, 1, 0, 2, 3});
}

TEST_CASE("MusicXML: signs and jumps become measure marks", "[musicxml]") {
    // 1 | 2 segno | 3 To Coda | 4 D.S. al Coda | 5 coda | 6, written the ways
    // notation programs write them: a glyph with a <sound>, text alone, text
    // with a <sound>, a sign on the barline (ADR-0049).
    const std::string rest = R"(<note><rest measure="yes"/><duration>16</duration></note>)";
    const ImportResult result = parse(onePart(
        R"(<direction><direction-type><words>dolce</words></direction-type></direction>)" + rest +
        R"(</measure><measure number="2"><direction placement="above"><direction-type>)"
        R"(<segno/></direction-type><sound segno="segno"/></direction>)" +
        rest + R"(</measure><measure number="3">)" + rest +
        R"(<direction><direction-type><coda/></direction-type><direction-type>)"
        R"(<words>To Coda</words></direction-type></direction>)"
        R"(</measure><measure number="4">)" +
        rest +
        R"(<direction><direction-type><words>D.S. al Coda</words></direction-type>)"
        R"(<sound dalsegno="segno"/></direction>)"
        R"(</measure><measure number="5"><barline location="left"><coda/></barline>)" +
        rest + R"(</measure><measure number="6">)" + rest));
    const core::Song& song = result.song;
    REQUIRE(song.measureCount() == 6);
    // Other text is reported: a measure rest has no beat to hold it
    // (ADR-0081). Navigation text is not.
    CHECK(result.warnings.size() == 1);
    CHECK(hasWarning(result, "text directions dropped", "measure 1"));
    CHECK(song.measureHeader(1).segno);
    CHECK(song.measureHeader(2).toCoda);
    CHECK_FALSE(song.measureHeader(2).coda); // the glyph decorates "To Coda"
    CHECK(song.measureHeader(3).jump == core::JumpTo::Segno);
    CHECK(song.measureHeader(3).jumpThen == core::JumpThen::Coda);
    CHECK(song.measureHeader(4).coda);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 2, 3, 1, 2, 4, 5});
}

TEST_CASE("MusicXML: a jump given only for playback ends where the song says", "[musicxml]") {
    // <sound dacapo="yes"/> says where the jump goes, not where it ends: the
    // song has a Fine, so it is D.C. al Fine.
    const std::string rest = R"(<note><rest measure="yes"/><duration>16</duration></note>)";
    const ImportResult result =
        parse(onePart(rest + R"(</measure><measure number="2">)" + rest +
                      R"(<sound fine="yes"/></measure><measure number="3">)" + rest +
                      R"(<direction><sound dacapo="yes"/></direction>)"));
    CHECK(result.warnings.empty());
    const core::Song& song = result.song;
    CHECK(song.measureHeader(1).fine);
    CHECK(song.measureHeader(2).jump == core::JumpTo::Start);
    CHECK(song.measureHeader(2).jumpThen == core::JumpThen::Fine);
    CHECK(song.playOrder() == std::vector<std::size_t>{0, 1, 2, 0, 1});
}

TEST_CASE("MusicXML: a fifth voice on a staff is dropped with a warning", "[musicxml]") {
    std::string measure;
    for (int voice = 1; voice <= 5; ++voice) {
        if (voice > 1) {
            measure += "<backup><duration>16</duration></backup>";
        }
        measure +=
            std::format(R"(<note><pitch><step>C</step><octave>{}</octave></pitch>)"
                        R"(<duration>16</duration><voice>{}</voice><type>whole</type></note>)",
                        voice + 1, voice);
    }
    const ImportResult result = parse(onePart(measure));
    for (std::size_t v = 0; v < core::Measure::kMaxVoices; ++v) {
        CHECK(voiceOf(result.song, 0, 0, 0, v).beats.size() == 1);
    }
    CHECK(hasWarning(result, "voice", "measure 1"));
}

TEST_CASE("MusicXML: compressed .mxl files are read through their container", "[musicxml]") {
    const std::string melody = readText(corpusDir() / "melody.musicxml");
    const std::string container =
        R"(<?xml version="1.0" encoding="UTF-8"?><container><rootfiles>)"
        R"(<rootfile full-path="score/melody.musicxml" )"
        R"(media-type="application/vnd.recordare.musicxml+xml"/></rootfiles></container>)";

    SECTION("with META-INF/container.xml") {
        const auto bytes = zipOf({{"mimetype", "application/vnd.recordare.musicxml"},
                                  {"META-INF/container.xml", container},
                                  {"score/melody.musicxml", melody}});
        const auto result = importMusicXmlBytes(bytes);
        REQUIRE(result.has_value());
        CHECK(result->song.info().title == "Corpus Melody");
    }
    SECTION("without a container, the first score in the archive") {
        const auto bytes = zipOf({{"melody.musicxml", melody}});
        const auto result = importMusicXmlBytes(bytes);
        REQUIRE(result.has_value());
        CHECK(result->song.measureCount() == 4);
    }
    SECTION("a container that names a missing file") {
        const auto bytes = zipOf({{"META-INF/container.xml", container}});
        const auto result = importMusicXmlBytes(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("score/melody.musicxml") != std::string::npos);
    }
    SECTION("uncompressed text through the same entry point") {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(melody.data());
        const auto result = importMusicXmlBytes({begin, melody.size()});
        REQUIRE(result.has_value());
        CHECK(result->song.trackCount() == 1);
    }
}

TEST_CASE("MusicXML: damaged documents never crash and import to loadable songs", "[musicxml]") {
    std::mt19937 random(20261123);
    constexpr std::array<std::string_view, 12> kNoise{
        "-1", "999999999", "-999999999", "0", "x", "", "1e308", "nan", "3+", "TAB", "37", "-7"};
    std::uniform_int_distribution<std::size_t> pickNoise(0, kNoise.size() - 1);
    const auto check = [](std::string_view xml) {
        const auto result = importMusicXml(xml);
        if (result) {
            // Whatever was read must be a model the native format accepts.
            REQUIRE(elara::io::songFromJson(elara::io::songToJson(result->song)).has_value());
        }
    };
    for (const auto& entry : std::filesystem::directory_iterator(corpusDir())) {
        const std::string text = readText(entry.path());
        CAPTURE(entry.path().filename().string());
        for (std::size_t cut = 0; cut < text.size(); cut += 61) {
            check(std::string_view(text).substr(0, cut));
        }
        std::uniform_int_distribution<std::size_t> where(0, text.size() - 1);
        for (int round = 0; round < 300; ++round) {
            // Replace the text after a random tag with noise.
            std::string damaged = text;
            const std::size_t open = damaged.find('>', where(random));
            const std::size_t close =
                open == std::string::npos ? std::string::npos : damaged.find('<', open + 1);
            if (close == std::string::npos) {
                continue;
            }
            damaged.replace(open + 1, close - open - 1, kNoise[pickNoise(random)]);
            CAPTURE(round, damaged.substr(open > 80 ? open - 80 : 0, 160));
            check(damaged);
        }
    }
}

TEST_CASE("MusicXML: unreadable documents are errors with a reason", "[musicxml]") {
    const auto message = [](std::string_view xml) {
        const auto result = importMusicXml(xml);
        REQUIRE_FALSE(result.has_value());
        return result.error().message;
    };
    CHECK(message("<score-partwise><part-list>").find("invalid XML") != std::string::npos);
    CHECK(message(R"(<score-timewise version="4.0"/>)").find("timewise") != std::string::npos);
    CHECK(message("<html/>").find("not a MusicXML score") != std::string::npos);

    const auto missing = importMusicXmlFile(corpusDir() / "no-such-file.musicxml");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message == "cannot open the file");
}

TEST_CASE("MusicXML: transposed keys move by the whole interval, so flat keys stay flat",
          "[musicxml]") {
    // Alto saxophone (sounds a major sixth lower) written in E-flat major: the
    // concert key is G-flat major, not its enharmonic F-sharp major.
    const ImportResult result = parse(
        R"(<score-partwise version="4.0"><part-list><score-part id="P1">)"
        R"(<part-name>Alto Saxophone</part-name></score-part></part-list><part id="P1">)"
        R"(<measure number="1"><attributes><divisions>1</divisions><key><fifths>-3</fifths>)"
        R"(</key><time><beats>4</beats><beat-type>4</beat-type></time><transpose>)"
        R"(<diatonic>-5</diatonic><chromatic>-9</chromatic></transpose></attributes>)"
        R"(<note><pitch><step>E</step><alter>-1</alter><octave>5</octave></pitch>)"
        R"(<duration>4</duration><type>whole</type></note></measure></part></score-partwise>)");
    CHECK(result.song.measureHeader(0).keySignature ==
          core::KeySignature(-6, core::KeyMode::Major));
    CHECK(result.song.track(0).instrument().writtenOffset == 9);
    CHECK(noteOf(result.song, 0, 0).pitch == core::Pitch(core::Step::G, -1, 4));
}
