// SPDX-License-Identifier: AGPL-3.0-or-later
// Slides on the standard staff: lines between notes, into and out of a note
// (ADR-0098).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::FretPosition;
using elara::core::Grace;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SlideIn;
using elara::core::SlideOut;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::LineItem;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;

namespace {

constexpr double kEpsilon = 1e-6;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// A quarter of `pitch`, a rest when empty.
Beat beat(std::string_view pitch) {
    Beat made{.duration = Duration(NoteValue::Quarter)};
    if (!pitch.empty()) {
        made.notes.push_back(Note{.pitch = *Pitch::parse(pitch)});
    }
    return made;
}

Beat slidingOut(Beat made, SlideOut slide) {
    for (Note& note : made.notes) {
        note.effects.slideOut = slide;
    }
    return made;
}

Beat slidingIn(Beat made, SlideIn slide) {
    for (Note& note : made.notes) {
        note.effects.slideIn = slide;
    }
    return made;
}

Beat on(Beat made, int string, int fret) {
    made.notes.at(0).position = FretPosition{.string = static_cast<std::int8_t>(string),
                                             .fret = static_cast<std::int8_t>(fret)};
    return made;
}

Song treble(const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

DisplayList layout(const Song& song, const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, 0, bravura(), options);
}

std::vector<LineItem> slides(const DisplayList& list) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found),
                         [](const LineItem& line) { return line.role == Role::Slide; });
    std::ranges::stable_sort(found, {}, [](const LineItem& line) { return line.from.x; });
    return found;
}

std::vector<GlyphItem> byRole(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x * item.scale;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x * item.scale;
}

} // namespace

TEST_CASE("Slides: a line from a note to the note it slides to", "[notation][layout][slide]") {
    SECTION("a shift slide up, from notehead to notehead") {
        const DisplayList list = layout(
            treble({{slidingOut(beat("C5"), SlideOut::Shift), beat("E5"), beat(""), beat("")}}));
        const auto found = slides(list);
        REQUIRE(found.size() == 1);
        const auto heads = byRole(list, Role::Notehead);
        REQUIRE(heads.size() == 2);
        CHECK(found[0].from.x > right(heads[0]));
        CHECK(found[0].from.x < right(heads[0]) + 0.5);
        CHECK(found[0].to.x < left(heads[1]));
        CHECK(found[0].to.x > left(heads[1]) - 0.5);
        CHECK_THAT(found[0].from.y, WithinAbs(heads[0].origin.y, 0.3));
        CHECK_THAT(found[0].to.y, WithinAbs(heads[1].origin.y, 0.3));
    }
    SECTION("a legato slide down") {
        const DisplayList list = layout(
            treble({{slidingOut(beat("E5"), SlideOut::Legato), beat("C5"), beat(""), beat("")}}));
        const auto found = slides(list);
        REQUIRE(found.size() == 1);
        CHECK(found[0].to.y > found[0].from.y);
    }
    SECTION("to before the accidental of the note it slides to") {
        const DisplayList list = layout(
            treble({{slidingOut(beat("C5"), SlideOut::Shift), beat("F#5"), beat(""), beat("")}}));
        const auto found = slides(list);
        REQUIRE(found.size() == 1);
        const auto sharps = byRole(list, Role::Accidental);
        REQUIRE(sharps.size() == 1);
        CHECK(found[0].to.x < left(sharps[0]));
    }
    SECTION("on a fretted staff, to the next note on its string only") {
        // A slides to the B three beats on, on its string; nothing is drawn
        // to the C on another string, nor past it.
        const DisplayList list =
            layout(treble({{on(slidingOut(beat("A4"), SlideOut::Shift), 3, 2), on(beat("C5"), 2, 1),
                            on(beat("D5"), 2, 3), on(beat("B4"), 3, 4)}}));
        CHECK(slides(list).empty());
    }
    SECTION("nothing to slide to: none") {
        const DisplayList list = layout(
            treble({{beat("C5"), beat("D5"), beat(""), slidingOut(beat("E5"), SlideOut::Shift)}}));
        CHECK(slides(list).empty());
    }
}

