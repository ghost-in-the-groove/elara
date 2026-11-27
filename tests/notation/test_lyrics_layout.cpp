// SPDX-License-Identifier: AGPL-3.0-or-later
// Lyrics under the staff (ADR-0084).
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
#include <span>
#include <string>
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
using elara::core::Pitch;
using elara::core::SharedOptional;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Syllable;
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
using elara::notation::StaffLayoutOptions;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::TextAlign;
using elara::notation::TextFace;
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

Beat note(NoteValue value = NoteValue::Quarter, int midi = 71) {
    return Beat{.duration = Duration(value), .notes = {Note{.pitch = Pitch::fromMidi(midi)}}};
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

// `beat` singing `verses`: "be-" joins the next syllable, "low_" is held, ""
// sings nothing in that verse.
Beat sung(Beat beat, const std::vector<std::string>& verses) {
    std::vector<Syllable> syllables;
    for (std::string text : verses) {
        Syllable syllable;
        if (text.size() > 1 && text.ends_with('_')) {
            text.pop_back();
            syllable.extend = true;
        }
        if (text.size() > 1 && text.ends_with('-')) {
            text.pop_back();
            syllable.hyphen = true;
        }
        syllable.text = std::move(text);
        syllables.push_back(std::move(syllable));
    }
    beat.lyrics = SharedOptional(std::move(syllables));
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

DisplayList layout(const Song& song, StaffView view = StaffView::Standard,
                   const StaffLayoutOptions& options = {}) {
    const StaffSelection selection{.view = view};
    return layoutSystem(song, std::span(&selection, 1), bravura(), options);
}

std::vector<TextItem> syllables(const DisplayList& list) {
    std::vector<TextItem> found;
    for (const TextItem& item : list.texts) {
        if (item.role == Role::Lyric) {
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

// The texts of `items` in alphabetical order, for syllables sharing a beat.
std::vector<std::string> sorted(const std::vector<TextItem>& items) {
    std::vector<std::string> out = contents(items);
    std::ranges::sort(out);
    return out;
}

std::vector<LineItem> lines(const DisplayList& list) {
    std::vector<LineItem> found;
    for (const LineItem& item : list.lines) {
        if (item.role == Role::Lyric) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const LineItem& item) { return item.from.x; });
    return found;
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    for (const GlyphItem& item : list.glyphs) {
        if (item.role == role) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double glyphBottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y * item.scale;
}

double glyphTop(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y * item.scale;
}

double headWidth() {
    return bravura().metrics(Glyph::NoteheadBlack).box.width();
}

// Text extents as layout assumes them (ascenders 0.75 em, descenders 0.21 em).
double textLeft(const TextItem& item) {
    const double width = textWidth(item.text, item.size);
    return item.align == TextAlign::Centre ? item.origin.x - width / 2 : item.origin.x;
}

double textRight(const TextItem& item) {
    return textLeft(item) + textWidth(item.text, item.size);
}

double textTop(const TextItem& item) {
    return item.origin.y - 0.75 * item.size;
}

double textBottom(const TextItem& item) {
    return item.origin.y + 0.21 * item.size;
}

} // namespace

TEST_CASE("Lyrics: syllables centred under their notes, a verse per line under the staff",
          "[notation][layout][lyrics]") {
    const DisplayList list =
        layout(voice({{sung(note(), {"be-", "oh"}), sung(note(), {"low"}), rest(),
                       sung(note(NoteValue::Quarter, 60), {"", "la"})}}));
    const auto found = syllables(list);
    REQUIRE(contents(found) == std::vector<std::string>{"be", "oh", "low", "la"});
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 3);
    const std::array<std::size_t, 4> headOf{0, 0, 1, 2};
    for (std::size_t i = 0; i < found.size(); ++i) {
        CHECK(found[i].align == TextAlign::Centre);
        CHECK(found[i].face == TextFace::Regular);
        CHECK_THAT(found[i].origin.x,
                   WithinAbs(heads[headOf[i]].origin.x + headWidth() / 2, kEpsilon));
        CHECK(found[i].measure == 0);
        // Under the staff and under every note, the middle C's ledger line too.
        CHECK(textTop(found[i]) > 4.0);
        CHECK(textTop(found[i]) > glyphBottom(heads[2]));
        CHECK(textBottom(found[i]) <= list.bottom + kEpsilon);
    }
    // Verse 1 on one line, verse 2 under it.
    CHECK_THAT(found[0].origin.y, WithinAbs(found[2].origin.y, kEpsilon));
    CHECK_THAT(found[1].origin.y, WithinAbs(found[3].origin.y, kEpsilon));
    CHECK(textTop(found[1]) > textBottom(found[0]));

    // A hyphen between the syllables of a word, level with the middle of the text.
    const auto drawn = lines(list);
    REQUIRE(drawn.size() == 1);
    const LineItem& hyphen = drawn[0];
    CHECK_THAT(hyphen.from.y, WithinAbs(hyphen.to.y, kEpsilon));
    CHECK(hyphen.from.x > textRight(found[0]));
    CHECK(hyphen.to.x < textLeft(found[2]));
    CHECK(hyphen.from.y < found[0].origin.y);
    CHECK(hyphen.from.y > textTop(found[0]));
    CHECK(roleName(Role::Lyric) == "lyric");
}

TEST_CASE("Lyrics: a held syllable starts at its note, a line under the notes it is held over",
          "[notation][layout][lyrics]") {
    const DisplayList list = layout(voice({{sung(note(), {"love_"}), note(NoteValue::Quarter, 72),
                                            note(NoteValue::Quarter, 74), sung(note(), {"me"})}}));
    const auto found = syllables(list);
    REQUIRE(contents(found) == std::vector<std::string>{"love", "me"});
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    CHECK(found[0].align == TextAlign::Left);
    CHECK_THAT(found[0].origin.x, WithinAbs(heads[0].origin.x, kEpsilon));
    const auto drawn = lines(list);
    REQUIRE(drawn.size() == 1);
    const LineItem& extender = drawn[0];
    CHECK_THAT(extender.from.y, WithinAbs(extender.to.y, kEpsilon));
    CHECK_THAT(extender.from.y, WithinAbs(found[0].origin.y, kEpsilon));
    CHECK(extender.from.x > textRight(found[0]));
    // To the end of the last note held, short of the next syllable.
    CHECK_THAT(extender.to.x, WithinAbs(heads[2].origin.x + headWidth(), kEpsilon));
    CHECK(extender.to.x < textLeft(found[1]));

    // A rest ends what a syllable is held over; a syllable with nothing after
    // it to hold draws no line.
    const DisplayList cut =
        layout(voice({{sung(note(), {"oh_"}), note(), rest(), sung(note(), {"ah_"})}}));
    const auto cutHeads = glyphs(cut, Role::Notehead);
    REQUIRE(cutHeads.size() == 3);
    const auto cutLines = lines(cut);
    REQUIRE(cutLines.size() == 1);
    CHECK_THAT(cutLines[0].to.x, WithinAbs(cutHeads[1].origin.x + headWidth(), kEpsilon));
}

TEST_CASE("Lyrics: notes make room for their syllables", "[notation][layout][lyrics]") {
    std::vector<Beat> plain;
    std::vector<Beat> words;
    const std::array<std::string, 8> said{"Ex-", "tra-", "or-", "di-", "na-", "ri-", "ly", "long"};
    for (const std::string& syllable : said) {
        plain.push_back(note(NoteValue::Eighth));
        words.push_back(sung(note(NoteValue::Eighth), {syllable}));
    }
    const DisplayList list = layout(voice({words}));
    const auto found = syllables(list);
    REQUIRE(found.size() == said.size());
    for (std::size_t i = 0; i + 1 < found.size(); ++i) {
        CHECK(textRight(found[i]) < textLeft(found[i + 1]));
    }
    // The hyphens fit between their syllables.
    const auto hyphens = lines(list);
    REQUIRE(hyphens.size() == 6);
    for (std::size_t i = 0; i < hyphens.size(); ++i) {
        CHECK(hyphens[i].from.x > textRight(found[i]));
        CHECK(hyphens[i].to.x < textLeft(found[i + 1]));
    }
    CHECK(glyphs(list, Role::Notehead).back().origin.x >
          glyphs(layout(voice({plain})), Role::Notehead).back().origin.x);
}

TEST_CASE("Lyrics: dynamics go under them", "[notation][layout][lyrics]") {
    Beat loud = sung(note(NoteValue::Whole), {"Ah", "Oh"});
    loud.notes[0].effects.dynamic = Dynamic::FF;
    const DisplayList list = layout(voice({{loud}}));
    const auto found = syllables(list);
    REQUIRE(found.size() == 2);
    const auto marks = glyphs(list, Role::Dynamic);
    REQUIRE(marks.size() == 1);
    CHECK(glyphTop(marks[0]) > textBottom(found[1]));
}

TEST_CASE("Lyrics: words and held syllables go on across lines", "[notation][layout][lyrics]") {
    const Song song = voice({{note(NoteValue::Half), sung(note(), {"go-", "hold_"}), note()},
                             {note(), sung(note(), {"ing", "on"}), rest(NoteValue::Half)}});
    // The first line ends after measure 1: a hyphen after "go", the line under
    // "hold" to the end of the line.
    const DisplayList first = layout(song, StaffView::Standard, {.measureCount = 1});
    auto firstWords = syllables(first);
    REQUIRE(sorted(firstWords) == std::vector<std::string>{"go", "hold"});
    if (firstWords[0].text != "go") {
        std::swap(firstWords[0], firstWords[1]);
    }
    auto firstLines = lines(first);
    REQUIRE(firstLines.size() == 2);
    if (firstLines[0].from.y > firstLines[1].from.y) {
        std::swap(firstLines[0], firstLines[1]);
    }
    const LineItem& hyphen = firstLines[0];
    const LineItem& held = firstLines[1];
    const auto heads = glyphs(first, Role::Notehead);
    REQUIRE(heads.size() == 3);
    CHECK(hyphen.from.x > textRight(firstWords[0]));
    CHECK(hyphen.to.x <= first.width + kEpsilon);
    CHECK(held.from.x > textRight(firstWords[1]));
    CHECK_THAT(held.from.y, WithinAbs(firstWords[1].origin.y, kEpsilon));
    CHECK(held.to.x > heads[2].origin.x + headWidth());
    CHECK(held.to.x <= first.width + kEpsilon);

    // The next line: the held notes go on to the first note, no hyphen before "ing".
    const DisplayList second = layout(song, StaffView::Standard, {.firstMeasure = 1});
    auto secondWords = syllables(second);
    REQUIRE(sorted(secondWords) == std::vector<std::string>{"ing", "on"});
    if (secondWords[1].text != "on") {
        std::swap(secondWords[0], secondWords[1]);
    }
    const auto secondHeads = glyphs(second, Role::Notehead);
    REQUIRE(secondHeads.size() == 2);
    const auto secondLines = lines(second);
    REQUIRE(secondLines.size() == 1);
    CHECK_THAT(secondLines[0].from.y, WithinAbs(secondWords[1].origin.y, kEpsilon));
    CHECK(secondLines[0].from.x <= secondHeads[0].origin.x + kEpsilon);
    CHECK_THAT(secondLines[0].to.x, WithinAbs(secondHeads[0].origin.x + headWidth(), kEpsilon));
}

TEST_CASE("Lyrics: under the tablature only when no standard notation shows",
          "[notation][layout][lyrics]") {
    const Song song =
        guitar({{sung(play(1, 0), {"Sing"}), sung(play(1, 2), {"a"}), rest(NoteValue::Half)}});
    const DisplayList tab = layout(song, StaffView::Tablature);
    const auto underTab = syllables(tab);
    REQUIRE(contents(underTab) == std::vector<std::string>{"Sing", "a"});
    CHECK(textTop(underTab[0]) > glyphBottom(glyphs(tab, Role::Rest).front()));

    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const DisplayList system = layoutSystem(song, both, bravura());
    CHECK(contents(syllables(system)) == std::vector<std::string>{"Sing", "a"});
}
