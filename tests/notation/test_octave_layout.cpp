// SPDX-License-Identifier: AGPL-3.0-or-later
// Octave lines over and under the staff (ADR-0089).
#include <elara/core/effects.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
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
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Ottava;
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

// A quarter note `text` under `ottava`.
Beat note(std::string_view text, Ottava ottava = Ottava::None,
          NoteValue value = NoteValue::Quarter) {
    Beat beat{.duration = Duration(value), .notes = {Note{.pitch = *Pitch::parse(text)}}};
    beat.effects.ottava = ottava;
    return beat;
}

// One staff in `clef`; measures[m] holds the beats of the first voice of measure m.
Song songOf(const std::vector<std::vector<Beat>>& measures, Clef clef = Clef::Treble) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(clef)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

DisplayList layout(const Song& song, const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, 0, bravura(), options);
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

// The dashed lines of octave lines, left to right.
std::vector<LineItem> dashes(const DisplayList& list) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found), [](const LineItem& line) {
        return line.role == Role::OctaveLine && line.dash > 0.0;
    });
    std::ranges::stable_sort(found, {}, [](const LineItem& line) { return line.from.x; });
    return found;
}

// The solid hooks closing octave lines, left to right.
std::vector<LineItem> hooks(const DisplayList& list) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found), [](const LineItem& line) {
        return line.role == Role::OctaveLine && line.dash == 0.0;
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

std::vector<Glyph> kinds(const std::vector<GlyphItem>& items) {
    std::vector<Glyph> out;
    for (const GlyphItem& item : items) {
        out.push_back(item.glyph);
    }
    return out;
}

} // namespace

TEST_CASE("Octave lines: notes under an 8va are written an octave lower, under the line",
          "[notation][layout][octave]") {
    // C6 | 8va: C6 D6 E6, then a rest: the line from the second note to the last.
    const DisplayList list = layout(songOf({{note("C6"), note("C6", Ottava::Alta),
                                             note("D6", Ottava::Alta), note("E6", Ottava::Alta)}}));
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    // Seven staff positions (three and a half spaces) lower: C5, in the third space.
    CHECK_THAT(heads[1].origin.y - heads[0].origin.y, WithinAbs(3.5, kEpsilon));
    CHECK_THAT(heads[1].origin.y, WithinAbs(1.5, kEpsilon));
    // The ledger lines of C6 are gone under the line: only the first note has them.
    for (const LineItem& line : list.lines) {
        if (line.role == Role::LedgerLine) {
            CHECK(line.to.x < heads[1].origin.x);
        }
    }

    const auto marks = glyphs(list, Role::OctaveLine);
    REQUIRE(kinds(marks) == std::vector{Glyph::OttavaAlta});
    CHECK_THAT(left(marks[0]), WithinAbs(heads[1].origin.x, kEpsilon));
    CHECK(bottom(marks[0]) < 0.0); // over the staff...
    for (std::size_t h = 1; h < heads.size(); ++h) {
        CHECK(bottom(marks[0]) < heads[h].origin.y - 0.5); // ...and its notes
    }
    const auto line = dashes(list);
    REQUIRE(line.size() == 1);
    CHECK(line[0].from.x > right(marks[0]));
    CHECK_THAT(line[0].from.y, WithinAbs(line[0].to.y, kEpsilon));
    CHECK(line[0].from.y > top(marks[0]));
    CHECK(line[0].from.y < bottom(marks[0]));
    CHECK(line[0].thickness == bravura().defaults().octaveLineThickness);
    // To the end of the last note, closed by a hook down towards the staff.
    const double lastRight =
        heads[3].origin.x + bravura().metrics(Glyph::NoteheadBlack).box.northEast.x;
    CHECK_THAT(line[0].to.x, WithinAbs(lastRight, kEpsilon));
    const auto hook = hooks(list);
    REQUIRE(hook.size() == 1);
    CHECK_THAT(hook[0].from.x, WithinAbs(lastRight, kEpsilon));
    CHECK_THAT(hook[0].from.y, WithinAbs(line[0].to.y, kEpsilon));
    CHECK(hook[0].to.y > hook[0].from.y);
    CHECK(hook[0].to.y < 0.0);
}