TEST_CASE("Slides: across a barline, and cut where a line ends", "[notation][layout][slide]") {
    const Song song =
        treble({{beat("C5"), beat("C5"), beat("C5"), slidingOut(beat("C5"), SlideOut::Shift)},
                {beat("G5"), beat(""), beat(""), beat("")}});
    const DisplayList whole = layout(song);
    const auto all = slides(whole);
    REQUIRE(all.size() == 1);
    const auto heads = byRole(whole, Role::Notehead);
    REQUIRE(heads.size() == 5);
    CHECK(all[0].from.x > right(heads[3]));
    CHECK(all[0].to.x < left(heads[4]));

    const DisplayList first = layout(song, {.measureCount = 1});
    const auto cut = slides(first);
    REQUIRE(cut.size() == 1);
    CHECK(cut[0].from.x > right(byRole(first, Role::Notehead)[3]));
    CHECK(cut[0].to.x <= first.width + kEpsilon);
    CHECK(cut[0].to.y < cut[0].from.y); // on up to the G

    const DisplayList second = layout(song, {.firstMeasure = 1});
    const auto rest = slides(second);
    REQUIRE(rest.size() == 1);
    const auto secondHeads = byRole(second, Role::Notehead);
    REQUIRE(secondHeads.size() == 1);
    CHECK(rest[0].to.x < left(secondHeads[0]));
    CHECK(rest[0].to.x > left(secondHeads[0]) - 0.5);
    CHECK(rest[0].from.x < rest[0].to.x - 0.5);
    CHECK(rest[0].from.y > rest[0].to.y); // coming up from the C
    CHECK_THAT(rest[0].to.y, WithinAbs(secondHeads[0].origin.y, 0.3));
}

TEST_CASE("Slides: into a note from below or above, out of it up or down",
          "[notation][layout][slide]") {
    const DisplayList list = layout(treble(
        {{slidingIn(beat("C5"), SlideIn::FromBelow), slidingIn(beat("G#4"), SlideIn::FromAbove),
          slidingOut(beat("C5"), SlideOut::Downwards),
          slidingOut(beat("C5"), SlideOut::Upwards)}}));
    const auto found = slides(list);
    REQUIRE(found.size() == 4);
    const auto heads = byRole(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    const auto sharps = byRole(list, Role::Accidental);
    REQUIRE(sharps.size() == 1);
    // In from below, up to the notehead.
    CHECK(found[0].to.x < left(heads[0]));
    CHECK(found[0].to.x > left(heads[0]) - 0.5);
    CHECK(found[0].from.y > found[0].to.y);
    CHECK_THAT(found[0].to.y, WithinAbs(heads[0].origin.y, 0.3));
    // In from above, to before the sharp.
    CHECK(found[1].to.x < left(sharps[0]));
    CHECK(found[1].from.y < found[1].to.y);
    // Out downwards and upwards, from the notehead.
    CHECK(found[2].from.x > right(heads[2]));
    CHECK(found[2].from.x < right(heads[2]) + 0.5);
    CHECK(found[2].to.y > found[2].from.y);
    CHECK(found[3].from.x > right(heads[3]));
    CHECK(found[3].to.y < found[3].from.y);
}

TEST_CASE("Slides: a grace note sliding into its note", "[notation][layout][slide]") {
    Beat main = beat("E5");
    main.graces.push_back(Grace{.duration = Duration(NoteValue::Eighth),
                                .notes = {Note{.pitch = *Pitch::parse("C5")}}});
    main.graces[0].notes[0].effects.slideOut = SlideOut::Shift;
    const DisplayList list = layout(treble({{beat("B4"), main, beat(""), beat("")}}));
    const auto found = slides(list);
    REQUIRE(found.size() == 1);
    const auto heads = byRole(list, Role::Notehead);
    REQUIRE(heads.size() == 3);
    REQUIRE(heads[1].scale < 1.0);
    CHECK(found[0].from.x > right(heads[1]));
    CHECK(found[0].to.x < left(heads[2]));
    CHECK(found[0].to.y < found[0].from.y);
}
