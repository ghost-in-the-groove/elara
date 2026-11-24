// SPDX-License-Identifier: AGPL-3.0-or-later
// Signs and jumps above the first staff (ADR-0049).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::JumpThen;
using elara::core::JumpTo;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::MeasureBox;
using elara::notation::Role;
using elara::notation::TextAlign;
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

const MeasureBox& measureBox(const DisplayList& list, std::size_t m) {
    const auto found = std::ranges::find(list.measures, m, &MeasureBox::index);
    REQUIRE(found != list.measures.end());
    return *found;
}

// The ink of everything but the navigation marks: their highest point.
double inkTop(const DisplayList& list) {
    double highest = 0.0;
    for (const GlyphItem& item : list.glyphs) {
        if (item.role != Role::Navigation && item.role != Role::Tempo) {
            highest = std::min(highest, top(item));
        }
    }
    return highest;
}

} // namespace

TEST_CASE("Navigation marks: signs open their measure above the staff",
          "[notation][layout][navigation]") {
    Song song = quarters(3);
    song.measureHeader(1).segno = true;
    song.measureHeader(2).coda = true;
    const DisplayList list = layout(song);
    const auto signs = glyphs(list, Role::Navigation);
    REQUIRE(signs.size() == 2);
    CHECK(signs[0].glyph == Glyph::Segno);
    CHECK(signs[0].measure == 1);
    CHECK(signs[1].glyph == Glyph::Coda);
    CHECK(signs[1].measure == 2);
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 12);
    for (std::size_t i = 0; i < signs.size(); ++i) {
        const MeasureBox& box = measureBox(list, signs[i].measure);
        // At the start of the measure, before its first note.
        CHECK(left(signs[i]) >= box.x - kEpsilon);
        CHECK(left(signs[i]) < left(heads[4 * (i + 1)]));
        CHECK(signs[i].scale < 1.0);
        // Above the ink, inside the list's extents.
        CHECK(bottom(signs[i]) < inkTop(list));
        CHECK(list.top <= top(signs[i]) + kEpsilon);
    }
    // One line for the system: the signs stand on it.
    CHECK(bottom(signs[0]) == Catch::Approx(bottom(signs[1])));
    CHECK(texts(list, Role::Navigation).empty());
}

TEST_CASE("Navigation marks: the second signs are doubled", "[notation][layout][navigation]") {
    Song song = quarters(2);
    song.measureHeader(0).segnoSegno = true;
    song.measureHeader(1).doubleCoda = true;
    const auto signs = glyphs(layout(song), Role::Navigation);
    REQUIRE(signs.size() == 4);
    CHECK(signs[0].glyph == Glyph::Segno);
    CHECK(signs[1].glyph == Glyph::Segno);
    CHECK(signs[2].glyph == Glyph::Coda);
    CHECK(signs[3].glyph == Glyph::Coda);
    CHECK(left(signs[1]) > right(signs[0]));
    CHECK(left(signs[3]) > right(signs[2]));
}

TEST_CASE("Navigation marks: jump texts end at their measure's barline",
          "[notation][layout][navigation]") {
    Song song = quarters(3);
    song.measureHeader(0).segno = true;
    song.measureHeader(1).toCoda = true;
    song.measureHeader(2).jump = JumpTo::Segno;
    song.measureHeader(2).jumpThen = JumpThen::Coda;
    const DisplayList list = layout(song);
    const auto marks = texts(list, Role::Navigation);
    REQUIRE(contents(marks) == std::vector<std::string>{"To Coda", "D.S. al Coda"});
    const auto signs = glyphs(list, Role::Navigation);
    REQUIRE(signs.size() == 1);
    for (std::size_t i = 0; i < marks.size(); ++i) {
        const TextItem& mark = marks[i];
        CHECK(mark.measure == i + 1);
        CHECK(mark.italic);
        CHECK(mark.align == TextAlign::Right);
        const MeasureBox& box = measureBox(list, mark.measure);
        CHECK(mark.origin.x > box.x + box.width - 1.0);
        CHECK(mark.origin.x < box.x + box.width + 1.5); // the final barline is wider
        // On the line the signs stand on.
        CHECK(mark.origin.y == Catch::Approx(bottom(signs[0])));
    }
}

