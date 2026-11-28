// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmarks for elara_io: the native format, MusicXML, MIDI and Guitar Pro.
#include <elara/core/performance.hpp>
#include <elara/io/guitar_pro.hpp>
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

// The large song as a notation program would export it: 20 guitar parts x 500
// measures, eight eighth-note chords of two notes, string and fret on each note.
std::string largeMusicXml() {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?><score-partwise version="4.0">)"
                      "<part-list>";
    for (int p = 0; p < 20; ++p) {
        xml += std::format(R"(<score-part id="P{0}"><part-name>Electric Guitar</part-name>)"
                           R"(<midi-instrument id="P{0}-I1"><midi-program>28</midi-program>)"
                           "</midi-instrument></score-part>",
                           p);
    }
    xml += "</part-list>";
    const auto note = [](int midi, int string, int fret, bool chord) {
        const auto pitch = elara::core::Pitch::fromMidi(midi);
        return std::format(
            "<note>{}<pitch><step>{}</step>{}<octave>{}</octave></pitch><duration>1</duration>"
            "<voice>1</voice><type>eighth</type><notations><technical><string>{}</string>"
            "<fret>{}</fret></technical></notations></note>",
            chord ? "<chord/>" : "", "CDEFGAB"[static_cast<int>(pitch.step())],
            pitch.alter() != 0 ? std::format("<alter>{}</alter>", pitch.alter()) : "",
            pitch.octave(), string, fret);
    };
    for (int p = 0; p < 20; ++p) {
        xml += std::format(R"(<part id="P{}">)", p);
        for (int m = 0; m < 500; ++m) {
            xml += std::format(R"(<measure number="{}">)", m + 1);
            if (m == 0) {
                xml += "<attributes><divisions>2</divisions><key><fifths>0</fifths></key>"
                       "<time><beats>4</beats><beat-type>4</beat-type></time>"
                       "<clef><sign>G</sign><line>2</line><clef-octave-change>-1"
                       "</clef-octave-change></clef><staff-details>";
                constexpr const char* kTuning[] = {"E2", "A2", "D3", "G3", "B3", "E4"};
                for (int line = 0; line < 6; ++line) {
                    xml += std::format(R"(<staff-tuning line="{}"><tuning-step>{}</tuning-step>)"
                                       "<tuning-octave>{}</tuning-octave></staff-tuning>",
                                       line + 1, kTuning[line][0], kTuning[line][1]);
                }
                xml += "</staff-details></attributes>";
            }
            for (int b = 0; b < 8; ++b) {
                xml += note(40 + b % 5, 6, b % 5, false);
                xml += note(45 + b % 5, 5, b % 5, true);
            }
            xml += "</measure>";
        }
        xml += "</part>";
    }
    return xml + "</score-partwise>";
}

} // namespace

TEST_CASE("native format benchmarks", "[benchmark][io]") {
    const elara::core::Song song = elara::bench::largeSong();
    const std::string json = elara::io::songToJson(song);
    const std::vector<std::uint8_t> file = elara::io::encodeNative(song);
    std::printf("large song: song.json %zu bytes, .elara file %zu bytes\n", json.size(),
                file.size());

    BENCHMARK("write song.json of the large song") {
        return elara::io::songToJson(song).size();
    };
    BENCHMARK("read song.json of the large song") {
        return elara::io::songFromJson(json)->measureCount();
    };
    BENCHMARK("encode the large song as a .elara file (JSON + deflate)") {
        return elara::io::encodeNative(song).size();
    };
    BENCHMARK("decode the large song from a .elara file") {
        return elara::io::decodeNative(file)->measureCount();
    };

    // Layout adjustments (ADR-0152): a break before every measure of every scope.
    const elara::core::Song broken = elara::bench::withBreaksEverywhere(song);
    const std::vector<std::uint8_t> brokenFile = elara::io::encodeNative(broken);
    std::printf("with a break before every measure of every scope: song.json %zu bytes, "
                ".elara file %zu bytes\n",
                elara::io::songToJson(broken).size(), brokenFile.size());
    BENCHMARK("encode the large song, a break before every measure of every scope") {
        return elara::io::encodeNative(broken).size();
    };
    BENCHMARK("decode the same") {
        return elara::io::decodeNative(brokenFile)->measureCount();
    };
}

TEST_CASE("MusicXML import benchmarks", "[benchmark][io]") {
    const std::string xml = largeMusicXml();
    const auto imported = elara::io::importMusicXml(xml);
    REQUIRE(imported.has_value());
    REQUIRE(imported->warnings.empty());
    REQUIRE(imported->song == elara::bench::largeSong());
    std::printf("large song as MusicXML: %zu bytes\n", xml.size());

    BENCHMARK("import the large song from MusicXML") {
        return elara::io::importMusicXml(xml)->song.measureCount();
    };
}

