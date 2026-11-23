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
    CHECK(summary(voiceOf(song, 0, 1, 1, 0)) == "2.=F3"); // the grace note is dropped
}

TEST_CASE("MusicXML: what the model cannot hold yet is reported, not lost silently", "[musicxml]") {
    const ImportResult result = load("piano-voices.musicxml");
    CHECK(hasWarning(result, "chord symbol", "measure 1"));

    CHECK(hasWarning(result, "text", "measure 2"));
    CHECK(hasWarning(result, "lyric", "measure 2"));
    CHECK(hasWarning(result, "grace note", "measure 2"));
    CHECK(hasWarning(result, "Piano", "grace note"));
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
        "<harmony><root><root-step>C</root-step></root><kind>major</kind></harmony>",
        R"(<note><grace/><pitch><step>D</step><octave>4</octave></pitch><type>eighth</type></note>)",
        "",
        R"(<direction><direction-type><words>dolce</words></direction-type></direction>)",
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
