// SPDX-License-Identifier: AGPL-3.0-or-later
// Dynamics marks under the staff (ADR-0044).
#include <elara/core/effects.hpp>
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
#include <initializer_list>
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
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::Anchor;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::Role;
using elara::notation::StaffBox;
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

Note pitched(std::string_view text, Dynamic dynamic) {
    Note note{.pitch = *Pitch::parse(text)};
    note.effects.dynamic = dynamic;
    return note;
}

// A note (B4 unless said otherwise) played at `dynamic`.
Beat play(Dynamic dynamic, NoteValue value = NoteValue::Quarter, std::string_view text = "B4") {
    return Beat{.duration = Duration(value), .notes = {pitched(text, dynamic)}};
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

// One treble staff; measures[m] holds the beats of the first voice of measure m.
Song songOf(const std::vector<std::vector<Beat>>& measures) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

// A guitar note at `fret` on `string`, played at `dynamic`.
Beat fretted(int string, int fret, Dynamic dynamic) {
    Note note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
              .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                       .fret = static_cast<std::int8_t>(fret)}};
    note.effects.dynamic = dynamic;
    return Beat{.duration = Duration(NoteValue::Quarter), .notes = {note}};
}

Song guitar(const std::vector<Beat>& beats) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    song.track(0).staff(0).measure(0).voices[0].beats = beats;
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

std::vector<Glyph> kinds(const std::vector<GlyphItem>& items) {
    std::vector<Glyph> out;
    for (const GlyphItem& item : items) {
        out.push_back(item.glyph);
    }
    return out;
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

// Where the eye takes a dynamic's middle to be.
double opticalCentre(const GlyphItem& item) {
    const auto anchor = bravura().metrics(item.glyph).anchor(Anchor::OpticalCenter);
    REQUIRE(anchor.has_value());
    return item.origin.x + anchor->x;
}

double centre(const GlyphItem& item) {
    return (left(item) + right(item)) / 2;
}

} // namespace

TEST_CASE("Dynamics: a mark where the loudness changes, none for mf at the start",
          "[notation][layout][dynamics]") {
    const DisplayList list =
        layout(songOf({{play(Dynamic::MF), play(Dynamic::F), play(Dynamic::F), play(Dynamic::P)}}));
    const auto marks = glyphs(list, Role::Dynamic);
    REQUIRE(marks.size() == 2);
    CHECK(kinds(marks) == std::vector{Glyph::DynamicForte, Glyph::DynamicPiano});

    // Centred by eye under the notes where the loudness changes.
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    CHECK_THAT(opticalCentre(marks[0]), WithinAbs(centre(heads[1]), kEpsilon));
    CHECK_THAT(opticalCentre(marks[1]), WithinAbs(centre(heads[3]), kEpsilon));

    // On one baseline under the staff, clear of its bottom line.
    CHECK_THAT(marks[0].origin.y, WithinAbs(marks[1].origin.y, kEpsilon));
    for (const GlyphItem& mark : marks) {
        CHECK(top(mark) > 4.5);
        CHECK(mark.measure == 0);
    }
    CHECK(list.bottom >= bottom(marks[0]) - kEpsilon);

    // Every note at mf: nothing to say.
    const DisplayList plain = layout(songOf({{play(Dynamic::MF), play(Dynamic::MF)}}));
    CHECK(glyphs(plain, Role::Dynamic).empty());
}

TEST_CASE("Dynamics: every dynamic has its mark, and marks never overlap",
          "[notation][layout][dynamics]") {
    const DisplayList list = layout(songOf({
        {play(Dynamic::PPP), play(Dynamic::PP), play(Dynamic::P), play(Dynamic::MP)},
        {play(Dynamic::MF), play(Dynamic::F), play(Dynamic::FF), play(Dynamic::FFF)},
    }));
    const auto marks = glyphs(list, Role::Dynamic);
    CHECK(kinds(marks) == std::vector{Glyph::DynamicPPP, Glyph::DynamicPP, Glyph::DynamicPiano,
                                      Glyph::DynamicMP, Glyph::DynamicMF, Glyph::DynamicForte,
                                      Glyph::DynamicFF, Glyph::DynamicFFF});
    for (std::size_t i = 1; i < marks.size(); ++i) {
        CHECK(left(marks[i]) > right(marks[i - 1]));
    }
    // The first sits under its note; the others give way when they must.
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 8);
    CHECK_THAT(opticalCentre(marks[0]), WithinAbs(centre(heads[0]), kEpsilon));
    CHECK(right(marks.back()) <= list.width + kEpsilon);

    // Far too many for the room under their notes: they stay on the line.
    const DisplayList crowded = layout(songOf({
        {play(Dynamic::PPP, NoteValue::Eighth), play(Dynamic::FFF, NoteValue::Eighth),
         play(Dynamic::PPP, NoteValue::Eighth), play(Dynamic::FFF, NoteValue::Eighth),
         play(Dynamic::PPP, NoteValue::Eighth), play(Dynamic::FFF, NoteValue::Eighth),
         play(Dynamic::PPP, NoteValue::Eighth), play(Dynamic::FFF, NoteValue::Eighth)},
    }));
    const auto many = glyphs(crowded, Role::Dynamic);
    REQUIRE(many.size() == 8);
    for (const GlyphItem& mark : many) {
        CHECK(left(mark) >= -kEpsilon);
        CHECK(right(mark) <= crowded.width + kEpsilon);
    }
}

