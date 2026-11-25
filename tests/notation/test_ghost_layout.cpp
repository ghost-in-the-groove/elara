// SPDX-License-Identifier: AGPL-3.0-or-later
// Ghost notes on the standard staff: noteheads in parentheses (ADR-0099).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
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
using elara::notation::Role;

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

// A beat of the pitches, those marked by `ghost` as ghost notes; a rest
// without pitches.
Beat chord(const std::vector<std::string_view>& pitches, const std::vector<bool>& ghost,
           Duration duration = Duration(NoteValue::Quarter)) {
    Beat made{.duration = duration};
    for (std::size_t i = 0; i < pitches.size(); ++i) {
        if (pitches[i].empty()) {
            continue;
        }
        Note note{.pitch = *Pitch::parse(pitches[i])};
        note.effects.ghost = ghost[i];
        made.notes.push_back(note);
    }
    return made;
}

DisplayList layout(const std::vector<Beat>& beats) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    song.track(0).staff(0).measure(0).voices[0].beats = beats;
    return layoutStaff(song, 0, 0, bravura());
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Glyph glyph) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [glyph](const GlyphItem& item) { return item.glyph == glyph; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.y; });
    return found;
}

std::vector<GlyphItem> byRole(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x * item.scale;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x * item.scale;
}

} // namespace

TEST_CASE("Ghost notes: the notehead between parentheses", "[notation][layout][ghost]") {
    const DisplayList list = layout({chord({"C5"}, {true}), chord({"D5"}, {false}),
                                     chord({""}, {false}), chord({""}, {false})});
    const auto opening = glyphs(list, Glyph::NoteheadParenthesisLeft);
    const auto closing = glyphs(list, Glyph::NoteheadParenthesisRight);
    REQUIRE(opening.size() == 1);
    REQUIRE(closing.size() == 1);
    const auto heads = byRole(list, Role::Notehead);
    REQUIRE(heads.size() == 2);
    CHECK(right(opening[0]) <= left(heads[0]));
    CHECK(right(opening[0]) > left(heads[0]) - 0.3);
    CHECK(left(closing[0]) >= right(heads[0]));
    CHECK(left(closing[0]) < right(heads[0]) + 0.3);
    CHECK_THAT(opening[0].origin.y, WithinAbs(heads[0].origin.y, 1e-6));
    CHECK_THAT(closing[0].origin.y, WithinAbs(heads[0].origin.y, 1e-6));
    CHECK(right(closing[0]) < left(heads[1]));
}

TEST_CASE("Ghost notes: only the ghost notes of a chord, its accidentals and dots outside",
          "[notation][layout][ghost]") {
    const DisplayList list =
        layout({chord({"E4", "G#4", "C5"}, {false, true, false}, Duration(NoteValue::Half, 1)),
                chord({""}, {false})});
    const auto opening = glyphs(list, Glyph::NoteheadParenthesisLeft);
    const auto closing = glyphs(list, Glyph::NoteheadParenthesisRight);
    REQUIRE(opening.size() == 1);
    REQUIRE(closing.size() == 1);
    const auto sharps = byRole(list, Role::Accidental);
    REQUIRE(sharps.size() == 1);
    CHECK_THAT(opening[0].origin.y, WithinAbs(sharps[0].origin.y, 1e-6)); // around the G#
    CHECK(right(sharps[0]) <= left(opening[0]));
    const auto dots = byRole(list, Role::Dot);
    REQUIRE(!dots.empty());
    for (const GlyphItem& dot : dots) {
        CHECK(left(dot) >= right(closing[0]));
    }
}
