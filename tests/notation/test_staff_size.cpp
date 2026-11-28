// SPDX-License-Identifier: AGPL-3.0-or-later
// The staff size that lets the staves shown fit a page (ADR-0171).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/staff_size.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::EngravingFont;
using elara::notation::fittedStaffSpace;
using elara::notation::kA4;
using elara::notation::kLeastStaffShare;
using elara::notation::kLeastStaffSpace;
using elara::notation::layoutPages;
using elara::notation::narrowedStaffSpace;
using elara::notation::pageOptions;
using elara::notation::selectTracks;
using elara::notation::StaffSelection;
using elara::notation::TrackView;
using elara::notation::typicalSystemHeight;
using elara::notation::WidthOverflow;
using elara::notation::widthOverflow;

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

// `guitars` guitar tracks of `measures` measures, each playing four quarter
// notes a measure across the strings.
Song guitars(std::size_t guitars, std::size_t measures) {
    Song song;
    song.appendMeasures(measures);
    for (std::size_t t = 0; t < guitars; ++t) {
        song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .writtenOffset = 12},
                            {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
        for (std::size_t m = 0; m < measures; ++m) {
            auto& beats = song.track(t).staff(0).measure(m).voices[0].beats;
            for (int string = 1; string <= 4; ++string) {
                const auto fret = static_cast<std::int8_t>((m + t) % 5);
                beats.push_back(Beat{
                    .duration = Duration(NoteValue::Quarter),
                    .notes = {Note{
                        .pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                        .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                                 .fret = fret}}}});
            }
        }
    }
    return song;
}

std::vector<std::size_t> all(const Song& song) {
    std::vector<std::size_t> tracks;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        tracks.push_back(t);
    }
    return tracks;
}

std::vector<StaffSelection> both(const Song& song) {
    const auto tracks = all(song);
    return selectTracks(song, tracks, TrackView::Both);
}

// A guitar playing three measures, the second of `sixteenths` sixteenth notes
// with two-digit frets in a time signature as long: wider than an A4 page at
// 1.75 mm from about forty of them.
Song wideMeasure(int sixteenths) {
    Song song = guitars(1, 3);
    song.measureHeader(1).timeSignature = TimeSignature(sixteenths / 4, 4);
    auto& beats = song.track(0).staff(0).measure(1).voices[0].beats;
    beats.clear();
    for (int b = 0; b < sixteenths; ++b) {
        const auto fret = static_cast<std::int8_t>(10 + b % 5);
        beats.push_back(
            Beat{.duration = Duration(NoteValue::Sixteenth),
                 .notes = {Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(1, fret)),
                                .position = FretPosition{.string = 1, .fret = fret}}}});
    }
    return song;
}

} // namespace

TEST_CASE("Staff size: one track keeps the staff space asked for", "[notation]") {
    const Song song = guitars(1, 4);
    CHECK(fittedStaffSpace(song, both(song), kA4) == 1.75);
    CHECK(fittedStaffSpace(song, both(song), kA4, 1.2) == 1.2);
}

TEST_CASE("Staff size: more tracks shown at once get smaller staves, never larger ones",
          "[notation]") {
    double before = 1.75;
    double eight = 0.0;
    for (std::size_t count = 1; count <= 10; ++count) {
        const Song song = guitars(count, 1);
        const double space = fittedStaffSpace(song, both(song), kA4);
        INFO(count << " guitars: " << space << " mm");
        CHECK(space <= before);
        CHECK(space >= kLeastStaffShare * 1.75 - 1e-9);
        before = space;
        if (count == 8) {
            eight = space;
        }
    }
    CHECK(eight < 1.75);
    // Notation alone is lower than notation over tablature: it keeps more.
    const Song song = guitars(8, 1);
    const auto tracks = all(song);
    CHECK(fittedStaffSpace(song, selectTracks(song, tracks, TrackView::Standard), kA4) > eight);
}

TEST_CASE("Staff size: at the size given, two systems fit a page", "[notation]") {
    for (const std::size_t count : {4U, 6U, 8U}) {
        const Song song = guitars(count, 24);
        const auto staves = both(song);
        const double space = fittedStaffSpace(song, staves, kA4);
        if (space <= kLeastStaffShare * 1.75 + 0.01) {
            continue; // the least share: two systems are no longer promised
        }
        auto options = pageOptions(kA4, space);
        options.credits = false;
        const auto pages = layoutPages(song, staves, bravura(), options);
        REQUIRE(pages.size() >= 2);
        INFO(count << " guitars at " << space << " mm");
        CHECK(pages.front().systems.size() >= 2);
    }
}

