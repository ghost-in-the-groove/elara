// SPDX-License-Identifier: AGPL-3.0-or-later
// Slurs over the beats slurred in a row (ADR-0086), and over notes played
// legato (ADR-0097).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Grace;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SlideOut;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::Point;
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

// A beat of `pitch` (a rest when empty), slurred to the next when `slurred`.
Beat beat(std::string_view pitch, bool slurred = false, NoteValue value = NoteValue::Quarter) {
    Beat made{.duration = Duration(value)};
    if (!pitch.empty()) {
        made.notes.push_back(Note{.pitch = *Pitch::parse(pitch)});
    }
    made.effects.slur = slurred;
    return made;
}

// `made` with its notes played legato from the note before them: hammered
// on or pulled off.
Beat legato(Beat made) {
    for (Note& note : made.notes) {
        note.effects.legatoFromPrevious = true;
    }
    return made;
}

// `made` with its notes sliding out to the next.
Beat sliding(Beat made, SlideOut slide) {
    for (Note& note : made.notes) {
        note.effects.slideOut = slide;
    }
    return made;
}

// `made` with an eighth grace note of `pitch` before the beat.
Beat graced(Beat made, std::string_view pitch) {
    made.graces.push_back(Grace{.duration = Duration(NoteValue::Eighth),
                                .notes = {Note{.pitch = *Pitch::parse(pitch)}}});
    return made;
}

// One treble staff: each measure's first voice, then optionally its second.
Song treble(const std::vector<std::vector<Beat>>& first,
            const std::vector<std::vector<Beat>>& second = {}) {
    Song song;
    song.appendMeasures(first.size());
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < first.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = first[m];
        if (m < second.size()) {
            song.track(0).staff(0).measure(m).voices[1].beats = second[m];
        }
    }
    return song;
}

DisplayList layout(const Song& song, const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, 0, bravura(), options);
}

std::vector<CurveItem> slurs(const DisplayList& list) {
    std::vector<CurveItem> found;
    std::ranges::copy_if(list.curves, std::back_inserter(found),
                         [](const CurveItem& curve) { return curve.role == Role::Slur; });
    std::ranges::stable_sort(found, {}, [](const CurveItem& curve) { return curve.from.x; });
    return found;
}

std::vector<GlyphItem> heads(const DisplayList& list) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [](const GlyphItem& item) { return item.role == Role::Notehead; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double centre(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return item.origin.x + (box.southWest.x + box.northEast.x) / 2;
}

double top(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y;
}

double bottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y;
}

Point at(const CurveItem& curve, double t) {
    const double u = 1 - t;
    const auto mix = [&](double a, double b, double c, double d) {
        return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
    };
    return {mix(curve.from.x, curve.control1.x, curve.control2.x, curve.to.x),
            mix(curve.from.y, curve.control1.y, curve.control2.y, curve.to.y)};
}

// The curve's y where it passes x (its middle line, without thickness).
double yAt(const CurveItem& curve, double x) {
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2;
        (at(curve, mid).x < x ? lo : hi) = mid;
    }
    return at(curve, lo).y;
}

// The top (bottom) of the stems within a space of x.
double stemEnd(const DisplayList& list, double x, bool up) {
    double end = up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
    for (const auto& line : list.lines) {
        if (line.role == Role::Stem && std::abs(line.from.x - x) < 1.0) {
            end = up ? std::min({end, line.from.y, line.to.y})
                     : std::max({end, line.from.y, line.to.y});
        }
    }
    return end;
}

} // namespace

