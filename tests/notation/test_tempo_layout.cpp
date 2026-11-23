// SPDX-License-Identifier: AGPL-3.0-or-later
// Tempo marks above the first staff (ADR-0042).
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Fraction;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::Track;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::Role;
using elara::notation::TextItem;

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

// `count` measures on one treble staff, each with four quarter notes B4.
Song quarters(std::size_t count) {
    Song song;
    song.appendMeasures(count);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    const Beat beat{.duration = Duration(NoteValue::Quarter),
                    .notes = {Note{.pitch = *Pitch::parse("B4")}}};
    for (std::size_t m = 0; m < count; ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = {beat, beat, beat, beat};
    }
    return song;
}

DisplayList layout(const Song& song) {
    return layoutStaff(song, 0, 0, bravura());
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

std::vector<TextItem> texts(const DisplayList& list, Role role) {
    std::vector<TextItem> found;
    std::ranges::copy_if(list.texts, std::back_inserter(found),
                         [role](const TextItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const TextItem& item) { return item.origin.x; });
    return found;
}

std::vector<std::string> contents(const std::vector<TextItem>& items) {
    std::vector<std::string> out;
    for (const TextItem& item : items) {
        out.push_back(item.text);
    }
    return out;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x * item.scale;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x * item.scale;
}

double top(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y * item.scale;
}

double bottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y * item.scale;
}

} // namespace

TEST_CASE("Tempo marks: the starting tempo above the first note", "[notation][layout][tempo]") {
    const DisplayList list = layout(quarters(2));
    const auto notes = glyphs(list, Role::Tempo);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].glyph == Glyph::MetNoteQuarterUp);
    CHECK(notes[0].scale < 1.0);
    CHECK(notes[0].measure == 0);
    const auto marks = texts(list, Role::Tempo);
    REQUIRE(contents(marks) == std::vector<std::string>{"= 120"});
    CHECK(marks[0].align == elara::notation::TextAlign::Left);
    CHECK(marks[0].origin.x > right(notes[0]));
    // Over the first note, above the staff, and inside the list's extents.
    const auto heads = glyphs(list, Role::Notehead);
    CHECK_THAT(left(notes[0]), WithinAbs(left(heads[0]), kEpsilon));
    CHECK(bottom(notes[0]) < -0.5);
    CHECK(marks[0].origin.y < -0.5);
    CHECK(list.top <= top(notes[0]) + kEpsilon);
    // The notehead of the mark sits on the text's line.
    CHECK_THAT(bottom(notes[0]), WithinAbs(marks[0].origin.y, 0.15));
}

TEST_CASE("Tempo marks: a change shows where it happens", "[notation][layout][tempo]") {
    SECTION("in the middle of a measure, over its column") {
        Song song = quarters(2);
        song.measureHeader(1).tempoChanges = {
            TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(90)}};
        const DisplayList list = layout(song);
        const auto marks = texts(list, Role::Tempo);
        REQUIRE(contents(marks) == std::vector<std::string>{"= 120", "= 90"});
        const auto notes = glyphs(list, Role::Tempo);
        REQUIRE(notes.size() == 2);
        CHECK(notes[1].measure == 1);
        const auto heads = glyphs(list, Role::Notehead);
        REQUIRE(heads.size() == 8);
        CHECK_THAT(left(notes[1]), WithinAbs(left(heads[6]), kEpsilon));
        CHECK(marks[0].origin.y == marks[1].origin.y); // one line for the system
    }
    SECTION("the same tempo again is not a change") {
        Song song = quarters(2);
        song.measureHeader(1).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(120)}};
        CHECK(contents(texts(layout(song), Role::Tempo)) == std::vector<std::string>{"= 120"});
    }
    SECTION("a change at the very start replaces the starting tempo") {
        Song song = quarters(1);
        song.measureHeader(0).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(100)}};
        CHECK(contents(texts(layout(song), Role::Tempo)) == std::vector<std::string>{"= 100"});
    }
}

TEST_CASE("Tempo marks: beat units and fractions of a beat", "[notation][layout][tempo]") {
    Song song = quarters(1);
    song.setStartTempo(Tempo(60, Duration(NoteValue::Quarter, 1)));
    const DisplayList dotted = layout(song);
    const auto notes = glyphs(dotted, Role::Tempo);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].glyph == Glyph::MetNoteQuarterUp);
    CHECK(notes[1].glyph == Glyph::MetAugmentationDot);
    CHECK(left(notes[1]) > right(notes[0]));
    const auto marks = texts(dotted, Role::Tempo);
    REQUIRE(contents(marks) == std::vector<std::string>{"= 60"});
    CHECK(marks[0].origin.x > right(notes[1]));

    song.setStartTempo(Tempo(92.5, Duration(NoteValue::Half)));
    const DisplayList half = layout(song);
    CHECK(glyphs(half, Role::Tempo).front().glyph == Glyph::MetNoteHalfUp);
    CHECK(contents(texts(half, Role::Tempo)) == std::vector<std::string>{"= 92.5"});
}
