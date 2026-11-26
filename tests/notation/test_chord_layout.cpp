// SPDX-License-Identifier: AGPL-3.0-or-later
// Chord symbols above the staff: names, and diagrams on fretted staves (ADR-0080).
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
using elara::notation::Glyph;
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
constexpr std::int8_t x = ChordDiagram::kMuted;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

SharedOptional<ChordSymbol> chord(std::string name, std::optional<ChordDiagram> diagram = {}) {
    return SharedOptional(ChordSymbol{.name = std::move(name), .diagram = std::move(diagram)});
}

// The note played at `fret` on `string` of a guitar in standard tuning.
Note fretted(int string, int fret) {
    return Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                         .fret = static_cast<std::int8_t>(fret)}};
}

Beat play(int string, int fret, NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value), .notes = {fretted(string, fret)}};
}

Beat sing(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value), .notes = {Note{.pitch = *Pitch::parse("B4")}}};
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

Beat named(Beat beat, SharedOptional<ChordSymbol> symbol) {
    beat.chord = std::move(symbol);
    return beat;
}

// A guitar with one voice per measure.
Song guitar(const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

// A treble staff without frets, one voice per measure.
Song voice(const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Voice", Instrument{.name = "Voice"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

DisplayList layout(const Song& song, StaffView view = StaffView::Standard) {
    const StaffSelection selection{.view = view};
    return layoutSystem(song, std::span(&selection, 1), bravura());
}

DisplayList notationAndTab(const Song& song) {
    const std::array<StaffSelection, 2> staves{StaffSelection{.view = StaffView::Standard},
                                               StaffSelection{.view = StaffView::Tablature}};
    return layoutSystem(song, staves, bravura());
}

std::vector<TextItem> names(const DisplayList& list) {
    std::vector<TextItem> found;
    for (const TextItem& item : list.texts) {
        if (item.role == Role::Chord) {
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

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role,
                              std::optional<Glyph> only = std::nullopt) {
    std::vector<GlyphItem> found;
    for (const GlyphItem& item : list.glyphs) {
        if (item.role == role && (!only || item.glyph == *only)) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

// The strings (vertical) and frets (horizontal) of the diagrams, left to
// right and top to bottom.
struct Grid {
    std::vector<LineItem> strings;
    std::vector<LineItem> frets;
};

Grid grid(const DisplayList& list) {
    Grid found;
    for (const LineItem& line : list.lines) {
        if (line.role != Role::Chord) {
            continue;
        }
        (line.from.x == line.to.x ? found.strings : found.frets).push_back(line);
    }
    std::ranges::stable_sort(found.strings, {}, [](const LineItem& line) { return line.from.x; });
    std::ranges::stable_sort(found.frets, {}, [](const LineItem& line) { return line.from.y; });
    return found;
}

double glyphTop(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y * item.scale;
}

double glyphBottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y * item.scale;
}

double glyphCentre(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return item.origin.x + (box.southWest.x + box.northEast.x) / 2 * item.scale;
}

double glyphMiddle(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return item.origin.y - (box.southWest.y + box.northEast.y) / 2 * item.scale;
}

// Text extents as layout assumes them (ascenders 0.75 em, descenders 0.21 em).
double textTop(const TextItem& item) {
    return item.origin.y - 0.75 * item.size;
}

double textBottom(const TextItem& item) {
    return item.origin.y + 0.21 * item.size;
}

double textLeft(const TextItem& item) {
    const double width = textWidth(item.text, item.size);
    switch (item.align) {
    case TextAlign::Left:
        return item.origin.x;
    case TextAlign::Centre:
        return item.origin.x - width / 2;
    case TextAlign::Right:
        return item.origin.x - width;
    }
    return item.origin.x;
}

double textRight(const TextItem& item) {
    return textLeft(item) + textWidth(item.text, item.size);
}

double lineTop(const LineItem& line) {
    return std::min(line.from.y, line.to.y);
}

double lineBottom(const LineItem& line) {
    return std::max(line.from.y, line.to.y);
}

} // namespace

TEST_CASE("Chord symbols: names centred over their beats, on one baseline above the staff",
          "[notation][layout][chords]") {
    // A staff without frets shows names only, diagrams or not.
    const DisplayList list =
        layout(voice({{named(sing(), chord("C", ChordDiagram{.frets = {0, 1, 0, 2, 3, x}})), rest(),
                       named(sing(NoteValue::Half), chord("G7"))}}));
    const auto found = names(list);
    REQUIRE(contents(found) == std::vector<std::string>{"C", "G7"});
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(found[i].align == TextAlign::Centre);
        CHECK_THAT(found[i].origin.x, WithinAbs(glyphCentre(heads[i]), kEpsilon));
        CHECK(found[i].measure == 0);
        CHECK(textBottom(found[i]) < glyphTop(heads[i]));
        CHECK(textBottom(found[i]) < 0.0);
        CHECK(textTop(found[i]) >= list.top - kEpsilon);
    }
    CHECK_THAT(found[0].origin.y, WithinAbs(found[1].origin.y, kEpsilon));
    CHECK(grid(list).strings.empty());
    CHECK(glyphs(list, Role::Chord).empty());

    // No chord symbols: nothing above the staff.
    const DisplayList plain = layout(voice({{sing(), rest(), sing(NoteValue::Half)}}));
    CHECK(names(plain).empty());
    CHECK(plain.top > list.top);
    CHECK(roleName(Role::Chord) == "chord");
}

TEST_CASE("Chord symbols: a diagram under the name on fretted staves",
          "[notation][layout][chords]") {
    // A minor: string 6 not played, 5 and 1 open, fingers on 4, 3 and 2.
    const DisplayList list = layout(guitar(
        {{named(play(5, 0), chord("Am", ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}})),
          play(3, 2, NoteValue::Half), play(2, 1)}}));
    const Grid lines = grid(list);
    REQUIRE(lines.strings.size() == 6);
    REQUIRE(lines.frets.size() == 5); // four frets at least
    const double space = lines.strings[1].from.x - lines.strings[0].from.x;
    CHECK(space > 0.3);
    for (std::size_t i = 1; i < 6; ++i) {
        CHECK_THAT(lines.strings[i].from.x - lines.strings[i - 1].from.x, WithinAbs(space, 1e-6));
        CHECK_THAT(lineTop(lines.strings[i]), WithinAbs(lines.frets.front().from.y, 1e-6));
        CHECK_THAT(lineBottom(lines.strings[i]), WithinAbs(lines.frets.back().from.y, 1e-6));
    }
    const double fret = lines.frets[1].from.y - lines.frets[0].from.y;
    for (std::size_t i = 1; i < 5; ++i) {
        CHECK_THAT(lines.frets[i].from.y - lines.frets[i - 1].from.y, WithinAbs(fret, 1e-6));
    }
    // The nut: the first fret line is thicker at the first fret.
    CHECK(lines.frets[0].thickness > lines.frets[1].thickness);

    // Centred over the notehead.
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(!heads.empty());
    const double centre = glyphCentre(heads.front());
    CHECK_THAT((lines.strings.front().from.x + lines.strings.back().from.x) / 2,
               WithinAbs(centre, 1e-6));

    // String 1 on the right; dots between the fret lines, open and muted
    // strings marked above the nut.
    const auto stringX = [&](int string) {
        return lines.strings[static_cast<std::size_t>(6 - string)].from.x;
    };
    const auto dots = glyphs(list, Role::Chord, Glyph::FretboardFilledCircle);
    REQUIRE(dots.size() == 3);
    CHECK_THAT(glyphCentre(dots[0]), WithinAbs(stringX(4), 1e-6));
    CHECK_THAT(glyphCentre(dots[1]), WithinAbs(stringX(3), 1e-6));
    CHECK_THAT(glyphCentre(dots[2]), WithinAbs(stringX(2), 1e-6));
    CHECK_THAT(glyphMiddle(dots[2]), WithinAbs(lines.frets[0].from.y + fret / 2, 1e-6));
    CHECK_THAT(glyphMiddle(dots[0]), WithinAbs(lines.frets[1].from.y + fret / 2, 1e-6));
    CHECK(glyphBottom(dots[0]) - glyphTop(dots[0]) < space);
    const auto open = glyphs(list, Role::Chord, Glyph::FretboardO);
    REQUIRE(open.size() == 2);
    CHECK_THAT(glyphCentre(open[0]), WithinAbs(stringX(5), 1e-6));
    CHECK_THAT(glyphCentre(open[1]), WithinAbs(stringX(1), 1e-6));
    const auto muted = glyphs(list, Role::Chord, Glyph::FretboardX);
    REQUIRE(muted.size() == 1);
    CHECK_THAT(glyphCentre(muted[0]), WithinAbs(stringX(6), 1e-6));
    for (const GlyphItem& mark : {open[0], open[1], muted[0]}) {
        CHECK(glyphBottom(mark) < lines.frets[0].from.y);
    }

    // The name above it all, and the whole above the staff and in the list's top.
    const auto found = names(list);
    REQUIRE(contents(found) == std::vector<std::string>{"Am"});
    CHECK_THAT(found[0].origin.x, WithinAbs(centre, 1e-6));
    CHECK(textBottom(found[0]) < glyphTop(open[0]));
    CHECK(lineBottom(lines.strings[0]) < 0.0);
    CHECK(textTop(found[0]) >= list.top - kEpsilon);
    for (const LineItem& line : lines.frets) {
        CHECK(line.measure == 0);
    }
}

TEST_CASE("Chord symbols: a diagram is large enough to read beside the tablature (ADR-0113)",
          "[notation][layout][chords]") {
    const DisplayList list = layout(guitar(
        {{named(play(5, 0), chord("Am", ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}})),
          play(3, 2, NoteValue::Half), play(2, 1)}}));
    const Grid lines = grid(list);
    REQUIRE(lines.strings.size() == 6);
    REQUIRE(lines.frets.size() == 5);
    // Strings and frets far enough apart to tell which string a dot is on:
    // the grid of six strings is wider than the staff is high.
    const double space = lines.strings[1].from.x - lines.strings[0].from.x;
    const double fret = lines.frets[1].from.y - lines.frets[0].from.y;
    CHECK(space >= 0.8);
    CHECK(fret >= 0.95);
    CHECK(lines.strings.back().from.x - lines.strings.front().from.x > 4.0);
    // A dot fills most of the room between two strings and never touches
    // the next one; the open and muted marks are as large.
    const auto dots = glyphs(list, Role::Chord, Glyph::FretboardFilledCircle);
    REQUIRE(!dots.empty());
    const double dot = glyphBottom(dots[0]) - glyphTop(dots[0]);
    CHECK(dot > 0.6 * space);
    CHECK(dot < space);
    const auto open = glyphs(list, Role::Chord, Glyph::FretboardO);
    REQUIRE(!open.empty());
    CHECK_THAT(open[0].scale, WithinAbs(dots[0].scale, kEpsilon));
}

