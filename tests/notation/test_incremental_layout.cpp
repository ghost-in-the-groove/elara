// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>
#include <elara/core/song.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include "song_edits.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SharedOptional;
using elara::core::Song;
using elara::core::Syllable;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::Track;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::layoutPages;
using elara::notation::PageLayoutCache;
using elara::notation::PageLayoutOptions;
using elara::notation::selectTracks;
using elara::notation::StaffSelection;
using elara::notation::TrackView;
using elara::notation::test::edit;

namespace {

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// A corpus file played through until it has at least `measures` measures, so
// that its pages hold enough systems to reuse.
std::shared_ptr<const Song> open(const std::string& name, std::size_t measures) {
    auto opened = elara::io::openFile(std::filesystem::path(ELARA_CORPUS_DIR) / name);
    REQUIRE(opened.has_value());
    Song song = std::move(opened->song);
    const std::size_t written = song.measureCount();
    REQUIRE(written > 0);
    while (song.measureCount() < measures) {
        const std::size_t m = song.measureCount();
        song.appendMeasures(1);
        song.measureHeader(m) = song.measureHeader(m % written);
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
                auto& staff = song.track(t).staff(s);
                staff.measure(m) = staff.measure(m % written);
            }
        }
    }
    return std::make_shared<const Song>(std::move(song));
}

std::vector<StaffSelection> everyStaff(const Song& song) {
    std::vector<std::size_t> tracks(song.trackCount());
    std::iota(tracks.begin(), tracks.end(), std::size_t{0});
    return selectTracks(song, tracks, TrackView::Both);
}

} // namespace

TEST_CASE("Laying out again after an edit gives the pages a full layout gives",
          "[notation][pages][incremental]") {
    const PageLayoutOptions options;
    std::size_t systems = 0;
    std::size_t laidOut = 0;
    for (const char* name : {"gp/band.gp", "gp/guitar-techniques.gp", "gp/techniques.gp5",
                             "gp/dal-segno.gp5", "gp/quartet.gp4", "musicxml/piano-voices.musicxml",
                             "musicxml/guitar-tab.musicxml", "musicxml/drums.musicxml"}) {
        CAPTURE(name);
        std::shared_ptr<const Song> song = open(name, 40);
        const std::vector<StaffSelection> staves = everyStaff(*song);
        PageLayoutCache cache;
        REQUIRE(cache.layout(song, staves, bravura(), options) ==
                layoutPages(*song, staves, bravura(), options));
        std::mt19937 random(7);
        for (int step = 0; step < 60; ++step) {
            CAPTURE(step);
            Song next = *song;
            edit(next, random);
            song = std::make_shared<const Song>(std::move(next));
            const std::vector<DisplayList> again = cache.layout(song, staves, bravura(), options);
            REQUIRE(again == layoutPages(*song, staves, bravura(), options));
            systems += cache.lastReuse().systems;
            laidOut += cache.lastReuse().laidOut;
        }
    }
    // Most systems were reused, so the comparisons above checked reuse.
    CHECK(laidOut * 2 < systems);
}

TEST_CASE("An edit lays out again only the measures and systems around it",
          "[notation][pages][incremental]") {
    Song song;
    song.addTrack(Track("Lead", {}, {elara::core::Staff{}}));
    song.appendMeasures(96);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        auto& voice = song.track(0).staff(0).measure(m).voices[0];
        for (int beat = 0; beat < 4; ++beat) {
            voice.beats.push_back(Beat{.duration = Duration(NoteValue::Quarter),
                                       .notes = {Note{.pitch = Pitch::fromMidi(60 + beat)}}});
        }
    }
    const std::vector<StaffSelection> staves = everyStaff(song);
    const PageLayoutOptions options;
    PageLayoutCache cache;

    auto first = std::make_shared<const Song>(song);
    const auto pages = cache.layout(first, staves, bravura(), options);
    const PageLayoutCache::Reuse full = cache.lastReuse();
    CHECK(full.measured == 96);
    CHECK(full.laidOut == full.systems);
    REQUIRE(full.systems > 10);

    // The same song again lays out nothing.
    CHECK(cache.layout(first, staves, bravura(), options) == pages);
    CHECK(cache.lastReuse().measured == 0);
    CHECK(cache.lastReuse().laidOut == 0);

    // One note of measure 50, D4 to E4, which keeps its width and so the line
    // breaks: that measure and the one after it are measured again (it may
    // change how the next one starts), and only the systems that read it are
    // laid out again. (A note that gains or loses an accidental changes the
    // width, and the breaks may move over several systems.)
    song.track(0).staff(0).measure(50).voices[0].beats[2].notes.front().pitch = Pitch::fromMidi(64);
    auto edited = std::make_shared<const Song>(song);
    CHECK(cache.layout(edited, staves, bravura(), options) ==
          layoutPages(*edited, staves, bravura(), options));
    const PageLayoutCache::Reuse reuse = cache.lastReuse();
    CHECK(reuse.measured == 2);
    CHECK(reuse.laidOut >= 1);
    CHECK(reuse.laidOut <= 3);
    CHECK(reuse.systems == full.systems);

    // Another number of measures starts again.
    song.appendMeasures(1);
    auto longer = std::make_shared<const Song>(song);
    CHECK(cache.layout(longer, staves, bravura(), options) ==
          layoutPages(*longer, staves, bravura(), options));
    CHECK(cache.lastReuse().measured == 97);
}

