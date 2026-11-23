// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/validation.hpp>
#include <elara/io/guitar_pro.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = elara::core;
using core::Duration;
using core::NoteValue;
using elara::io::importGuitarPro;
using elara::io::importGuitarProFile;
using elara::io::ImportResult;

namespace {

constexpr std::size_t kSector = 0x1000;

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    for (const char c : text) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
}

void putLe32(std::vector<std::uint8_t>& bytes, std::size_t at, std::size_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

void putText(std::vector<std::uint8_t>& bytes, std::size_t at, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(text[i]);
    }
}

// A Guitar Pro 6 file system ("BCFS"): sector 0 unused, then for each file an
// entry sector (type 2, name, size, the sectors that hold it) and its data.
std::vector<std::uint8_t>
fileSystem(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<std::uint8_t> fs(kSector, 0);
    for (const auto& [name, content] : files) {
        const std::size_t count =
            std::max<std::size_t>(1, (content.size() + kSector - 1) / kSector);
        const std::size_t entry = fs.size();
        fs.resize(entry + (count + 1) * kSector, 0);
        putLe32(fs, entry, 2);
        putText(fs, entry + 0x04, name);
        putLe32(fs, entry + 0x8C, content.size());
        for (std::size_t i = 0; i < count; ++i) {
            putLe32(fs, entry + 0x94 + 4 * i, entry / kSector + 1 + i);
        }
        putText(fs, entry + kSector, content);
    }
    std::vector<std::uint8_t> bytes = bytesOf("BCFS");
    bytes.insert(bytes.end(), fs.begin(), fs.end());
    return bytes;
}

// "BCFZ", the unpacked size and `data` as chunks of up to three literal bytes.
std::vector<std::uint8_t> packLiterals(std::span<const std::uint8_t> data) {
    std::vector<bool> bits;
    const auto put = [&bits](unsigned value, unsigned count) { // most significant first
        for (unsigned i = count; i > 0; --i) {
            bits.push_back(((value >> (i - 1)) & 1U) != 0);
        }
    };
    for (std::size_t at = 0; at < data.size(); at += 3) {
        const auto count = static_cast<unsigned>(std::min<std::size_t>(3, data.size() - at));
        bits.push_back(false);
        bits.push_back((count & 1U) != 0); // the count, least significant bit first
        bits.push_back((count & 2U) != 0);
        for (unsigned i = 0; i < count; ++i) {
            put(data[at + i], 8);
        }
    }
    std::vector<std::uint8_t> out = bytesOf("BCFZ....");
    putLe32(out, 4, data.size());
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (i % 8 == 0) {
            out.push_back(0);
        }
        if (bits[i]) {
            out.back() = static_cast<std::uint8_t>(out.back() | (0x80U >> (i % 8)));
        }
    }
    return out;
}

// A Guitar Pro 6 score: one bass track whose General MIDI table, tuning and
// capo sit on the track, one 4/4 bar in the bass clef holding a whole note on
// the open low string, and a comment long enough to fill a second sector.
std::string bassScore() {
    return R"(<?xml version="1.0" encoding="utf-8"?><GPIF><GPRevision>0</GPRevision>)"
           "<!-- " +
           std::string(5000, 'x') +
           " -->"
           "<Score><Title><![CDATA[Low]]></Title></Score><MasterTrack><Tracks>0</Tracks>"
           R"(</MasterTrack><Tracks><Track id="0"><Name><![CDATA[Bass]]></Name>)"
           R"(<GeneralMidi table="Instrument"><Program>33</Program></GeneralMidi><Properties>)"
           R"(<Property name="CapoFret"><Fret>0</Fret></Property><Property name="Tuning">)"
           "<Pitches>28 33 38 43</Pitches></Property></Properties></Track></Tracks>"
           "<MasterBars><MasterBar><Key><AccidentalCount>0</AccidentalCount><Mode>Major</Mode>"
           "</Key><Time>4/4</Time><Bars>0</Bars></MasterBar></MasterBars><Bars>"
           R"(<Bar id="0"><Clef>F4</Clef><Voices>0 -1 -1 -1</Voices></Bar></Bars><Voices>)"
           R"(<Voice id="0"><Beats>0</Beats></Voice></Voices><Beats><Beat id="0">)"
           R"(<Rhythm ref="0"/><Notes>0</Notes></Beat></Beats><Notes><Note id="0"><Properties>)"
           R"(<Property name="Fret"><Fret>0</Fret></Property><Property name="String">)"
           "<String>0</String></Property></Properties></Note></Notes><Rhythms>"
           R"(<Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm></Rhythms></GPIF>)";
}

std::string error(std::span<const std::uint8_t> bytes) {
    const auto imported = importGuitarPro(bytes);
    REQUIRE_FALSE(imported.has_value());
    return imported.error().message;
}

void checkBass(std::span<const std::uint8_t> bytes) {
    const auto imported = importGuitarPro(bytes);
    if (!imported) {
        FAIL(imported.error().message);
    }
    REQUIRE(imported->song.trackCount() == 1);
    const core::Track& bass = imported->song.track(0);
    CHECK(imported->song.info().title == "Low");
    CHECK(bass.instrument().name == "Electric Bass (finger)");
    CHECK(bass.instrument().writtenOffset == 12);
    CHECK(bass.staff(0).clef() == core::Clef::Bass8vb);
    CHECK(bass.staff(0).tuning()->toString() == "E1 A1 D2 G2");
    const core::Note& note = bass.staff(0).measure(0).voices[0].beats[0].notes[0];
    CHECK(note.position == core::FretPosition{.string = 4, .fret = 0});
    CHECK(note.pitch.midiNumber() == 28);
}

} // namespace