TEST_CASE("Slurs: on the notehead side of stems going one way, above when they differ",
          "[notation][layout][slur]") {
    SECTION("stems down: above the noteheads") {
        const DisplayList list =
            layout(treble({{beat("B4", true), beat("C5", true), beat("D5"), beat("")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 3);
        CHECK_THAT(found[0].from.x, WithinAbs(centre(notes[0]), 0.3));
        CHECK_THAT(found[0].to.x, WithinAbs(centre(notes[2]), 0.3));
        CHECK(found[0].from.y < top(notes[0]));
        CHECK(found[0].to.y < top(notes[2]));
        CHECK(yAt(found[0], centre(notes[1])) < top(notes[1]));
    }
    SECTION("stems up: under the noteheads") {
        const DisplayList list =
            layout(treble({{beat("E4", true), beat("F4", true), beat("G4"), beat("")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 3);
        CHECK(found[0].from.y > bottom(notes[0]));
        CHECK(found[0].to.y > bottom(notes[2]));
        CHECK(yAt(found[0], centre(notes[1])) > bottom(notes[1]));
    }
    SECTION("stems both ways: above, from the tip of the stem going up") {
        const DisplayList list =
            layout(treble({{beat("G4", true), beat("D5"), beat(""), beat("")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 2);
        CHECK(found[0].from.y < stemEnd(list, found[0].from.x, true));
        CHECK(found[0].to.y < top(notes[1]));
    }
}

TEST_CASE("Slurs: arch over the notes under them", "[notation][layout][slur]") {
    // C5 and the last C5 have stems down; the slur clears the high A5 between.
    const DisplayList list =
        layout(treble({{beat("C5", true), beat("A5", true), beat("C5"), beat("")}}));
    const auto found = slurs(list);
    REQUIRE(found.size() == 1);
    const auto notes = heads(list);
    REQUIRE(notes.size() == 3);
    const auto& box = bravura().metrics(notes[1].glyph).box;
    for (const double x :
         {notes[1].origin.x + box.southWest.x, notes[1].origin.x + box.northEast.x}) {
        CHECK(yAt(found[0], x) < top(notes[1]) - 0.2);
    }
    // Not higher than it has to be: the ends stay by their noteheads.
    CHECK(found[0].from.y > top(notes[0]) - 1.0);
}

TEST_CASE("Slurs: the upper voice's above, the lower voice's below", "[notation][layout][slur]") {
    const DisplayList list =
        layout(treble({{beat("E5", true, NoteValue::Half), beat("F5", false, NoteValue::Half)}},
                      {{beat("C4", true, NoteValue::Half), beat("D4", false, NoteValue::Half)}}));
    const auto found = slurs(list);
    REQUIRE(found.size() == 2);
    const auto upper =
        std::ranges::min_element(found, {}, [](const CurveItem& c) { return c.from.y; });
    const auto lower =
        std::ranges::max_element(found, {}, [](const CurveItem& c) { return c.from.y; });
    CHECK(upper->from.y < 0.0);
    CHECK(upper->control1.y < upper->from.y); // bulging up
    CHECK(lower->from.y > 4.0);
    CHECK(lower->control1.y > lower->from.y); // bulging down
}

TEST_CASE("Slurs: across a barline, and cut where a line ends", "[notation][layout][slur]") {
    const Song song = treble({{beat("C5"), beat("D5"), beat("E5"), beat("F5", true)},
                              {beat("G5", true), beat("A5"), beat(""), beat("")}});
    const DisplayList whole = layout(song);
    const auto all = slurs(whole);
    REQUIRE(all.size() == 1);
    const auto notes = heads(whole);
    REQUIRE(notes.size() == 6);
    CHECK_THAT(all[0].from.x, WithinAbs(centre(notes[3]), 0.3));
    CHECK_THAT(all[0].to.x, WithinAbs(centre(notes[5]), 0.3));

    const DisplayList first = layout(song, {.measureCount = 1});
    const auto cut = slurs(first);
    REQUIRE(cut.size() == 1);
    CHECK_THAT(cut[0].from.x, WithinAbs(centre(heads(first)[3]), 0.3));
    CHECK(cut[0].to.x > centre(heads(first)[3]) + 1.0);
    CHECK(cut[0].to.x <= first.width + kEpsilon);

    const DisplayList second = layout(song, {.firstMeasure = 1});
    const auto rest = slurs(second);
    REQUIRE(rest.size() == 1);
    const auto secondNotes = heads(second);
    REQUIRE(secondNotes.size() == 2);
    CHECK(rest[0].from.x < secondNotes[0].origin.x);
    CHECK_THAT(rest[0].to.x, WithinAbs(centre(secondNotes[1]), 0.3));
}

TEST_CASE("Slurs: a beat slurred to nothing draws none", "[notation][layout][slur]") {
    // The last beat of the song, and the last of a voice the next measure leaves empty.
    const Song song = treble(
        {{beat("C5"), beat("D5"), beat("E5"), beat("F5")}, {beat("G5", true, NoteValue::Whole)}},
        {{beat("C4", false, NoteValue::Half), beat("D4", true, NoteValue::Half)}});
    CHECK(slurs(layout(song)).empty());
}

TEST_CASE("Slurs: hammer-ons, pull-offs and legato slides slur their notes",
          "[notation][layout][slur]") {
    SECTION("a hammer-on and a pull-off in a row: one slur over the three notes") {
        const DisplayList list =
            layout(treble({{beat("C5"), legato(beat("D5")), legato(beat("C5")), beat("")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 3);
        CHECK_THAT(found[0].from.x, WithinAbs(centre(notes[0]), 0.3));
        CHECK_THAT(found[0].to.x, WithinAbs(centre(notes[2]), 0.3));
    }
    SECTION("a legato slide slurs into the next note, a shift slide does not") {
        const DisplayList list =
            layout(treble({{sliding(beat("C5"), SlideOut::Legato), beat("D5"),
                            sliding(beat("E5"), SlideOut::Shift), beat("F5")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 4);
        CHECK_THAT(found[0].from.x, WithinAbs(centre(notes[0]), 0.3));
        CHECK_THAT(found[0].to.x, WithinAbs(centre(notes[1]), 0.3));
    }
    SECTION("joined to a marked slur: one slur") {
        const DisplayList list =
            layout(treble({{beat("C5", true), beat("D5"), legato(beat("E5")), beat("")}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 3);
        CHECK_THAT(found[0].from.x, WithinAbs(centre(notes[0]), 0.3));
        CHECK_THAT(found[0].to.x, WithinAbs(centre(notes[2]), 0.3));
    }
    SECTION("on a fretted staff, only to the next note on its string") {
        // A slides legato to the B four beats on, on its string; the C on
        // another string is not slurred to it. D is hammered on from C.
        Beat a = sliding(beat("A4"), SlideOut::Legato);
        a.notes[0].position = elara::core::FretPosition{.string = 3, .fret = 2};
        Beat c = beat("C5");
        c.notes[0].position = elara::core::FretPosition{.string = 2, .fret = 1};
        Beat d = legato(beat("D5"));
        d.notes[0].position = elara::core::FretPosition{.string = 2, .fret = 3};
        Beat b = beat("B4");
        b.notes[0].position = elara::core::FretPosition{.string = 3, .fret = 4};
        const DisplayList list = layout(treble({{a, c, d, b}}));
        const auto found = slurs(list);
        REQUIRE(found.size() == 1);
        const auto notes = heads(list);
        REQUIRE(notes.size() == 4);
        CHECK_THAT(found[0].from.x, WithinAbs(centre(notes[1]), 0.3));
        CHECK_THAT(found[0].to.x, WithinAbs(centre(notes[2]), 0.3));
    }
    SECTION("across a barline, and from the line before") {
        const Song song = treble({{beat("C5"), beat("D5"), beat("E5"), beat("F5")},
                                  {legato(beat("G5")), beat("A5"), beat(""), beat("")}});
        const auto all = slurs(layout(song));
        REQUIRE(all.size() == 1);
        const DisplayList second = layout(song, {.firstMeasure = 1});
        const auto rest = slurs(second);
        REQUIRE(rest.size() == 1);
        CHECK(rest[0].from.x < heads(second)[0].origin.x);
    }
}

TEST_CASE("Slurs: a grace note played legato into its note is slurred to it",
          "[notation][layout][slur]") {
    // B4, then D5 hammered on from its grace note C5: the slur joins the grace
    // note to D5, under them since the grace note's stem goes up, and B4 is
    // not slurred.
    const DisplayList list =
        layout(treble({{beat("B4"), legato(graced(beat("D5"), "C5")), beat(""), beat("")}}));
    const auto found = slurs(list);
    REQUIRE(found.size() == 1);
    const auto notes = heads(list);
    REQUIRE(notes.size() == 3);
    const GlyphItem& grace = notes[1];
    const GlyphItem& main = notes[2];
    REQUIRE(grace.scale < 1.0);
    CHECK(found[0].from.x > centre(notes[0]) + 0.5);
    CHECK(found[0].from.x >= grace.origin.x);
    CHECK(found[0].to.x <= centre(main) + 0.3);
    CHECK(found[0].to.x > found[0].from.x);
    CHECK(found[0].from.y > top(grace));
    CHECK(found[0].control1.y > found[0].from.y); // bulging down
}

TEST_CASE("Slurs: a grace note sliding legato into its note is slurred to it",
          "[notation][layout][slur]") {
    Beat main = graced(beat("D5"), "C5");
    main.graces[0].notes[0].effects.slideOut = SlideOut::Legato;
    const DisplayList list = layout(treble({{beat("B4"), main, beat(""), beat("")}}));
    const auto found = slurs(list);
    REQUIRE(found.size() == 1);
    CHECK(found[0].from.x > centre(heads(list)[0]) + 0.5);
}