TEST_CASE("An edit to the tempo a gradual change goes to lays out the ramp's systems again",
          "[notation][pages][incremental]") {
    // ADR-0082: "rit." at measure 2 runs to the change at measure 40, many
    // systems later; that change decides the word and where the line ends.
    Song song;
    song.addTrack(Track("Lead", {}, {elara::core::Staff{}}));
    song.appendMeasures(48);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        auto& voice = song.track(0).staff(0).measure(m).voices[0];
        for (int beat = 0; beat < 4; ++beat) {
            voice.beats.push_back(Beat{.duration = Duration(NoteValue::Quarter),
                                       .notes = {Note{.pitch = Pitch::fromMidi(60 + beat)}}});
        }
    }
    song.measureHeader(2).tempoChanges = {
        TempoChange{.offset = {}, .tempo = Tempo(120.0), .gradual = true}};
    song.measureHeader(40).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(60.0)}};
    const std::vector<StaffSelection> staves = everyStaff(song);
    const PageLayoutOptions options;
    PageLayoutCache cache;
    (void)cache.layout(std::make_shared<const Song>(song), staves, bravura(), options);

    // Faster: "accel."; gone: no ramp at all; back: "rit." again.
    for (const std::optional<double> bpm :
         {std::optional(180.0), std::optional<double>(), std::optional(60.0)}) {
        CAPTURE(bpm.value_or(0.0));
        song.measureHeader(40).tempoChanges.clear();
        if (bpm) {
            song.measureHeader(40).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(*bpm)}};
        }
        const auto edited = std::make_shared<const Song>(song);
        CHECK(cache.layout(edited, staves, bravura(), options) ==
              layoutPages(*edited, staves, bravura(), options));
    }
}

TEST_CASE("An edit to a syllable held into later systems lays those systems out again",
          "[notation][pages][incremental]") {
    // ADR-0084: a syllable held over notes draws its line on into the systems
    // after it, up to 16 measures on, so those read measures well before
    // their first.
    Song song;
    song.addTrack(Track("Lead", {}, {elara::core::Staff{}}));
    song.appendMeasures(24);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        auto& voice = song.track(0).staff(0).measure(m).voices[0];
        for (int beat = 0; beat < 4; ++beat) {
            voice.beats.push_back(Beat{.duration = Duration(NoteValue::Quarter),
                                       .notes = {Note{.pitch = Pitch::fromMidi(60 + beat)}}});
        }
    }
    const auto sing = [&song](std::size_t m, std::optional<Syllable> syllable) {
        auto& beat = song.track(0).staff(0).measure(m).voices[0].beats[0];
        beat.lyrics = syllable ? SharedOptional(std::vector<Syllable>{*syllable})
                               : SharedOptional<std::vector<Syllable>>();
    };
    sing(0, Syllable{.text = "Ah", .extend = true}); // held to measure 16
    const std::vector<StaffSelection> staves = everyStaff(song);
    const PageLayoutOptions options;
    PageLayoutCache cache;
    auto first = std::make_shared<const Song>(song);
    REQUIRE(cache.layout(first, staves, bravura(), options) ==
            layoutPages(*first, staves, bravura(), options));
    REQUIRE(cache.lastReuse().systems >= 3);

    // A syllable at measure 2 ends the hold there; taken away, it is held on
    // again; then the first syllable is no longer held.
    for (const int step : {0, 1, 2}) {
        CAPTURE(step);
        if (step == 0) {
            sing(2, Syllable{.text = "la"});
        } else if (step == 1) {
            sing(2, std::nullopt);
        } else {
            sing(0, Syllable{.text = "Ah"});
        }
        const auto edited = std::make_shared<const Song>(song);
        CHECK(cache.layout(edited, staves, bravura(), options) ==
              layoutPages(*edited, staves, bravura(), options));
        CHECK(cache.lastReuse().laidOut < cache.lastReuse().systems);
    }
}
