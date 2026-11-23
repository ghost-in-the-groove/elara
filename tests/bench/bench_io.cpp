// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmarks for elara_io: the native format and MusicXML import.
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
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
