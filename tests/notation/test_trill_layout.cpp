// SPDX-License-Identifier: AGPL-3.0-or-later
// Trills over their notes, and in a lane over tablature (ADR-0090).
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
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Trill;
using elara::core::Tuning;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
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

Note trilled(std::string_view text, int semitones = 2) {
    Note note{.pitch = *Pitch::parse(text)};
    note.effects.trill = Trill{.semitones = static_cast<std::int8_t>(semitones)};
    return note;
}

Beat beatOf(NoteValue value, std::vector<Note> notes) {
    return Beat{.duration = Duration(value), .notes = std::move(notes)};
}

// One treble staff; voices[v] holds the beats of voice v of the only measure.
Song songOf(const std::vector<std::vector<Beat>>& voices) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t v = 0; v < voices.size(); ++v) {
        song.track(0).staff(0).measure(0).voices[v].beats = voices[v];
    }
    return song;
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role, Glyph which) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found), [&](const GlyphItem& item) {
        return item.role == role && item.glyph == which;
    });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

std::vector<GlyphItem> heads(const DisplayList& list) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [](const GlyphItem& item) { return item.role == Role::Notehead; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
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

} // namespace

TEST_CASE("Trills: tr over the note, a wavy line on to the next note when there is room",
          "[notation][layout][trill]") {
    // A trilled whole-measure-long half note, then a trilled quarter, then B4.
    const DisplayList list =
        layoutStaff(songOf({{beatOf(NoteValue::Half, {trilled("G5")}),
                             beatOf(NoteValue::Quarter, {trilled("B4")}),
                             beatOf(NoteValue::Quarter, {Note{.pitch = *Pitch::parse("B4")}})}}),
                    0, 0, bravura());
    const auto notes = heads(list);
    REQUIRE(notes.size() == 3);
    const auto marks = glyphs(list, Role::Ornament, Glyph::OrnamentTrill);
    REQUIRE(marks.size() == 2);
    const double headCentre = bravura().metrics(Glyph::NoteheadBlack).box.northEast.x / 2;
    for (std::size_t i = 0; i < 2; ++i) {
        CAPTURE(i);
        CHECK_THAT((left(marks[i]) + right(marks[i])) / 2,
                   WithinAbs(notes[i].origin.x + headCentre, kEpsilon));
        CHECK(bottom(marks[i]) < 0.0);                     // over the staff
        CHECK(bottom(marks[i]) < notes[i].origin.y - 0.5); // and its note
    }
    // The half note's wavy line runs from the mark to before the next note.
    const auto waves = glyphs(list, Role::Ornament, Glyph::WiggleTrill);
    REQUIRE(!waves.empty());
    CHECK(left(waves.front()) >= right(marks[0]));
    CHECK(right(waves.back()) < left(marks[1]));
    CHECK(top(marks[1]) > top(marks[0]) - 0.5); // so the next mark need not climb over it
    for (const GlyphItem& wave : waves) {
        CHECK(top(wave) > top(marks[0]));
        CHECK(bottom(wave) < bottom(marks[0]));
    }
}

TEST_CASE("Trills: under the staff for the lower voice of two", "[notation][layout][trill]") {
    const DisplayList list =
        layoutStaff(songOf({{beatOf(NoteValue::Whole, {Note{.pitch = *Pitch::parse("C5")}})},
                            {beatOf(NoteValue::Whole, {trilled("E4")})}}),
                    0, 0, bravura());
    const auto marks = glyphs(list, Role::Ornament, Glyph::OrnamentTrill);
    REQUIRE(marks.size() == 1);
    CHECK(top(marks[0]) > 4.0);
    // None without trills.
    CHECK(glyphs(layoutStaff(
                     songOf({{beatOf(NoteValue::Whole, {Note{.pitch = *Pitch::parse("C5")}})}}), 0,
                     0, bravura()),
                 Role::Ornament, Glyph::OrnamentTrill)
              .empty());
}

TEST_CASE("Trills: tablature shows tr and the other note's fret in a lane",
          "[notation][layout][trill]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    Note note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(1, 5)),
              .position = FretPosition{.string = 1, .fret = 5}};
    note.effects.trill = Trill{.semitones = 2};
    song.track(0).staff(0).measure(0).voices[0].beats = {beatOf(NoteValue::Whole, {note})};

    const auto laneOf = [](const DisplayList& list) {
        std::vector<std::string_view> texts;
        for (const TextItem& text : list.texts) {
            if (text.role == Role::Technique) {
                texts.push_back(text.text);
            }
        }
        return texts;
    };
    const StaffSelection tablature{.view = StaffView::Tablature};
    const DisplayList alone = layoutSystem(song, std::span(&tablature, 1), bravura());
    REQUIRE(glyphs(alone, Role::Technique, Glyph::OrnamentTrill).size() == 1);
    CHECK(laneOf(alone) == std::vector<std::string_view>{"(7)"});
    CHECK(glyphs(alone, Role::Technique, Glyph::OrnamentTrill)[0].origin.y < 0.0); // above it

    // Under standard notation the staff above has the trill as well.
    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const DisplayList system = layoutSystem(song, both, bravura());
    CHECK(glyphs(system, Role::Ornament, Glyph::OrnamentTrill).size() == 1);
    CHECK(glyphs(system, Role::Technique, Glyph::OrnamentTrill).size() == 1);
}