TEST_CASE("Dynamics: rests and tied notes keep the dynamic; a chord takes its loudest note",
          "[notation][layout][dynamics]") {
    Beat tied = play(Dynamic::FF);
    tied.notes[0].effects.tiedFromPrevious = true;
    Beat chord{.duration = Duration(NoteValue::Quarter),
               .notes = {pitched("G4", Dynamic::P), pitched("D5", Dynamic::F)}};
    const DisplayList list = layout(songOf({
        {play(Dynamic::P), rest(), play(Dynamic::P), tied},
        {chord, play(Dynamic::F), rest(NoteValue::Half)},
    }));
    const auto marks = glyphs(list, Role::Dynamic);
    REQUIRE(marks.size() == 2);
    CHECK(kinds(marks) == std::vector{Glyph::DynamicPiano, Glyph::DynamicForte});
    CHECK(marks[0].measure == 0);
    CHECK(marks[1].measure == 1);
}

TEST_CASE("Dynamics: a line starts with the dynamic in force", "[notation][layout][dynamics]") {
    const Song song = songOf({
        {play(Dynamic::P), play(Dynamic::P), play(Dynamic::P), play(Dynamic::P)},
        {rest(NoteValue::Whole)},
        {play(Dynamic::P), play(Dynamic::P), play(Dynamic::F), play(Dynamic::F)},
    });
    // From the third measure: p was in force, so only the f is marked.
    const auto marks = glyphs(layout(song, {.firstMeasure = 2}), Role::Dynamic);
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].glyph == Glyph::DynamicForte);
    CHECK(marks[0].measure == 2);
    // The whole song marks the p once.
    CHECK(kinds(glyphs(layout(song), Role::Dynamic)) ==
          std::vector{Glyph::DynamicPiano, Glyph::DynamicForte});
}

TEST_CASE("Dynamics: clear of the ink under the staff", "[notation][layout][dynamics]") {
    // Low notes with ledger lines and stems below the staff.
    const DisplayList list = layout(songOf({{play(Dynamic::F, NoteValue::Quarter, "E3"),
                                             play(Dynamic::P, NoteValue::Quarter, "C3")}}));
    const auto marks = glyphs(list, Role::Dynamic);
    REQUIRE(marks.size() == 2);
    double ink = 0.0;
    for (const GlyphItem& head : glyphs(list, Role::Notehead)) {
        ink = std::max(ink, bottom(head));
    }
    for (const auto& line : list.lines) {
        if (line.role != Role::StaffLine) {
            ink = std::max({ink, line.from.y, line.to.y});
        }
    }
    for (const GlyphItem& mark : marks) {
        CHECK(top(mark) > ink);
    }
}

TEST_CASE("Dynamics: under the standard notation, or under tablature shown alone",
          "[notation][layout][dynamics]") {
    const Song song = guitar({fretted(1, 0, Dynamic::P), fretted(1, 3, Dynamic::F)});
    const auto staffOf = [](const DisplayList& list, StaffView view) {
        const auto found = std::ranges::find(list.staves, view, &StaffBox::view);
        REQUIRE(found != list.staves.end());
        return *found;
    };

    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const DisplayList score = layoutSystem(song, both, bravura());
    const auto scoreMarks = glyphs(score, Role::Dynamic);
    REQUIRE(scoreMarks.size() == 2);
    const StaffBox standard = staffOf(score, StaffView::Standard);
    const StaffBox tablature = staffOf(score, StaffView::Tablature);
    for (const GlyphItem& mark : scoreMarks) {
        CHECK(top(mark) > standard.y + standard.height);
        CHECK(bottom(mark) < tablature.y);
    }

    const StaffSelection alone{.view = StaffView::Tablature};
    const DisplayList tab = layoutSystem(song, std::span(&alone, 1), bravura());
    const auto tabMarks = glyphs(tab, Role::Dynamic);
    REQUIRE(tabMarks.size() == 2);
    // Under the rhythm written below the tablature.
    double stems = 0.0;
    for (const auto& line : tab.lines) {
        if (line.role == Role::Stem) {
            stems = std::max({stems, line.from.y, line.to.y});
        }
    }
    const StaffBox strings = staffOf(tab, StaffView::Tablature);
    CHECK(stems > strings.y + strings.height);
    for (const GlyphItem& mark : tabMarks) {
        CHECK(top(mark) > stems);
    }
}