TEST_CASE("Staff size: very many staves get one system a page, as small as it needs",
          "[notation]") {
    const Song song = guitars(40, 1);
    const auto staves = both(song);
    const double space = fittedStaffSpace(song, staves, kA4);
    CHECK(space < kLeastStaffShare * 1.75);
    CHECK(space >= kLeastStaffSpace);
    const double room = kA4.height - 2.0 * 15.0;
    CHECK((typicalSystemHeight(song, staves) * space <= room + 1e-9 || space == kLeastStaffSpace));
}

TEST_CASE("Staff size: it depends on the staves shown, not on the music", "[notation]") {
    const Song busy = guitars(6, 8);
    const Song quiet = guitars(6, 1);
    CHECK(fittedStaffSpace(busy, both(busy), kA4) == fittedStaffSpace(quiet, both(quiet), kA4));
}

TEST_CASE("Staff size: margins that leave no room are refused", "[notation]") {
    const Song song = guitars(2, 1);
    CHECK_THROWS_AS(fittedStaffSpace(song, both(song), kA4, 1.75, 150.0), std::invalid_argument);
    CHECK_THROWS_AS(fittedStaffSpace(song, both(song), kA4, 0.0), std::invalid_argument);
}

TEST_CASE("Staff size: a measure wider than the page fits it at the narrowed staff space",
          "[notation]") {
    const Song song = wideMeasure(48);
    const auto staves = both(song);
    const auto options = pageOptions(kA4, 1.75);
    const WidthOverflow over =
        widthOverflow(layoutPages(song, staves, bravura(), options), options);
    REQUIRE(over.amount > 1.0);
    CHECK(over.measure == 1);

    const double space = narrowedStaffSpace(kA4, 1.75, 1.75, over.amount);
    INFO(over.amount << " staff spaces past the margin: " << space << " mm");
    CHECK(space < 1.75);
    CHECK(space >= kLeastStaffShare * 1.75 - 1e-9);
    // There the measure ends inside the margin; a little larger and it would not.
    const auto narrowed = pageOptions(kA4, space);
    CHECK(widthOverflow(layoutPages(song, staves, bravura(), narrowed), narrowed).amount == 0.0);
    const auto larger = pageOptions(kA4, space + 0.02);
    CHECK(widthOverflow(layoutPages(song, staves, bravura(), larger), larger).amount > 0.0);
}

TEST_CASE("Staff size: pages whose systems end inside the margin are left alone", "[notation]") {
    const Song song = guitars(1, 12);
    const auto staves = both(song);
    const auto options = pageOptions(kA4, 1.75);
    const WidthOverflow over =
        widthOverflow(layoutPages(song, staves, bravura(), options), options);
    CHECK(over.amount == 0.0);
    CHECK(narrowedStaffSpace(kA4, 1.75, 1.75, over.amount) == 1.75);
    CHECK(narrowedStaffSpace(kA4, 1.75, 1.4, -3.0) == 1.4);
}

TEST_CASE("Staff size: narrowing stops at the least share of a part's staff", "[notation]") {
    const double least = kLeastStaffShare * 1.75;
    // A system twice as wide as the page: as small as it goes, and still too wide.
    const double page = (kA4.width - 2.0 * 15.0) / 1.75;
    CHECK(std::abs(narrowedStaffSpace(kA4, 1.75, 1.75, page) - least) < 1e-9);
    // Staves smaller than that already, as every track at once has them, stay.
    CHECK(narrowedStaffSpace(kA4, 1.75, 0.8, 10.0) == 0.8);
    // A view narrowed once is narrowed further from where it is.
    const double further = narrowedStaffSpace(kA4, 1.75, 1.5, 5.0);
    CHECK(further < 1.5);
    CHECK(further >= least - 1e-9);
    // Hundredths of a millimetre, rounded down.
    CHECK(std::abs(further * 100.0 - std::round(further * 100.0)) < 1e-6);

    CHECK_THROWS_AS(narrowedStaffSpace(kA4, 1.75, 1.75, 5.0, 150.0), std::invalid_argument);
    CHECK_THROWS_AS(narrowedStaffSpace(kA4, 0.0, 1.75, 5.0), std::invalid_argument);
    CHECK_THROWS_AS(narrowedStaffSpace(kA4, 1.75, 0.0, 5.0), std::invalid_argument);
}
