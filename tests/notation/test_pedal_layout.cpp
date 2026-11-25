// SPDX-License-Identifier: AGPL-3.0-or-later
// The sustain pedal under the staff (ADR-0092).
#include <elara/core/effects.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pedal;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::LineItem;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;
using elara::notation::StaffSelection;
using elara::notation::StaffView;

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

// A quarter note `text` under `pedal`; a rest without a text.
Beat note(std::string_view text, Pedal pedal = Pedal::None, NoteValue value = NoteValue::Quarter) {
    Beat beat{.duration = Duration(value)};
    if (!text.empty()) {
        beat.notes.push_back(Note{.pitch = *Pitch::parse(text)});
    }
    beat.effects.pedal = pedal;
    return beat;
}

// A piano's two staves; right[m] and left[m] hold the beats of the first voice
// of measure m of each (an empty left hand rests).
Song pianoOf(const std::vector<std::vector<Beat>>& right,
             const std::vector<std::vector<Beat>>& left = {}) {
    Song song;
    song.appendMeasures(right.size());
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano"}, {Staff(Clef::Treble), Staff(Clef::Bass)}));
    for (std::size_t m = 0; m < right.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = right[m];
        if (m < left.size()) {
            song.track(0).staff(1).measure(m).voices[0].beats = left[m];
        }
    }
    return song;
}

DisplayList layout(const Song& song, std::size_t staff = 0,
                   const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, staff, bravura(), options);
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

// The lines of the pedal: level ones (`level`) or the others, left to right.
std::vector<LineItem> pedalLines(const DisplayList& list, bool level) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found), [level](const LineItem& line) {
        return line.role == Role::Pedal && (std::abs(line.from.y - line.to.y) < kEpsilon) == level;
    });
    std::ranges::stable_sort(found, {}, [](const LineItem& line) { return line.from.x; });
    return found;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x;
}

double top(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y;
}

double bottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y;
}

double barlineX(const DisplayList& list) {
    double barline = 0.0;
    for (const LineItem& line : list.lines) {
        if (line.role == Role::Barline) {
            barline = std::max(barline, line.from.x);
        }
    }
    return barline;
}

} // namespace

TEST_CASE("Pedal: Ped. under the staff, a line to where it comes up and a hook",
          "[notation][layout][pedal]") {
    // C4 D4 under the pedal, then E4 F4: "Ped." at C4, below the staff and the
    // notes, a line on to just before E4, where the pedal comes up.
    const DisplayList list = layout(
        pianoOf({{note("C4", Pedal::Down), note("D4", Pedal::Down), note("E4"), note("F4")}}));
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    const auto marks = glyphs(list, Role::Pedal);
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].glyph == Glyph::KeyboardPedalPed);
    CHECK_THAT(left(marks[0]), WithinAbs(heads[0].origin.x, kEpsilon));
    CHECK(top(marks[0]) > 4.0);                     // under the staff...
    CHECK(top(marks[0]) > heads[0].origin.y + 0.5); // ...and C4's ledger line

    const auto level = pedalLines(list, true);
    REQUIRE(level.size() == 1);
    CHECK(level[0].from.x > right(marks[0]));
    CHECK_THAT(level[0].from.y, WithinAbs(bottom(marks[0]), kEpsilon));
    CHECK(level[0].to.x > heads[1].origin.x);
    CHECK(level[0].to.x < heads[2].origin.x);
    CHECK(level[0].thickness == bravura().defaults().pedalLineThickness);
    // The hook goes up from its end towards the staff.
    const auto hook = pedalLines(list, false);
    REQUIRE(hook.size() == 1);
    CHECK_THAT(hook[0].from.x, WithinAbs(level[0].to.x, kEpsilon));
    CHECK_THAT(hook[0].to.x, WithinAbs(level[0].to.x, kEpsilon));
    CHECK_THAT(hook[0].from.y, WithinAbs(level[0].to.y, kEpsilon));
    CHECK(hook[0].to.y < hook[0].from.y);
    CHECK(hook[0].to.y > 4.0);

    // No pedal, nothing drawn.
    CHECK(glyphs(
              layout(pianoOf({{note("C4"), note("D4", Pedal::None, NoteValue::Half), note("E4")}})),
              Role::Pedal)
              .empty());
}

