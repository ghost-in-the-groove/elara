// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>
#include <elara/core/song.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

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
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Measure;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Ottava;
using elara::core::Pitch;
using elara::core::Song;
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

std::size_t pick(std::mt19937& random, std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(random);
}

// The first note of `measure`, or null.
Note* firstNote(Measure& measure) {
    for (auto& voice : measure.voices) {
        for (auto& beat : voice.beats) {
            if (!beat.notes.empty()) {
                return &beat.notes.front();
            }
        }
    }
    return nullptr;
}

// One edit of the kinds an editor makes: a measure's notes, a note's effects, a
// measure's header or a staff's clef, at a random place.
void edit(Song& song, std::mt19937& random) {
    const std::size_t m = pick(random, song.measureCount());
    const std::size_t t = pick(random, song.trackCount());
    Track& track = song.track(t);
    auto& staff = track.staff(pick(random, track.staffCount()));
    auto& header = song.measureHeader(m);
    switch (pick(random, 12)) {
    case 0: // another measure's notes, as paste does
        staff.measure(m) = staff.measure(pick(random, song.measureCount()));
        break;
    case 1:
        staff.measure(m) = Measure{};
        break;
    case 2:
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.tiedFromPrevious = !note->effects.tiedFromPrevious;
        }
        break;
    case 3:
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.palmMute = !note->effects.palmMute;
            note->effects.letRing = pick(random, 2) == 0;
        }
        break;
    case 4: {
        constexpr Dynamic kDynamics[] = {Dynamic::P, Dynamic::MF, Dynamic::FF};
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.dynamic = kDynamics[pick(random, 3)];
        }
        break;
    }
    case 5:
        if (header.tempoChanges.empty()) {
            header.tempoChanges.push_back(TempoChange{
                .offset = {}, .tempo = Tempo(60.0 + static_cast<double>(pick(random, 120)))});
        } else {
            header.tempoChanges.clear();
        }
        break;
    case 6:
        header.repeatStart = !header.repeatStart;
        break;
    case 8:
        song.setStartTempo(Tempo(60.0 + static_cast<double>(pick(random, 120))));
        break;
    case 7:
        header.repeatTimes =
            header.repeatEnd() ? 0 : static_cast<std::uint8_t>(2 + pick(random, 3));
        header.alternateEndings = static_cast<std::uint8_t>(pick(random, 4));
        break;
    case 9: { // a clef change, or none (ADR-0087)
        constexpr Clef kClefs[] = {Clef::Treble, Clef::Bass, Clef::Alto};
        const std::size_t which = pick(random, 4);
        staff.measure(m).clef = which < 3 ? std::optional(kClefs[which]) : std::nullopt;
        break;
    }
    case 10: { // an octave line over the whole measure, running on into its neighbours
        constexpr Ottava kOttavas[] = {Ottava::None, Ottava::Alta, Ottava::Bassa};
        const Ottava ottava = kOttavas[pick(random, 3)];
        for (auto& voice : staff.measure(m).voices) {
            for (auto& beat : voice.beats) {
                beat.effects.ottava = ottava;
            }
        }
        break;
    }
    default:
        header.keySignature = KeySignature(static_cast<int>(pick(random, 15)) - 7, KeyMode::Major);
        break;
    }
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
    for (const std::optional<double> bpm : {std::optional(180.0), std::optional<double>(),
                                            std::optional(60.0)}) {
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