TEST_CASE("Octave lines: 8vb and 15mb lie under the staff, 15ma moves two octaves",
          "[notation][layout][octave]") {
    // In the bass clef: G1, then G1 under 8vb (written G2) | A1 under 15mb (A3).
    const auto bassa = [](Ottava ottava) {
        return note("G1", ottava);
    };
    const auto quindicesima = [] {
        return note("A1", Ottava::QuindicesimaBassa);
    };
    const DisplayList bass = layout(songOf(
        {{bassa(Ottava::None), bassa(Ottava::Bassa), bassa(Ottava::Bassa), bassa(Ottava::None)},
         {quindicesima(), quindicesima(), quindicesima(), quindicesima()}},
        Clef::Bass));
    const auto heads = glyphs(bass, Role::Notehead);
    REQUIRE(heads.size() == 8);
    CHECK_THAT(heads[0].origin.y, WithinAbs(7.5, kEpsilon));
    CHECK_THAT(heads[1].origin.y, WithinAbs(4.0, kEpsilon)); // the bottom line
    CHECK_THAT(heads[4].origin.y, WithinAbs(0.0, kEpsilon)); // the top line
    const auto marks = glyphs(bass, Role::OctaveLine);
    REQUIRE(kinds(marks) == std::vector{Glyph::OttavaBassaVb, Glyph::QuindicesimaBassaMb});
    for (const GlyphItem& mark : marks) {
        CHECK(top(mark) > 4.0); // under the staff
    }
    CHECK(top(marks[0]) > heads[1].origin.y + 0.5);
    // Each line closes with a hook up towards the staff.
    const auto hook = hooks(bass);
    REQUIRE(hook.size() == 2);
    for (const LineItem& line : hook) {
        CHECK(line.to.y < line.from.y);
        CHECK(line.to.y > 4.0);
    }

    // 15ma: C7 written C5.
    const DisplayList high = layout(
        songOf({{note("C5"), note("C7", Ottava::QuindicesimaAlta, NoteValue::Half), note("C5")}}));
    const auto high_heads = glyphs(high, Role::Notehead);
    REQUIRE(high_heads.size() == 3);
    CHECK_THAT(high_heads[1].origin.y, WithinAbs(high_heads[0].origin.y, kEpsilon));
    CHECK(kinds(glyphs(high, Role::OctaveLine)) == std::vector{Glyph::QuindicesimaAlta});
}

TEST_CASE("Octave lines: a line runs over a barline, and is cut by the end of a line",
          "[notation][layout][octave]") {
    const Song song =
        songOf({{note("C6"), note("C6"), note("D6", Ottava::Alta), note("E6", Ottava::Alta)},
                {note("F6", Ottava::Alta), note("G6", Ottava::Alta), note("C5"), note("C5")}});
    // Laid out whole: one mark, one line over the barline.
    const DisplayList whole = layout(song);
    CHECK(glyphs(whole, Role::OctaveLine).size() == 1);
    REQUIRE(dashes(whole).size() == 1);
    CHECK(hooks(whole).size() == 1);
    const auto heads = glyphs(whole, Role::Notehead);
    REQUIRE(heads.size() == 8);
    CHECK(dashes(whole)[0].to.x > heads[5].origin.x);
    CHECK(dashes(whole)[0].to.x < heads[6].origin.x);

    // One measure a line: the first line's runs to its end without a hook,
    // the second starts with the mark again.
    const DisplayList first = layout(song, {.firstMeasure = 0, .measureCount = 1});
    CHECK(glyphs(first, Role::OctaveLine).size() == 1);
    REQUIRE(dashes(first).size() == 1);
    CHECK(hooks(first).empty());
    double barline = 0.0;
    for (const LineItem& line : first.lines) {
        if (line.role == Role::Barline) {
            barline = std::max(barline, line.from.x);
        }
    }
    CHECK(dashes(first)[0].to.x >= barline - 0.5);
    const DisplayList second = layout(song, {.firstMeasure = 1});
    const auto again = glyphs(second, Role::OctaveLine);
    REQUIRE(kinds(again) == std::vector{Glyph::OttavaAlta});
    CHECK(again[0].measure == 1);
    CHECK_THAT(left(again[0]), WithinAbs(glyphs(second, Role::Notehead)[0].origin.x, kEpsilon));
    CHECK(hooks(second).size() == 1);
}

TEST_CASE("Octave lines: a short line still shows its hook; none on tablature",
          "[notation][layout][octave]") {
    // One eighth note under an 8va: the line reaches past the mark.
    const DisplayList list = layout(songOf(
        {{note("C6", Ottava::Alta, NoteValue::Eighth), note("C5", Ottava::None, NoteValue::Eighth),
          note("C5", Ottava::None, NoteValue::Half), note("C5")}}));
    const auto marks = glyphs(list, Role::OctaveLine);
    REQUIRE(marks.size() == 1);
    REQUIRE(dashes(list).size() == 1);
    CHECK(dashes(list)[0].to.x - dashes(list)[0].from.x >= 1.0 - kEpsilon);
    CHECK(hooks(list).size() == 1);

    // No octave lines without them, nor on tablature shown alone.
    CHECK(glyphs(
              layout(songOf({{note("C6"), note("C5", Ottava::None, NoteValue::Half), note("C5")}})),
              Role::OctaveLine)
              .empty());
    Song guitar;
    guitar.appendMeasures(1);
    guitar.addTrack(Track("Guitar",
                          Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                          {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    Beat open = note("E4", Ottava::Alta, NoteValue::Whole);
    open.notes[0].position = FretPosition{.string = 1, .fret = 0};
    guitar.track(0).staff(0).measure(0).voices[0].beats = {open};
    const StaffSelection tablature{.view = StaffView::Tablature};
    const DisplayList tab = layoutSystem(guitar, std::span(&tablature, 1), bravura());
    CHECK(glyphs(tab, Role::OctaveLine).empty());
    CHECK(dashes(tab).empty());
}