TEST_CASE("Chord symbols: a diagram up the neck shows its first fret, a long one more frets",
          "[notation][layout][chords]") {
    const DisplayList high = layout(guitar(
        {{named(play(4, 5, NoteValue::Whole),
                chord("C#m7b5", ChordDiagram{.firstFret = 4, .frets = {x, 5, 5, 4, 5, x}}))}}));
    const Grid lines = grid(high);
    REQUIRE(lines.frets.size() == 5);
    CHECK_THAT(lines.frets[0].thickness, WithinAbs(lines.frets[1].thickness, kEpsilon));
    const auto labels = names(high);
    REQUIRE(contents(labels) == std::vector<std::string>{"4", "C#m7b5"});
    // Left of the strings, by the first fret.
    CHECK(textRight(labels[0]) < lines.strings.front().from.x);
    CHECK(textTop(labels[0]) < lines.frets[1].from.y);
    CHECK(textBottom(labels[0]) > lines.frets[0].from.y);
    const auto dots = glyphs(high, Role::Chord, Glyph::FretboardFilledCircle);
    REQUIRE(dots.size() == 4);
    const double fret = lines.frets[1].from.y - lines.frets[0].from.y;
    // Left to right: strings 5 to 2, at frets 5, 4, 5 and 5.
    CHECK_THAT(glyphMiddle(dots[1]), WithinAbs(lines.frets[0].from.y + fret / 2, 1e-6));
    CHECK_THAT(glyphMiddle(dots[0]), WithinAbs(lines.frets[1].from.y + fret / 2, 1e-6));
    CHECK_THAT(glyphMiddle(dots[3]), WithinAbs(lines.frets[1].from.y + fret / 2, 1e-6));
    CHECK(glyphs(high, Role::Chord, Glyph::FretboardX).size() == 2);

    // A chord spanning five frets draws five.
    const DisplayList wide = layout(guitar(
        {{named(play(1, 5, NoteValue::Whole),
                chord("Gadd9", ChordDiagram{.firstFret = 1, .frets = {5, 3, 0, 0, 2, 3}}))}}));
    CHECK(grid(wide).frets.size() == 6);
}