TEST_CASE("MusicXML export benchmarks", "[benchmark][io]") {
    const elara::core::Song song = elara::bench::largeSong();
    const elara::io::ExportResult exported = elara::io::exportMusicXml(song);
    REQUIRE(exported.warnings.empty());
    const auto back = elara::io::importMusicXml(exported.data);
    REQUIRE(back.has_value());
    REQUIRE(back->warnings.empty());
    REQUIRE(back->song == song);
    const elara::io::ExportResult compressed =
        elara::io::exportMusicXml(song, {.compressed = true});
    std::printf("large song exported: MusicXML %zu bytes, .mxl %zu bytes\n", exported.data.size(),
                compressed.data.size());

    BENCHMARK("export the large song as MusicXML") {
        return elara::io::exportMusicXml(song).data.size();
    };
    BENCHMARK("export the large song as MusicXML on one thread") {
        return elara::io::exportMusicXml(song, {.threads = 1}).data.size();
    };
    BENCHMARK("export the large song as .mxl (MusicXML + deflate)") {
        return elara::io::exportMusicXml(song, {.compressed = true}).data.size();
    };
}

namespace {

// (track, start, length, key, velocity) of every note, sorted: what a song plays.
std::vector<std::tuple<std::size_t, elara::core::Fraction, elara::core::Fraction, int, int>>
playedNotes(const elara::core::Song& song) {
    std::vector<std::tuple<std::size_t, elara::core::Fraction, elara::core::Fraction, int, int>>
        notes;
    for (const auto& note : elara::core::soundingNotes(song)) {
        notes.emplace_back(note.track, note.start, note.length, note.key, note.velocity);
    }
    std::ranges::sort(notes);
    return notes;
}

} // namespace

TEST_CASE("MIDI benchmarks", "[benchmark][io]") {
    const elara::core::Song song = elara::bench::largeSong();
    const elara::io::ExportResult exported = elara::io::exportMidi(song);
    const std::vector<std::uint8_t> bytes(exported.data.begin(), exported.data.end());
    const auto back = elara::io::importMidi(bytes);
    REQUIRE(back.has_value());
    REQUIRE(back->warnings.empty());
    REQUIRE(playedNotes(back->song) == playedNotes(song)); // fret positions are not in MIDI
    std::printf("large song as MIDI: %zu bytes\n", exported.data.size());

    BENCHMARK("export the large song as MIDI") {
        return elara::io::exportMidi(song).data.size();
    };
    BENCHMARK("export the large song as MIDI on one thread") {
        return elara::io::exportMidi(song, {.threads = 1}).data.size();
    };
    BENCHMARK("import the large song from MIDI") {
        return elara::io::importMidi(bytes)->song.measureCount();
    };
    BENCHMARK("import the large song from MIDI on one thread") {
        return elara::io::importMidi(bytes, {.threads = 1})->song.measureCount();
    };
    BENCHMARK("sounding notes of the large song") {
        return elara::core::soundingNotes(song).size();
    };
}

