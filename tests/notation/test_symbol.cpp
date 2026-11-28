// SPDX-License-Identifier: AGPL-3.0-or-later
// Picking a symbol of a laid-out score with the pointer (ADR-0130).
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/shared_value.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/symbol.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::ChordSymbol;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::NoteValue;
using elara::core::SharedOptional;
using elara::core::Song;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::BoundingBox;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::GlyphItem;
using elara::notation::layoutSystem;
using elara::notation::Point;
using elara::notation::Rect;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::Symbol;
using elara::notation::symbolAt;
using elara::notation::TextItem;

namespace {

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

constexpr std::array kEvery{Role::Clef,    Role::KeySignature, Role::TimeSignature, Role::Tempo,
                            Role::Section, Role::Chord,        Role::BeatText};

/// A guitar over three measures in D major: 3/4 from the second measure, which
/// starts the chorus; an A minor chord and a text over the first beat.
Song song() {
    Song made;
    made.appendMeasures(3);
    made.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    for (std::size_t m = 0; m < made.measureCount(); ++m) {
        made.measureHeader(m).keySignature = KeySignature(2, KeyMode::Major);
    }
    made.measureHeader(1).timeSignature = TimeSignature(3, 4);
    made.measureHeader(2).timeSignature = TimeSignature(3, 4);
    made.measureHeader(1).section = "Chorus";
    Staff& staff = made.track(0).staff(0);
    Beat first{.duration = Duration(NoteValue::Whole), .notes = {staff.frettedNote(1, 5)}};
    first.chord = SharedOptional(ChordSymbol{.name = "Am"});
    first.text = SharedOptional(std::string("let it ring"));
    staff.measure(0).voices[0].beats = {first};
    return made;
}

DisplayList both(const Song& laid) {
    const std::array<StaffSelection, 2> staves{StaffSelection{.view = StaffView::Standard},
                                               StaffSelection{.view = StaffView::Tablature}};
    return layoutSystem(laid, staves, bravura());
}

/// The middle of the ink of a glyph.
Point middleOf(const GlyphItem& glyph) {
    const BoundingBox& ink = bravura().metrics(glyph.glyph).box;
    return {glyph.origin.x + ((ink.southWest.x + ink.northEast.x) / 2.0 * glyph.scale),
            glyph.origin.y - ((ink.southWest.y + ink.northEast.y) / 2.0 * glyph.scale)};
}

/// The middle of the ink of the first glyph of `role` laid out for `measure`.
Point glyphOf(const DisplayList& list, Role role, std::size_t measure) {
    const auto found = std::ranges::find_if(list.glyphs, [role, measure](const GlyphItem& glyph) {
        return glyph.role == role && glyph.measure == measure;
    });
    REQUIRE(found != list.glyphs.end());
    return middleOf(*found);
}

/// A point inside the text `words`.
Point textOf(const DisplayList& list, const std::string& words) {
    const auto found = std::ranges::find(list.texts, words, &TextItem::text);
    REQUIRE(found != list.texts.end());
    return {found->origin.x, found->origin.y - (found->size * 0.3)};
}

bool inside(const Rect& box, Point point) {
    return point.x >= box.x && point.x <= box.x + box.width && point.y >= box.y &&
           point.y <= box.y + box.height;
}

} // namespace

TEST_CASE("Symbol: the glyph under the point says what it is and for which measure",
          "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);

    const Point key = glyphOf(list, Role::KeySignature, 0);
    const std::optional<Symbol> picked = symbolAt(list, key, kEvery, bravura());
    REQUIRE(picked);
    CHECK(picked->role == Role::KeySignature);
    CHECK(picked->measure == 0);
    CHECK(inside(picked->box, key));

    // The time signature of the first measure, and the one that changes it.
    const std::optional<Symbol> common =
        symbolAt(list, glyphOf(list, Role::TimeSignature, 0), kEvery, bravura());
    REQUIRE(common);
    CHECK(common->role == Role::TimeSignature);
    CHECK(common->measure == 0);
    const std::optional<Symbol> waltz =
        symbolAt(list, glyphOf(list, Role::TimeSignature, 1), kEvery, bravura());
    REQUIRE(waltz);
    CHECK(waltz->measure == 1);
    CHECK(waltz->box.x > common->box.x);

    const std::optional<Symbol> clef =
        symbolAt(list, glyphOf(list, Role::Clef, 0), kEvery, bravura());
    REQUIRE(clef);
    CHECK(clef->role == Role::Clef);
}

