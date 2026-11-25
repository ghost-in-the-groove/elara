// SPDX-License-Identifier: AGPL-3.0-or-later
// Beat text above the staff (ADR-0081).
#include <elara/core/measure.hpp>
#include <elara/core/shared_value.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::ChordDiagram;
using elara::core::ChordSymbol;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SharedOptional;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::GlyphItem;
using elara::notation::layoutSystem;
using elara::notation::LineItem;
using elara::notation::Role;
using elara::notation::roleName;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::TextAlign;
using elara::notation::TextItem;
using elara::notation::textWidth;

namespace {

constexpr double kEpsilon = 1e-9;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

Beat sing(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value), .notes = {Note{.pitch = *Pitch::parse("B4")}}};
}

Beat play(int string, int fret, NoteValue value = NoteValue::Quarter) {
    return Beat{
        .duration = Duration(value),
        .notes = {Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                       .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                                .fret = static_cast<std::int8_t>(fret)}}}};
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

Beat texted(Beat beat, std::string text) {
    beat.text = SharedOptional(std::move(text));
    return beat;
}

Song staffOf(Staff staff, const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Part", Instrument{.name = "Part", .midiProgram = 25}, {std::move(staff)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

Song voice(const std::vector<std::vector<Beat>>& measures) {
    return staffOf(Staff(Clef::Treble), measures);
}

Song guitar(const std::vector<std::vector<Beat>>& measures) {
    return staffOf(Staff(Clef::Treble8vb, Tuning::standardGuitar()), measures);
}

DisplayList layout(const Song& song, StaffView view = StaffView::Standard) {
    const StaffSelection selection{.view = view};
    return layoutSystem(song, std::span(&selection, 1), bravura());
}

std::vector<TextItem> texts(const DisplayList& list, Role role = Role::BeatText) {
    std::vector<TextItem> found;
    for (const TextItem& item : list.texts) {
        if (item.role == role) {
            found.push_back(item);
        }
    }
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

std::vector<GlyphItem> noteheads(const DisplayList& list) {
    std::vector<GlyphItem> found;
    for (const GlyphItem& item : list.glyphs) {
        if (item.role == Role::Notehead) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double glyphTop(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y * item.scale;
}

// Text extents as layout assumes them (ascenders 0.75 em, descenders 0.21 em).
double textTop(const TextItem& item) {
    return item.origin.y - 0.75 * item.size;
}

double textBottom(const TextItem& item) {
    return item.origin.y + 0.21 * item.size;
}

double textRight(const TextItem& item) {
    return item.origin.x + textWidth(item.text, item.size);
}

} // namespace

TEST_CASE("Beat text: starts at its beat, in italics, above the staff",
          "[notation][layout][text]") {
    const DisplayList list =
        layout(voice({{texted(sing(), "dolce"), rest(), texted(sing(NoteValue::Half), "rit.")}}));
    const auto found = texts(list);
    REQUIRE(contents(found) == std::vector<std::string>{"dolce", "rit."});
    const auto heads = noteheads(list);
    REQUIRE(heads.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(found[i].align == TextAlign::Left);
        CHECK(found[i].italic);
        CHECK_THAT(found[i].origin.x, WithinAbs(heads[i].origin.x, kEpsilon));
        CHECK(found[i].measure == 0);
        CHECK(textBottom(found[i]) < glyphTop(heads[i]));
        CHECK(textBottom(found[i]) < 0.0);
        CHECK(textTop(found[i]) >= list.top - kEpsilon);
    }
    CHECK_THAT(found[0].origin.y, WithinAbs(found[1].origin.y, kEpsilon));

    // The notes keep their places: text takes no room between beats.
    const DisplayList plain = layout(voice({{sing(), rest(), sing(NoteValue::Half)}}));
    const auto plainHeads = noteheads(plain);
    REQUIRE(plainHeads.size() == 2);
    CHECK_THAT(plainHeads[1].origin.x, WithinAbs(heads[1].origin.x, kEpsilon));
    CHECK(texts(plain).empty());
    CHECK(plain.top > list.top);
    CHECK(roleName(Role::BeatText) == "beatText");
}

TEST_CASE("Beat text: above the chord symbols", "[notation][layout][text]") {
    constexpr std::int8_t x = ChordDiagram::kMuted;
    Beat beat = texted(play(2, 1, NoteValue::Whole), "let it ring");
    beat.chord = SharedOptional(ChordSymbol{
        .name = "C", .diagram = ChordDiagram{.firstFret = 1, .frets = {0, 1, 0, 2, 3, x}}});
    const DisplayList list = layout(guitar({{beat}}));
    const auto found = texts(list);
    REQUIRE(found.size() == 1);
    const auto names = texts(list, Role::Chord);
    REQUIRE(names.size() == 1);
    CHECK(textBottom(found[0]) < textTop(names[0]));
    for (const LineItem& line : list.lines) {
        if (line.role == Role::Chord) {
            CHECK(textBottom(found[0]) < std::min(line.from.y, line.to.y));
        }
    }
    CHECK(textTop(found[0]) >= list.top - kEpsilon);
}

TEST_CASE("Beat text: texts that would overlap stack upwards", "[notation][layout][text]") {
    const DisplayList list = layout(
        voice({{texted(sing(NoteValue::Eighth), "let it ring"),
                texted(sing(NoteValue::Eighth), "with feeling"), rest(), rest(NoteValue::Half)},
               {texted(sing(NoteValue::Whole), "a tempo")}}));
    const auto found = texts(list);
    REQUIRE(contents(found) == std::vector<std::string>{"let it ring", "with feeling", "a tempo"});
    // The second starts before the first ends: it goes on a row above.
    REQUIRE(found[1].origin.x < textRight(found[0]));
    CHECK(textBottom(found[1]) <= textTop(found[0]));
    // The third starts after both: back on the lowest row.
    REQUIRE(found[2].origin.x > textRight(found[1]));
    CHECK_THAT(found[2].origin.y, WithinAbs(found[0].origin.y, kEpsilon));
    CHECK(found[2].measure == 1);
    CHECK(textTop(found[1]) >= list.top - kEpsilon);
}

TEST_CASE("Beat text: on a rest, over the tablature alone, once over both",
          "[notation][layout][text]") {
    const Song song =
        guitar({{texted(rest(NoteValue::Whole), "tacet")}, {play(1, 0, NoteValue::Whole)}});
    for (const StaffView view : {StaffView::Standard, StaffView::Tablature}) {
        const DisplayList list = layout(song, view);
        const auto found = texts(list);
        REQUIRE(contents(found) == std::vector<std::string>{"tacet"});
        CHECK(found[0].measure == 0);
        REQUIRE(list.measures.size() == 2);
        CHECK(found[0].origin.x > list.measures[0].x);
        CHECK(textBottom(found[0]) < 0.0);
    }
    const std::array<StaffSelection, 2> staves{StaffSelection{.view = StaffView::Standard},
                                               StaffSelection{.view = StaffView::Tablature}};
    const DisplayList both = layoutSystem(song, staves, bravura());
    REQUIRE(texts(both).size() == 1);
    CHECK(textBottom(texts(both)[0]) < both.staves[0].y);
}