namespace {

// The large song as Guitar Pro 7-8 store it: GPIF with `trackCount` guitar
// tracks x 500 measures, eight eighth-note beats of two notes, every bar,
// voice, beat and note under its own id.
std::string largeGpif(int trackCount = 20) {
    std::string tracks;
    for (int t = 0; t < trackCount; ++t) {
        tracks += std::format(
            R"(<Track id="{}"><Name><![CDATA[Guitar {}]]></Name><InstrumentSet>)"
            "<Name>Electric Guitar</Name><Type>electricGuitar</Type></InstrumentSet>"
            "<Transpose><Chromatic>0</Chromatic><Octave>-1</Octave></Transpose><Sounds><Sound>"
            "<MIDI><Program>27</Program></MIDI></Sound></Sounds><Staves><Staff><Properties>"
            R"(<Property name="CapoFret"><Fret>0</Fret></Property><Property name="Tuning">)"
            "<Pitches>40 45 50 55 59 64</Pitches></Property></Properties></Staff></Staves>"
            "</Track>",
            t, t + 1);
    }
    std::string masterBars;
    std::string bars;
    std::string voices;
    std::string beats;
    std::string notes;
    int bar = 0;
    int beat = 0;
    int note = 0;
    for (int m = 0; m < 500; ++m) {
        masterBars += "<MasterBar><Key><AccidentalCount>0</AccidentalCount><Mode>Major</Mode>"
                      "</Key><Time>4/4</Time><Bars>";
        for (int t = 0; t < trackCount; ++t, ++bar) {
            masterBars += std::format("{}{}", t == 0 ? "" : " ", bar);
            bars += std::format(
                R"(<Bar id="{0}"><Clef>G2</Clef><Voices>{0} -1 -1 -1</Voices></Bar>)", bar);
            voices += std::format(R"(<Voice id="{}"><Beats>)", bar);
            for (int b = 0; b < 8; ++b, ++beat) {
                voices += std::format("{}{}", b == 0 ? "" : " ", beat);
                beats +=
                    std::format(R"(<Beat id="{}"><Rhythm ref="0"/><Notes>{} {}</Notes></Beat>)",
                                beat, note, note + 1);
                for (int s = 0; s < 2; ++s, ++note) {
                    notes += std::format(
                        R"(<Note id="{}"><Properties><Property name="Fret"><Fret>{}</Fret>)"
                        R"(</Property><Property name="Midi"><Number>{}</Number></Property>)"
                        R"(<Property name="String"><String>{}</String></Property></Properties>)"
                        "</Note>",
                        note, b % 5, (s == 0 ? 40 : 45) + b % 5, s);
                }
            }
            voices += "</Beats></Voice>";
        }
        masterBars += "</Bars></MasterBar>";
    }
    std::string trackIds;
    for (int t = 0; t < trackCount; ++t) {
        trackIds += std::format("{}{}", t == 0 ? "" : " ", t);
    }
    return R"(<?xml version="1.0" encoding="utf-8"?><GPIF><Score><Title><![CDATA[Large]]>)"
           "</Title></Score><MasterTrack><Tracks>" +
           trackIds + "</Tracks></MasterTrack><Tracks>" + tracks + "</Tracks><MasterBars>" +
           masterBars + "</MasterBars><Bars>" + bars + "</Bars><Voices>" + voices +
           "</Voices><Beats>" + beats + "</Beats><Notes>" + notes +
           R"(</Notes><Rhythms><Rhythm id="0"><NoteValue>Eighth</NoteValue></Rhythm></Rhythms>)"
           "</GPIF>";
}

void putLe32(std::vector<std::uint8_t>& bytes, std::size_t at, std::size_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

// A Guitar Pro 6 container holding `score` (at most about 4 MB: one entry sector
// lists its data sectors) as score.gpif, compressed as literal bytes only: the
// slowest stream to unpack per byte, since back references copy whole runs.
std::vector<std::uint8_t> literalGpx(const std::string& score) {
    constexpr std::size_t kSector = 0x1000;
    const std::size_t count = (score.size() + kSector - 1) / kSector;
    std::vector<std::uint8_t> fs(4 + (count + 2) * kSector, 0);
    fs[0] = 'B';
    fs[1] = 'C';
    fs[2] = 'F';
    fs[3] = 'S';
    const std::size_t entry = 4 + kSector;
    putLe32(fs, entry, 2);
    const std::string_view name = "score.gpif";
    for (std::size_t i = 0; i < name.size(); ++i) {
        fs[entry + 4 + i] = static_cast<std::uint8_t>(name[i]);
    }
    putLe32(fs, entry + 0x8C, score.size());
    REQUIRE(0x94 + 4 * count < kSector); // one entry sector lists every data sector
    for (std::size_t i = 0; i < count; ++i) {
        putLe32(fs, entry + 0x94 + 4 * i, 2 + i);
    }
    for (std::size_t i = 0; i < score.size(); ++i) {
        fs[entry + kSector + i] = static_cast<std::uint8_t>(score[i]);
    }

    std::vector<std::uint8_t> packed{'B', 'C', 'F', 'Z', 0, 0, 0, 0};
    putLe32(packed, 4, fs.size());
    std::uint64_t buffer = 0;
    unsigned used = 0;
    const auto put = [&](std::uint64_t value, unsigned bits) {
        buffer = (buffer << bits) | value;
        used += bits;
        while (used >= 8) {
            used -= 8;
            packed.push_back(static_cast<std::uint8_t>((buffer >> used) & 0xFFU));
        }
    };
    for (std::size_t at = 0; at < fs.size(); at += 3) {
        const std::size_t chunk = std::min<std::size_t>(3, fs.size() - at);
        put(0, 1);
        put(chunk & 1U, 1); // the count, least significant bit first
        put((chunk >> 1U) & 1U, 1);
        for (std::size_t i = 0; i < chunk; ++i) {
            put(fs[at + i], 8);
        }
    }
    if (used > 0) {
        put(0, 8 - used);
    }
    return packed;
}

} // namespace

TEST_CASE("Guitar Pro import benchmarks", "[benchmark][io]") {
    const std::string gpif = largeGpif();
    const auto imported = elara::io::importGpif(gpif);
    REQUIRE(imported.has_value());
    REQUIRE(imported->warnings.empty());
    REQUIRE(playedNotes(imported->song) == playedNotes(elara::bench::largeSong()));
    const std::string small = largeGpif(2);
    const std::vector<std::uint8_t> gpx = literalGpx(small);
    const auto unpacked = elara::io::importGuitarPro(gpx);
    REQUIRE(unpacked.has_value());
    REQUIRE(unpacked->song == elara::io::importGpif(small)->song);
    std::printf("large song as GPIF: %zu bytes; 2 of its tracks: %zu bytes, %zu as a "
                "literal-only .gpx\n",
                gpif.size(), small.size(), gpx.size());

    BENCHMARK("import the large song from GPIF") {
        return elara::io::importGpif(gpif)->song.measureCount();
    };
    BENCHMARK("import the large song from GPIF on one thread") {
        return elara::io::importGpif(gpif, {.threads = 1})->song.measureCount();
    };
    BENCHMARK("import 2 tracks x 500 measures from GPIF") {
        return elara::io::importGpif(small)->song.measureCount();
    };
    BENCHMARK("import the same from a .gpx container (literal bytes only)") {
        return elara::io::importGuitarPro(gpx)->song.measureCount();
    };
}