TEST_CASE("Chord symbols: above the tablature alone, once over notation and tablature",
          "[notation][layout][chords]") {
    const Song song =
        guitar({{named(play(2, 1, NoteValue::Whole),
                       chord("C", ChordDiagram{.firstFret = 1, .frets = {0, 1, 0, 2, 3, x}}))}});
    const DisplayList tab = layout(song, StaffView::Tablature);
    REQUIRE(contents(names(tab)) == std::vector<std::string>{"C"});
    const Grid tabGrid = grid(tab);
    REQUIRE(tabGrid.strings.size() == 6);
    CHECK(lineBottom(tabGrid.strings[0]) < 0.0);
    CHECK(textTop(names(tab)[0]) >= tab.top - kEpsilon);

    const DisplayList both = notationAndTab(song);
    REQUIRE(contents(names(both)) == std::vector<std::string>{"C"});
    const Grid bothGrid = grid(both);
    REQUIRE(bothGrid.strings.size() == 6);
    CHECK(lineBottom(bothGrid.strings[0]) < both.staves[0].y);
}

TEST_CASE("Chord symbols: above the technique marks", "[notation][layout][chords]") {
    Beat muted = play(6, 0);
    muted.notes.front().effects.palmMute = true;
    const DisplayList list = layout(guitar(
        {{named(muted, chord("E5", ChordDiagram{.firstFret = 1, .frets = {x, x, x, 2, 2, 0}})),
          play(6, 0), play(6, 0), play(6, 0)}}));
    double techniqueTop = 0.0;
    bool any = false;
    for (const TextItem& item : list.texts) {
        if (item.role == Role::Technique) {
            techniqueTop = std::min(techniqueTop, textTop(item));
            any = true;
        }
    }
    REQUIRE(any);
    const Grid lines = grid(list);
    REQUIRE(!lines.frets.empty());
    CHECK(lines.frets.back().from.y < techniqueTop);
}

