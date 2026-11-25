// SPDX-License-Identifier: AGPL-3.0-or-later
// Tempo marks above the first staff (ADR-0042).
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <span>
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
using elara::notation::layoutPages;
using elara::notation::layoutStaff;
using elara::notation::LineItem;
using elara::notation::PageLayoutOptions;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::TextItem;
using elara::notation::textWidth;

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

std::vector<LineItem> lines(const DisplayList& list, Role role) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found),
                         [role](const LineItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const LineItem& item) { return item.from.x; });
    return found;
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

TEST_CASE("Triplet feel marks: the pair as written equals the pair as played",
          "[notation][layout][tempo][feel]") {
    // ADR-0104: after the tempo mark where the feel starts, alone where it
    // changes, and a straight pair where it ends.
    using elara::core::TripletFeel;
    const auto glyphsOf = [](const DisplayList& list, std::size_t m) {
        std::vector<Glyph> found;
        for (const GlyphItem& item : glyphs(list, Role::Tempo)) {
            if (item.measure == m) {
                found.push_back(item.glyph);
            }
        }
        return found;
    };
    const std::vector eighthPair{Glyph::TextBlackNoteShortStem, Glyph::TextCont8thBeamShortStem,
                                 Glyph::TextBlackNoteFrac8thShortStem};
    const std::vector sixteenthPair{Glyph::TextBlackNoteShortStem, Glyph::TextCont16thBeamShortStem,
                                    Glyph::TextBlackNoteFrac16thShortStem};
    const auto join = [](std::initializer_list<std::vector<Glyph>> parts) {
        std::vector<Glyph> all;
        for (const auto& part : parts) {
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    };

    Song song = quarters(4);
    song.measureHeader(0).tripletFeel = TripletFeel::Eighth;
    song.measureHeader(1).tripletFeel = TripletFeel::Eighth;
    song.measureHeader(2).tripletFeel = TripletFeel::DottedSixteenth;
    const DisplayList list = layout(song);
    CHECK(glyphsOf(list, 0) == join({{Glyph::MetNoteQuarterUp},
                                     eighthPair,
                                     {Glyph::TextTupletBracketStartShortStem,
                                      Glyph::MetNoteQuarterUp, Glyph::TextTuplet3ShortStem,
                                      Glyph::MetNote8thUp, Glyph::TextTupletBracketEndShortStem}}));
    CHECK(glyphsOf(list, 1).empty()); // the same feel goes on
    CHECK(glyphsOf(list, 2) ==
          join({sixteenthPair,
                {Glyph::MetNote16thUp, Glyph::MetAugmentationDot, Glyph::MetNote32ndUp}}));
    CHECK(glyphsOf(list, 3) == join({sixteenthPair, sixteenthPair})); // straight again
    const auto marks = texts(list, Role::Tempo);
    REQUIRE(contents(marks) == std::vector<std::string>{"= 120", "=", "=", "="});
    // On the tempo's line, after its text, the "=" between the pairs.
    const auto all = glyphs(list, Role::Tempo);
    CHECK(left(all[1]) > marks[0].origin.x + textWidth("= 120", marks[0].size));
    CHECK(marks[1].origin.y == marks[0].origin.y);
    CHECK(marks[1].origin.x > right(all[3]));
    CHECK(left(all[4]) > marks[1].origin.x + textWidth("=", marks[1].size));
    CHECK_THAT(bottom(all[1]), WithinAbs(marks[0].origin.y, 0.15));
    // Each mark starts over its measure's first note.
    const auto heads = glyphs(list, Role::Notehead);
    const auto firstOf = [&all](std::size_t m) {
        return *std::ranges::find(all, m, &GlyphItem::measure);
    };
    CHECK_THAT(left(firstOf(2)), WithinAbs(left(heads[8]), kEpsilon));

    SECTION("Scottish pairs and triplet sixteenths") {
        song.measureHeader(0).tripletFeel = TripletFeel::ScottishEighth;
        song.measureHeader(1).tripletFeel = TripletFeel::None;
        song.measureHeader(2).tripletFeel = TripletFeel::Sixteenth;
        const DisplayList other = layout(song);
        CHECK(glyphsOf(other, 0) ==
              join({{Glyph::MetNoteQuarterUp},
                    eighthPair,
                    {Glyph::MetNote16thUp, Glyph::MetNote8thUp, Glyph::MetAugmentationDot}}));
        CHECK(glyphsOf(other, 1) == join({eighthPair, eighthPair}));
        CHECK(glyphsOf(other, 2) ==
              join({sixteenthPair,
                    {Glyph::TextTupletBracketStartShortStem, Glyph::MetNote8thUp,
                     Glyph::TextTuplet3ShortStem, Glyph::MetNote16thUp,
                     Glyph::TextTupletBracketEndShortStem}}));
    }
    SECTION("none without a triplet feel") {
        CHECK(glyphs(layout(quarters(2)), Role::Tempo).size() == 1);
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

TEST_CASE("Tempo marks: a gradual change says which way it goes", "[notation][layout][tempo]") {
    // ADR-0082: "accel." or "rit." after the mark, then a dashed line to the
    // next change.
    SECTION("a ritardando from the tempo in force, as Guitar Pro writes it") {
        Song song = quarters(3);
        song.measureHeader(1).tempoChanges = {
            TempoChange{.offset = {}, .tempo = Tempo(120), .gradual = true}};
        song.measureHeader(2).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(90)}};
        const DisplayList list = layout(song);
        const auto marks = texts(list, Role::Tempo);
        REQUIRE(contents(marks) == std::vector<std::string>{"= 120", "rit.", "= 90"});
        CHECK(marks[1].italic);
        CHECK_FALSE(marks[2].italic);
        CHECK(marks[1].measure == 1);
        CHECK(marks[1].origin.y == marks[0].origin.y);
        const auto heads = glyphs(list, Role::Notehead);
        REQUIRE(heads.size() == 12);
        CHECK_THAT(marks[1].origin.x, WithinAbs(left(heads[4]), kEpsilon));
        const auto dashes = lines(list, Role::Tempo);
        REQUIRE(dashes.size() == 1);
        CHECK(dashes[0].dash > 0.0);
        CHECK(dashes[0].from.y == dashes[0].to.y);
        CHECK(dashes[0].from.y < marks[1].origin.y);
        CHECK(dashes[0].from.x > marks[1].origin.x + textWidth(marks[1].text, marks[1].size));
        const auto notes = glyphs(list, Role::Tempo);
        REQUIRE(notes.size() == 2);
        CHECK(dashes[0].to.x < left(notes[1]));
        CHECK(dashes[0].to.x > left(heads[6]));
    }
    SECTION("an accelerando from a tempo of its own") {
        Song song = quarters(3);
        song.measureHeader(1).tempoChanges = {
            TempoChange{.offset = {}, .tempo = Tempo(100), .gradual = true}};
        song.measureHeader(2).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(140)}};
        const auto marks = texts(layout(song), Role::Tempo);
        REQUIRE(contents(marks) == std::vector<std::string>{"= 120", "= 100", "accel.", "= 140"});
        CHECK(marks[2].origin.x > marks[1].origin.x + textWidth(marks[1].text, marks[1].size));
    }
    SECTION("nothing more with no change to go to, or one at the same speed") {
        Song song = quarters(3);
        song.measureHeader(1).tempoChanges = {
            TempoChange{.offset = {}, .tempo = Tempo(100), .gradual = true}};
        DisplayList list = layout(song);
        CHECK(contents(texts(list, Role::Tempo)) == std::vector<std::string>{"= 120", "= 100"});
        CHECK(lines(list, Role::Tempo).empty());
        // Half = 50 is quarter = 100.
        song.measureHeader(2).tempoChanges = {
            TempoChange{.offset = {}, .tempo = Tempo(50, Duration(NoteValue::Half))}};
        list = layout(song);
        CHECK(contents(texts(list, Role::Tempo)) ==
              std::vector<std::string>{"= 120", "= 100", "= 50"});
        CHECK(lines(list, Role::Tempo).empty());
    }
}