TEST_CASE("Navigation marks: every jump says where it goes and how it ends",
          "[notation][layout][navigation]") {
    const auto textOf = [](JumpTo to, JumpThen then) {
        Song song = quarters(1);
        song.measureHeader(0).jump = to;
        song.measureHeader(0).jumpThen = then;
        const auto marks = texts(layout(song), Role::Navigation);
        REQUIRE(marks.size() == 1);
        return marks[0].text;
    };
    CHECK(textOf(JumpTo::Start, JumpThen::End) == "D.C.");
    CHECK(textOf(JumpTo::Start, JumpThen::Fine) == "D.C. al Fine");
    CHECK(textOf(JumpTo::Segno, JumpThen::End) == "D.S.");
    CHECK(textOf(JumpTo::Segno, JumpThen::DoubleCoda) == "D.S. al Double Coda");
    CHECK(textOf(JumpTo::SegnoSegno, JumpThen::Coda) == "D.S.S. al Coda");
    CHECK(textOf(JumpTo::SegnoSegno, JumpThen::Fine) == "D.S.S. al Fine");
}

TEST_CASE("Navigation marks: the marks closing one measure do not overlap",
          "[notation][layout][navigation]") {
    Song song = quarters(1);
    song.measureHeader(0).fine = true;
    song.measureHeader(0).toCoda = true;
    song.measureHeader(0).toDoubleCoda = true;
    song.measureHeader(0).jump = JumpTo::Start;
    song.measureHeader(0).jumpThen = JumpThen::Fine;
    const auto marks = texts(layout(song), Role::Navigation);
    REQUIRE(contents(marks) ==
            std::vector<std::string>{"Fine", "To Coda", "To Double Coda", "D.C. al Fine"});
    for (std::size_t i = 1; i < marks.size(); ++i) {
        const double leftOfNext = marks[i].origin.x - textWidth(marks[i].text, marks[i].size);
        CHECK(marks[i - 1].origin.x < leftOfNext);
    }
}

TEST_CASE("Navigation marks: texts that would overlap go on another line",
          "[notation][layout][navigation]") {
    // Measures of one whole note are narrower than "D.S.S. al Double Coda".
    Song song;
    song.appendMeasures(5);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < 5; ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = {Beat{
            .duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = *Pitch::parse("B4")}}}};
    }
    song.measureHeader(1).segnoSegno = true;
    song.measureHeader(2).toCoda = true;
    song.measureHeader(2).toDoubleCoda = true;
    song.measureHeader(3).jump = JumpTo::SegnoSegno;
    song.measureHeader(3).jumpThen = JumpThen::DoubleCoda;
    song.measureHeader(4).doubleCoda = true;
    const DisplayList list = layout(song);
    struct Box {
        double left, right, top, bottom;
    };
    std::vector<Box> boxes;
    const auto signs = glyphs(list, Role::Navigation);
    REQUIRE(signs.size() == 4);
    for (const GlyphItem& sign : signs) {
        boxes.push_back({left(sign), right(sign), top(sign), bottom(sign)});
        // The signs keep the lowest line: texts move up for them.
        CHECK(bottom(sign) == Catch::Approx(bottom(signs[0])));
    }
    const auto marks = texts(list, Role::Navigation);
    REQUIRE(marks.size() == 3);
    for (const TextItem& mark : marks) {
        constexpr double kAscent = 0.75;
        constexpr double kDescent = 0.21;
        boxes.push_back({mark.origin.x - textWidth(mark.text, mark.size), mark.origin.x,
                         mark.origin.y - kAscent * mark.size,
                         mark.origin.y + kDescent * mark.size});
        CHECK(list.top <= mark.origin.y - kAscent * mark.size + kEpsilon);
        CHECK(mark.origin.y <= bottom(signs[0]) + kEpsilon);
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            const bool apart = boxes[i].right <= boxes[j].left || boxes[j].right <= boxes[i].left ||
                               boxes[i].bottom <= boxes[j].top || boxes[j].bottom <= boxes[i].top;
            INFO("marks " << i << " and " << j);
            CHECK(apart);
        }
    }
    // Still above the ink.
    for (const Box& box : boxes) {
        CHECK(box.bottom < inkTop(list));
    }
}

TEST_CASE("Navigation marks: above alternate endings, under tempo marks",
          "[notation][layout][navigation]") {
    Song song = quarters(2);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(0).alternateEndings = 0b01;
    song.measureHeader(0).repeatTimes = 2;
    song.measureHeader(1).alternateEndings = 0b10;
    song.measureHeader(0).segno = true;
    const DisplayList list = layout(song);
    const auto signs = glyphs(list, Role::Navigation);
    REQUIRE(signs.size() == 1);
    double bracket = 0.0;
    for (const auto& line : list.lines) {
        if (line.role == Role::Volta) {
            bracket = std::min({bracket, line.from.y, line.to.y});
        }
    }
    CHECK(bracket < 0.0);
    CHECK(bottom(signs[0]) < bracket);
    const auto tempo = glyphs(list, Role::Tempo);
    REQUIRE_FALSE(tempo.empty());
    CHECK(bottom(tempo[0]) < top(signs[0]));
    CHECK(list.top <= top(tempo[0]) + kEpsilon);
}
