// SPDX-License-Identifier: AGPL-3.0-or-later
// Fermatas over the beats that hold one (ADR-0085).
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
#include <cmath>
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
using elara::core::Fermata;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::Role;
using elara::notation::StaffBox;
using elara::notation::StaffSelection;
using elara::notation::StaffView;

namespace {

constexpr double kEpsilon = 1e-6;
constexpr double kGap = 0.5; // between a fermata and the ink or staff by it

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

Beat held(std::string_view pitch, Fermata fermata, NoteValue value = NoteValue::Quarter) {
    Beat beat{.duration = Duration(value)};
    if (!pitch.empty()) {
        beat.notes.push_back(Note{.pitch = *Pitch::parse(pitch)});
    }
    beat.effects.fermata = fermata;
    return beat;
}

Song treble(const std::vector<Beat>& first, const std::vector<Beat>& second = {}) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    song.track(0).staff(0).measure(0).voices[0].beats = first;
    song.track(0).staff(0).measure(0).voices[1].beats = second;
    return song;
}

std::vector<GlyphItem> glyphs(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.x; });
    return found;
}

double top(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.northEast.y;
}

double bottom(const GlyphItem& item) {
    return item.origin.y - bravura().metrics(item.glyph).box.southWest.y;
}

double centre(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return item.origin.x + (box.southWest.x + box.northEast.x) / 2;
}

// The top of the stems within 1.5 spaces of x.
double highestStem(const DisplayList& list, double x) {
    double highest = 0.0;
    for (const auto& line : list.lines) {
        if (line.role == Role::Stem && std::abs(line.from.x - x) < 1.5) {
            highest = std::min({highest, line.from.y, line.to.y});
        }
    }
    return highest;
}

} // namespace

TEST_CASE("Fermatas: over the notes, rests and stems, shaped by their length",
          "[notation][layout][fermata]") {
    // A stemmed-down B4, a stem-up G4, a rest and a high A5: each fermata is
    // centred on its notehead or rest, half a space over the staff and the stem.
    const DisplayList list =
        layoutStaff(treble({held("B4", Fermata::Normal), held("G4", Fermata::Short),
                            held("", Fermata::Long), held("A5", Fermata::Normal)}),
                    0, 0, bravura());
    const auto marks = glyphs(list, Role::Fermata);
    REQUIRE(marks.size() == 4);
    CHECK(marks[0].glyph == Glyph::FermataAbove);
    CHECK(marks[1].glyph == Glyph::FermataShortAbove);
    CHECK(marks[2].glyph == Glyph::FermataLongAbove);
    CHECK(marks[3].glyph == Glyph::FermataAbove);

    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 3);
    CHECK_THAT(centre(marks[0]), WithinAbs(centre(heads[0]), kEpsilon));
    CHECK_THAT(centre(marks[1]), WithinAbs(centre(heads[1]), kEpsilon));
    CHECK_THAT(centre(marks[3]), WithinAbs(centre(heads[2]), kEpsilon));
    const auto rests = glyphs(list, Role::Rest);
    REQUIRE(rests.size() == 1);
    CHECK_THAT(centre(marks[2]), WithinAbs(centre(rests[0]), 0.2));

    const double line = bravura().defaults().staffLineThickness / 2;
    CHECK_THAT(bottom(marks[0]), WithinAbs(-line - kGap, kEpsilon)); // over the staff
    CHECK_THAT(bottom(marks[2]), WithinAbs(-line - kGap, kEpsilon));
    CHECK(bottom(marks[1]) <= highestStem(list, centre(marks[1])) - kGap + kEpsilon); // its stem
    CHECK(bottom(marks[3]) < bottom(marks[0])); // over the ledger line
}

TEST_CASE("Fermatas: the lower voice of two holds its fermata under the staff",
          "[notation][layout][fermata]") {
    const DisplayList list = layoutStaff(treble({held("E5", Fermata::Normal, NoteValue::Whole)},
                                                {held("G4", Fermata::Normal, NoteValue::Whole)}),
                                         0, 0, bravura());
    const auto marks = glyphs(list, Role::Fermata);
    REQUIRE(marks.size() == 2);
    const auto upper = std::ranges::find(marks, Glyph::FermataAbove, &GlyphItem::glyph);
    const auto lower = std::ranges::find(marks, Glyph::FermataBelow, &GlyphItem::glyph);
    REQUIRE(upper != marks.end());
    REQUIRE(lower != marks.end());
    CHECK(bottom(*upper) < 0.0);
    CHECK(top(*lower) >= 4.0 + kGap);
}

TEST_CASE("Fermatas: over the standard notation, or over tablature shown alone",
          "[notation][layout][fermata]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    Beat open = held("", Fermata::Normal, NoteValue::Whole);
    open.notes.push_back(Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(1, 0)),
                              .position = FretPosition{.string = 1, .fret = 0}});
    song.track(0).staff(0).measure(0).voices[0].beats = {open};

    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const DisplayList score = layoutSystem(song, both, bravura());
    const auto scoreMarks = glyphs(score, Role::Fermata);
    REQUIRE(scoreMarks.size() == 1);
    const auto standard = std::ranges::find(score.staves, StaffView::Standard, &StaffBox::view);
    REQUIRE(standard != score.staves.end());
    CHECK(bottom(scoreMarks[0]) < standard->y);

    const StaffSelection alone{.view = StaffView::Tablature};
    const DisplayList tab = layoutSystem(song, std::span(&alone, 1), bravura());
    const auto tabMarks = glyphs(tab, Role::Fermata);
    REQUIRE(tabMarks.size() == 1);
    CHECK(bottom(tabMarks[0]) < tab.staves.front().y);
    const auto frets =
        std::ranges::find(tab.texts, Role::FretNumber, &elara::notation::TextItem::role);
    REQUIRE(frets != tab.texts.end());
    CHECK_THAT(centre(tabMarks[0]), WithinAbs(frets->origin.x, 0.3));
}