TEST_CASE("Tempo marks: a gradual change goes on into the next systems",
          "[notation][layout][tempo]") {
    // Slowing down from the middle of measure 2 to measure 7, two measures a
    // system: the line runs to the end of its system and across the next two.
    Song song = quarters(8);
    song.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(120), .gradual = true}};
    song.measureHeader(6).tempoChanges = {TempoChange{.offset = {}, .tempo = Tempo(60)}};
    const StaffSelection selection{};
    PageLayoutOptions options;
    options.measuresPerSystem = 2;
    const auto pages = layoutPages(song, std::span(&selection, 1), bravura(), options);
    std::vector<LineItem> dashes;
    std::vector<std::string> words;
    for (const DisplayList& page : pages) {
        REQUIRE(page.systems.size() >= 1);
        for (const LineItem& line : lines(page, Role::Tempo)) {
            dashes.push_back(line);
        }
        for (const TextItem& text : texts(page, Role::Tempo)) {
            if (text.italic) {
                words.push_back(text.text);
            }
        }
    }
    CHECK(words == std::vector<std::string>{"rit."});
    std::ranges::stable_sort(dashes, {}, &LineItem::measure);
    REQUIRE(dashes.size() == 3);
    CHECK(dashes[0].measure == 1);
    CHECK(dashes[1].measure == 2);
    CHECK(dashes[2].measure == 4);
    for (const LineItem& dash : dashes) {
        CHECK(dash.to.x - dash.from.x > 1.0);
    }
}
