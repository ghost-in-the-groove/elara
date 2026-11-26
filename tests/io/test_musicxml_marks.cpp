// SPDX-License-Identifier: AGPL-3.0-or-later
// The MusicXML marks the reader and the writer share (src/io/src/musicxml_shared.hpp):
// every mark the writer writes reads back as itself, and every alias only the
// reader accepts reads as the marks it stands for.
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include "musicxml_shared.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace core = elara::core;
namespace detail = elara::io::detail;
using elara::io::exportMusicXml;
using elara::io::importMusicXml;

namespace {

/// A piano holding one whole C5 in its only measure.
core::Song wholeNote() {
    core::Song song;
    song.appendMeasures(1);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    song.track(p).staff(0).measure(0).voices[0].beats = {
        core::Beat{.duration = core::Duration(core::NoteValue::Whole),
                   .notes = {core::Note{.pitch = core::Pitch::fromMidi(72)}}}};
    return song;
}

core::Beat& beatOf(core::Song& song, std::size_t beat = 0) {
    return song.track(0).staff(0).measure(0).voices[0].beats.at(beat);
}

/// Exports `song`, finds `written` in the document and reads the same song back.
void checkRoundTrip(const core::Song& song, std::string_view written) {
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    CHECK(exported.data.find(written) != std::string::npos);
    const auto back = importMusicXml(exported.data);
    REQUIRE(back.has_value());
    CHECK(back->warnings.empty());
    if (back->song != song) {
        FAIL_CHECK(std::format("read back as\n{}\ninstead of\n{}",
                               elara::io::songToJson(back->song), elara::io::songToJson(song)));
    }
}

/// A one-part score whose only measure holds a whole C5 with `notations`.
std::string noteWith(std::string_view notations) {
    return std::format(
        R"(<score-partwise version="4.0"><part-list><score-part id="P1"><part-name>Test)"
        R"(</part-name></score-part></part-list><part id="P1"><measure number="1"><attributes>)"
        R"(<divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>)"
        R"(</attributes><note><pitch><step>C</step><octave>5</octave></pitch>)"
        R"(<duration>4</duration><voice>1</voice><type>whole</type><notations>{})"
        R"(</notations></note></measure></part></score-partwise>)",
        notations);
}

core::Song readSong(std::string_view xml) {
    const auto result = importMusicXml(xml);
    REQUIRE(result.has_value());
    CHECK(result->warnings.empty());
    return result->song;
}

} // namespace

TEST_CASE("MusicXML marks: every mark comes back from export as itself", "[musicxml][marks]") {
    for (const detail::ArticulationMark& mark : detail::kArticulations) {
        INFO("articulation " << mark.name);
        core::Song song = wholeNote();
        core::Beat& beat = beatOf(song);
        detail::addMark(mark, beat.notes[0].effects, beat.effects.breath);
        REQUIRE(song != wholeNote());
        checkRoundTrip(song, std::format("<{}/>", mark.name));
    }
    for (const auto& [name, ornament] : detail::kOrnaments) {
        INFO("ornament " << name);
        core::Song song = wholeNote();
        beatOf(song).notes[0].effects.ornament = ornament;
        checkRoundTrip(song, std::format("<{}/>", name));
    }
    for (const auto& [name, stroke] : detail::kPickStrokes) {
        INFO("pick stroke " << name);
        core::Song song = wholeNote();
        beatOf(song).effects.pickStroke = stroke;
        checkRoundTrip(song, std::format("<{}/>", name));
    }
    for (std::size_t s = 0; s < detail::kTremoloSpeeds.size(); ++s) {
        const int strokes = static_cast<int>(s) + 1;
        INFO("tremolo strokes " << strokes);
        core::Song song = wholeNote();
        beatOf(song).effects.tremoloPicking = detail::kTremoloSpeeds[s];
        CHECK(detail::tremoloStrokes(detail::kTremoloSpeeds[s]) == strokes);
        checkRoundTrip(song, std::format(R"(<tremolo type="single">{}</tremolo>)", strokes));
    }
    for (const auto& [name, dynamic] : detail::kDynamics) {
        // Written where the dynamic changes: after the loudest or the softest.
        INFO("dynamic " << name);
        core::Song song = wholeNote();
        core::Beat& first = beatOf(song);
        first.duration = core::Duration(core::NoteValue::Half);
        first.notes[0].effects.dynamic =
            dynamic == core::Dynamic::FFF ? core::Dynamic::PPP : core::Dynamic::FFF;
        core::Beat second = first;
        second.notes[0].effects.dynamic = dynamic;
        song.track(0).staff(0).measure(0).voices[0].beats.push_back(second);
        checkRoundTrip(song, std::format("<{}/>", name));
    }
}

TEST_CASE("MusicXML marks: what only the reader accepts reads as the marks it stands for",
          "[musicxml][marks]") {
    struct Alias {
        std::string_view read;    // in <notations>
        std::string_view meaning; // the same, as the writer writes it
    };
    static constexpr std::array kAliases{
        Alias{"<articulations><spiccato/></articulations>",
              "<articulations><staccato/></articulations>"},
        Alias{"<articulations><detached-legato/></articulations>",
              "<articulations><staccato/><tenuto/></articulations>"},
        // Softer than ppp and louder than fff are the softest and the loudest.
        Alias{"<dynamics><pppp/></dynamics>", "<dynamics><ppp/></dynamics>"},
        Alias{"<dynamics><pppppp/></dynamics>", "<dynamics><ppp/></dynamics>"},
        Alias{"<dynamics><ffff/></dynamics>", "<dynamics><fff/></dynamics>"},
        Alias{"<dynamics><ffffff/></dynamics>", "<dynamics><fff/></dynamics>"},
        // A tremolo without a type is a single one, without a number three
        // strokes, and with too few or too many strokes the nearest speed.
        Alias{R"(<ornaments><tremolo type="single"/></ornaments>)",
              R"(<ornaments><tremolo type="single">3</tremolo></ornaments>)"},
        Alias{"<ornaments><tremolo>2</tremolo></ornaments>",
              R"(<ornaments><tremolo type="single">2</tremolo></ornaments>)"},
        Alias{R"(<ornaments><tremolo type="single">0</tremolo></ornaments>)",
              R"(<ornaments><tremolo type="single">1</tremolo></ornaments>)"},
        Alias{R"(<ornaments><tremolo type="single">5</tremolo></ornaments>)",
              R"(<ornaments><tremolo type="single">3</tremolo></ornaments>)"},
    };
    const core::Song plain = readSong(noteWith(""));
    for (const auto& [alias, meaning] : kAliases) {
        INFO(alias);
        const core::Song song = readSong(noteWith(alias));
        CHECK(song != plain);
        CHECK(song == readSong(noteWith(meaning)));
    }
    // Every alias of the shared table is checked above.
    for (const detail::ArticulationMark& alias : detail::kArticulationAliases) {
        INFO("alias " << alias.name);
        CHECK(std::ranges::any_of(kAliases, [&](const Alias& checked) {
            return checked.read.find(std::format("<{}/>", alias.name)) != std::string_view::npos;
        }));
    }
}