TEST_CASE("Pedal: a change notches the line where its beat starts", "[notation][layout][pedal]") {
    // Down on C5 D5, changed on E5, on to the end of the measure with F5.
    const DisplayList list =
        layout(pianoOf({{note("C5", Pedal::Down), note("D5", Pedal::Down),
                         note("E5", Pedal::Change), note("F5", Pedal::Down)}}));
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    REQUIRE(glyphs(list, Role::Pedal).size() == 1); // one pedalling
    const auto level = pedalLines(list, true);
    REQUIRE(level.size() == 2); // the line, broken by the notch
    const auto slanted = pedalLines(list, false);
    REQUIRE(slanted.size() == 3); // up and down at E5, then the hook
    // The notch peaks over E5's column, above the line.
    CHECK_THAT(slanted[0].to.x, WithinAbs(heads[2].origin.x, kEpsilon));
    CHECK_THAT(slanted[1].from.x, WithinAbs(heads[2].origin.x, kEpsilon));
    CHECK(slanted[0].to.y < level[0].from.y);
    CHECK_THAT(slanted[0].from.x, WithinAbs(level[0].to.x, kEpsilon));
    CHECK_THAT(slanted[1].to.x, WithinAbs(level[1].from.x, kEpsilon));
    // To the barline, where the hook closes it.
    CHECK(level[1].to.x > heads[3].origin.x);
    CHECK(level[1].to.x <= barlineX(list) + kEpsilon);
    CHECK_THAT(slanted[2].from.x, WithinAbs(level[1].to.x, kEpsilon));
}

TEST_CASE("Pedal: under the dynamics, on its own staff only", "[notation][layout][pedal]") {
    // ff on the left hand's C3, under the pedal: "Ped." goes under the "ff".
    Beat loud = note("C3", Pedal::Down, NoteValue::Whole);
    loud.notes[0].effects.dynamic = Dynamic::FF;
    const Song song = pianoOf({{note("C5", Pedal::None, NoteValue::Whole)}}, {{loud}});
    const DisplayList bass = layout(song, 1);
    const auto dynamics = glyphs(bass, Role::Dynamic);
    REQUIRE(!dynamics.empty());
    const auto marks = glyphs(bass, Role::Pedal);
    REQUIRE(marks.size() == 1);
    for (const GlyphItem& dynamic : dynamics) {
        CHECK(top(marks[0]) > bottom(dynamic));
    }
    // The right hand shows none.
    CHECK(glyphs(layout(song, 0), Role::Pedal).empty());
}

TEST_CASE("Pedal: a pedalling runs over a barline, and is cut by the end of a line",
          "[notation][layout][pedal]") {
    // A rest under the pedal goes on with it.
    const Song song =
        pianoOf({{note("C5"), note("C5"), note("D5", Pedal::Down), note("", Pedal::Down)},
                 {note("F5", Pedal::Down), note("G5", Pedal::Down), note("C5"), note("C5")}});
    const DisplayList whole = layout(song);
    CHECK(glyphs(whole, Role::Pedal).size() == 1);
    REQUIRE(pedalLines(whole, true).size() == 1);
    CHECK(pedalLines(whole, false).size() == 1);
    const auto heads = glyphs(whole, Role::Notehead);
    REQUIRE(heads.size() == 7);
    CHECK(pedalLines(whole, true)[0].to.x > heads[4].origin.x);
    CHECK(pedalLines(whole, true)[0].to.x < heads[5].origin.x);

    // One measure a line: the first runs to its end without a hook, the second
    // starts with "Ped." again.
    const DisplayList first = layout(song, 0, {.firstMeasure = 0, .measureCount = 1});
    CHECK(glyphs(first, Role::Pedal).size() == 1);
    REQUIRE(pedalLines(first, true).size() == 1);
    CHECK(pedalLines(first, false).empty());
    CHECK(pedalLines(first, true)[0].to.x >= barlineX(first) - 0.5);
    const DisplayList second = layout(song, 0, {.firstMeasure = 1});
    const auto again = glyphs(second, Role::Pedal);
    REQUIRE(again.size() == 1);
    CHECK(again[0].measure == 1);
    CHECK_THAT(left(again[0]), WithinAbs(glyphs(second, Role::Notehead)[0].origin.x, kEpsilon));
    CHECK(pedalLines(second, false).size() == 1);
}

TEST_CASE("Pedal: none on tablature", "[notation][layout][pedal]") {
    Song guitar;
    guitar.appendMeasures(1);
    guitar.addTrack(Track("Guitar",
                          Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                          {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    Beat open = note("E4", Pedal::Down, NoteValue::Whole);
    open.notes[0].position = FretPosition{.string = 1, .fret = 0};
    guitar.track(0).staff(0).measure(0).voices[0].beats = {open};
    const StaffSelection tablature{.view = StaffView::Tablature};
    const DisplayList tab = layoutSystem(guitar, std::span(&tablature, 1), bravura());
    CHECK(glyphs(tab, Role::Pedal).empty());
    CHECK(pedalLines(tab, true).empty());
}