TEST_CASE("Guitar Pro 6: a compressed file keeps its tracks, tunings and drums",
          "[io][guitarpro]") {
    const auto loaded =
        importGuitarProFile(std::filesystem::path(ELARA_CORPUS_DIR) / "gp" / "duet.gpx");
    if (!loaded) {
        FAIL(loaded.error().message);
    }
    const ImportResult& result = *loaded;
    const core::Song& song = result.song;
    CHECK(result.warnings ==
          std::vector<std::string>{"Drums: drum notes without a MIDI key dropped (measure 2)"});
    CHECK(core::validate(song).empty());
    CHECK(song.info().title == "Corpus Duet");
    CHECK(song.startTempo() == core::Tempo(132));
    CHECK(song.measureHeader(0).keySignature.fifths() == -3);
    REQUIRE(song.trackCount() == 2);

    // The tuning (E-flat, spelled with flats) and the capo sit on the track; the
    // guitar is written an octave above its sound.
    const core::Track& guitar = song.track(0);
    CHECK(guitar.instrument().kind == core::InstrumentKind::Fretted);
    CHECK(guitar.instrument().name == "Acoustic Guitar (steel)");
    CHECK(guitar.instrument().writtenOffset == 12);
    CHECK(guitar.staff(0).clef() == core::Clef::Treble8vb);
    CHECK(guitar.staff(0).tuning()->toString() == "Eb2 Ab2 Db3 Gb3 Bb3 Eb4");
    CHECK(guitar.staff(0).capo() == 2);
    const auto& bar1 = guitar.staff(0).measure(0).voices[0].beats;
    REQUIRE(bar1.size() == 3);
    CHECK(bar1[0].notes.size() == 3);
    CHECK(bar1[0].notes[0].position == core::FretPosition{.string = 6, .fret = 0});
    CHECK(bar1[0].notes[0].pitch.midiNumber() == 39 + 2);
    CHECK(bar1[2].duration == Duration(NoteValue::Half));
    CHECK(bar1[2].notes[0].effects.letRing);

    // Drum notes give their MIDI key; the one that names only a kit element is
    // dropped, which leaves its beat a rest.
    const core::Track& drums = song.track(1);
    CHECK(drums.instrument().kind == core::InstrumentKind::Percussion);
    CHECK(drums.staff(0).clef() == core::Clef::Percussion);
    const auto& kit = drums.staff(0).measure(0).voices[0].beats;
    REQUIRE(kit.size() == 4);
    CHECK(kit[0].notes[0].pitch.midiNumber() == 36);
    CHECK(kit[0].notes[1].pitch.midiNumber() == 42);
    CHECK(kit[2].notes[0].pitch.midiNumber() == 38);
    const auto& kit2 = drums.staff(0).measure(1).voices[0].beats;
    REQUIRE(kit2.size() == 2);
    CHECK(kit2[0].notes[0].pitch.midiNumber() == 49);
    CHECK(kit2[1].isRest());
}

TEST_CASE("Guitar Pro 6: plain and literal-only containers hold the same score",
          "[io][guitarpro]") {
    const std::vector<std::uint8_t> plain =
        fileSystem({{"misc.xml", "<Misc />"}, {"score.gpif", bassScore()}, {"Other", "x"}});
    REQUIRE(plain.size() > 5 * kSector); // the score spans two sectors
    checkBass(plain);
    checkBass(packLiterals(plain));
}

TEST_CASE("Guitar Pro 6: damaged containers are errors with a reason", "[io][guitarpro]") {
    CHECK(error(bytesOf("BCFZab")) == "Guitar Pro: damaged Guitar Pro 6 file: it ends early");

    // A back reference (1), word size 1 (0001), offset 1, length 1, with nothing
    // unpacked yet.
    std::vector<std::uint8_t> early = bytesOf(std::string_view("BCFZ\x0A\x00\x00\x00", 8));
    early.push_back(0x8E);
    CHECK(error(early) == "Guitar Pro: damaged Guitar Pro 6 file: a reference before the start");

    CHECK(error(packLiterals(bytesOf("ABCDEFG"))) ==
          "Guitar Pro: damaged Guitar Pro 6 file: the unpacked data is not a file system");
    CHECK(error(fileSystem({{"misc.xml", "<Misc />"}})) ==
          "Guitar Pro: no score.gpif in the container (not a Guitar Pro 6 file)");

    std::vector<std::uint8_t> farSector = fileSystem({{"score.gpif", "<GPIF />"}});
    putLe32(farSector, 4 + kSector + 0x94, 99);
    CHECK(error(farSector) ==
          "Guitar Pro: damaged Guitar Pro 6 file: score.gpif names a sector past the end");

    std::vector<std::uint8_t> cut = fileSystem({{"score.gpif", "<GPIF />"}});
    cut.resize(4 + kSector + 0x40);
    CHECK(error(cut) == "Guitar Pro: damaged Guitar Pro 6 file: a file entry is cut");
}