TEST_CASE("Symbol: a symbol is everything of its role drawn together", "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);

    // The two sharps of D major are one key signature: either picks both.
    std::vector<Point> sharps;
    for (const GlyphItem& glyph : list.glyphs) {
        if (glyph.role == Role::KeySignature && glyph.measure == 0) {
            sharps.push_back(middleOf(glyph));
        }
    }
    REQUIRE(sharps.size() == 2);
    const std::optional<Symbol> first = symbolAt(list, sharps[0], kEvery, bravura());
    const std::optional<Symbol> second = symbolAt(list, sharps[1], kEvery, bravura());
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
    CHECK(inside(first->box, sharps[0]));
    CHECK(inside(first->box, sharps[1]));

    // Both numbers of a time signature too.
    const std::optional<Symbol> time =
        symbolAt(list, glyphOf(list, Role::TimeSignature, 1), kEvery, bravura());
    REQUIRE(time);
    std::size_t digits = 0;
    for (const GlyphItem& glyph : list.glyphs) {
        if (glyph.role == Role::TimeSignature && glyph.measure == 1 &&
            inside(time->box, middleOf(glyph))) {
            ++digits;
        }
    }
    CHECK(digits >= 2);
}

TEST_CASE("Symbol: texts over the staff are picked by their words", "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);

    const std::optional<Symbol> section = symbolAt(list, textOf(list, "Chorus"), kEvery, bravura());
    REQUIRE(section);
    CHECK(section->role == Role::Section);
    CHECK(section->measure == 1);

    const std::optional<Symbol> chord = symbolAt(list, textOf(list, "Am"), kEvery, bravura());
    REQUIRE(chord);
    CHECK(chord->role == Role::Chord);
    CHECK(chord->measure == 0);

    const std::optional<Symbol> text =
        symbolAt(list, textOf(list, "let it ring"), kEvery, bravura());
    REQUIRE(text);
    CHECK(text->role == Role::BeatText);
}

TEST_CASE("Symbol: a text reaches as high and as low as layout makes room for",
          "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);
    const auto words = std::ranges::find(list.texts, std::string("let it ring"), &TextItem::text);
    REQUIRE(words != list.texts.end());

    const std::optional<Symbol> text =
        symbolAt(list, textOf(list, "let it ring"), kEvery, bravura(), 0.0);
    REQUIRE(text);
    // Layout keeps 0.75 em above a text's baseline and 0.21 em below it, as the
    // layout issues do.
    CHECK_THAT(text->box.y, WithinAbs(words->origin.y - (0.75 * words->size), 1e-9));
    CHECK_THAT(text->box.y + text->box.height,
               WithinAbs(words->origin.y + (0.21 * words->size), 1e-9));
}

TEST_CASE("Symbol: it names the staff it is drawn on", "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);
    REQUIRE(list.staves.size() == 2);

    // The key signature is on the standard notation; the tablature has its own clef.
    const std::optional<Symbol> key =
        symbolAt(list, glyphOf(list, Role::KeySignature, 0), kEvery, bravura());
    REQUIRE(key);
    REQUIRE(key->staff);
    CHECK(list.staves[*key->staff].view == StaffView::Standard);

    std::optional<Symbol> tabClef;
    for (const GlyphItem& glyph : list.glyphs) {
        if (glyph.role == Role::Clef && glyph.origin.y > list.staves[1].y - 0.5) {
            tabClef = symbolAt(list, glyph.origin, kEvery, bravura(), 1.0);
        }
    }
    REQUIRE(tabClef);
    REQUIRE(tabClef->staff);
    CHECK(list.staves[*tabClef->staff].view == StaffView::Tablature);
}

TEST_CASE("Symbol: nothing is picked away from the ink, or of a role not asked for",
          "[notation][symbol]") {
    const Song laid = song();
    const DisplayList list = both(laid);
    const Point key = glyphOf(list, Role::KeySignature, 0);

    // Far under the system.
    CHECK_FALSE(symbolAt(list, {key.x, list.bottom + 20.0}, kEvery, bravura()));
    // The key signature is there, but only clefs were asked for.
    constexpr std::array kClefs{Role::Clef};
    CHECK_FALSE(symbolAt(list, key, kClefs, bravura(), 0.0));
    // No role, no symbol.
    CHECK_FALSE(symbolAt(list, key, std::span<const Role>(), bravura()));

    // A little off the ink still picks it; further does not.
    const std::optional<Symbol> exact = symbolAt(list, key, kEvery, bravura(), 0.0);
    REQUIRE(exact);
    const Point above{exact->box.x + (exact->box.width / 2.0), exact->box.y - 0.3};
    constexpr std::array kKeys{Role::KeySignature};
    CHECK(symbolAt(list, above, kKeys, bravura()));
    CHECK_FALSE(symbolAt(list, above, kKeys, bravura(), 0.1));
}