TEST_CASE("Chord symbols: neighbours never overlap, the notes under them keep their places",
          "[notation][layout][chords]") {
    const auto eighths = [](std::optional<std::string> first, std::optional<std::string> second,
                            std::optional<std::string> last) {
        std::vector<Beat> beats(8, sing(NoteValue::Eighth));
        if (first) {
            beats[0].chord = chord(*first);
        }
        if (second) {
            beats[1].chord = chord(*second);
        }
        if (last) {
            beats[7].chord = chord(*last);
        }
        return voice({beats, beats});
    };

    const DisplayList crowded = layout(eighths("Cmaj7#11", "Dbmaj9", "Ebm7b5/Bb"));
    const auto found = names(crowded);
    REQUIRE(found.size() == 6);
    for (std::size_t i = 1; i < found.size(); ++i) {
        CHECK(textLeft(found[i]) > textRight(found[i - 1]) + 0.3);
    }
    // Within the measure: clear of the barline after it.
    double firstBarline = crowded.width;
    for (const LineItem& line : crowded.lines) {
        if (line.role == Role::Barline && line.measure == 0) {
            firstBarline = std::min(firstBarline, line.from.x);
        }
    }
    CHECK(textRight(found[2]) < firstBarline);
    CHECK(textLeft(found[3]) > firstBarline);

    // A short name over the first note moves nothing.
    const auto heads = [](const DisplayList& list) {
        std::vector<double> at;
        for (const GlyphItem& item : glyphs(list, Role::Notehead)) {
            at.push_back(item.origin.x);
        }
        return at;
    };
    const DisplayList plain = layout(eighths(std::nullopt, std::nullopt, std::nullopt));
    const DisplayList one = layout(eighths("C", std::nullopt, std::nullopt));
    CHECK(heads(one) == heads(plain));
}

TEST_CASE("Chord symbols: on a measure of rests", "[notation][layout][chords]") {
    const Song song =
        guitar({{named(rest(NoteValue::Whole), chord("N.C."))}, {play(1, 0, NoteValue::Whole)}});
    for (const StaffView view : {StaffView::Standard, StaffView::Tablature}) {
        const DisplayList list = layout(song, view);
        const auto found = names(list);
        REQUIRE(contents(found) == std::vector<std::string>{"N.C."});
        CHECK(found[0].measure == 0);
        REQUIRE(list.measures.size() == 2);
        CHECK(textLeft(found[0]) > list.measures[0].x);
        CHECK(textRight(found[0]) < list.measures[0].x + list.measures[0].width);
    }
}
