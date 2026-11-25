// SPDX-License-Identifier: AGPL-3.0-or-later
// Fingerings over and under the notes, and those of a fretting hand left of
// them (ADR-0093).
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
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Finger;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::InstrumentKind;
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

// A note `text` played with `finger` and plucked with `pluck`.
Note fingered(std::string_view text, Finger finger, Finger pluck = Finger::None) {
    Note note{.pitch = *Pitch::parse(text)};
    note.effects.fingering = finger;
    note.effects.pluck = pluck;
    return note;
}

Beat chordOf(std::vector<Note> notes, NoteValue value = NoteValue::Whole) {
    return Beat{.duration = Duration(value), .notes = std::move(notes)};
}

// A piano: `right` in the first voices of the treble staff's only measure,
// `left` in the bass staff's.
Song pianoOf(const std::vector<std::vector<Beat>>& right, std::vector<Beat> left = {}) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano"}, {Staff(Clef::Treble), Staff(Clef::Bass)}));
    for (std::size_t v = 0; v < right.size(); ++v) {
        song.track(0).staff(0).measure(0).voices[v].beats = right[v];
    }
    if (!left.empty()) {
        song.track(0).staff(1).measure(0).voices[0].beats = std::move(left);
    }
    return song;
}

// A guitar playing `beat` (its notes written an octave above their sound).
Song guitarOf(Beat beat) {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar",
                                   .kind = InstrumentKind::Fretted,
                                   .midiProgram = 24,
                                   .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    song.track(0).staff(0).measure(0).voices[0].beats = {std::move(beat)};
    return song;
}

std::vector<GlyphItem> glyphsOf(const DisplayList& list, Role role) {
    std::vector<GlyphItem> found;
    std::ranges::copy_if(list.glyphs, std::back_inserter(found),
                         [role](const GlyphItem& item) { return item.role == role; });
    return found;
}

// From the top down.
std::vector<GlyphItem> fingerings(const DisplayList& list) {
    std::vector<GlyphItem> found = glyphsOf(list, Role::Fingering);
    std::ranges::stable_sort(found, {}, [](const GlyphItem& item) { return item.origin.y; });
    return found;
}

std::vector<Glyph> glyphNames(const std::vector<GlyphItem>& items) {
    std::vector<Glyph> names;
    std::ranges::transform(items, std::back_inserter(names),
                           [](const GlyphItem& item) { return item.glyph; });
    return names;
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

double centreX(const GlyphItem& item) {
    return (left(item) + right(item)) / 2;
}

double centreY(const GlyphItem& item) {
    return (top(item) + bottom(item)) / 2;
}

// The top and the bottom of the glyphs of a role.
std::pair<double, double> extentOf(const DisplayList& list, Role role) {
    double highest = 1e9;
    double lowest = -1e9;
    for (const GlyphItem& item : list.glyphs) {
        if (item.role == role) {
            highest = std::min(highest, top(item));
            lowest = std::max(lowest, bottom(item));
        }
    }
    return {highest, lowest};
}

} // namespace

TEST_CASE("Fingerings: over the right hand, one over the other in the order of the notes",
          "[notation][layout][fingering]") {
    const DisplayList list = layoutStaff(
        pianoOf({{chordOf({fingered("C4", Finger::Thumb), fingered("E4", Finger::Middle),
                           fingered("G4", Finger::Little)})}}),
        0, 0, bravura());
    const std::vector<GlyphItem> marks = fingerings(list);
    REQUIRE(marks.size() == 3);
    // Read from the top down as the notes are: G's 5 over E's 3 over C's 1.
    CHECK(glyphNames(marks) ==
          std::vector{Glyph::Fingering5, Glyph::Fingering3, Glyph::Fingering1});
    const auto [headTop, headBottom] = extentOf(list, Role::Notehead);
    const GlyphItem head = glyphsOf(list, Role::Notehead).front();
    for (std::size_t i = 0; i < marks.size(); ++i) {
        CHECK(marks[i].scale == 1.0);
        CHECK(bottom(marks[i]) < headTop);
        CHECK_THAT(centreX(marks[i]), WithinAbs(centreX(head), kEpsilon));
        if (i > 0) {
            CHECK(bottom(marks[i - 1]) < top(marks[i]));
        }
    }
}

TEST_CASE("Fingerings: under the left hand and under the lower voice of two",
          "[notation][layout][fingering]") {
    // The left hand: C3 with 5 and G3 with 1, read from the top down 1 then 5.
    const DisplayList bass = layoutStaff(
        pianoOf({{chordOf({Note{.pitch = *Pitch::parse("C5")}})}},
                {chordOf({fingered("C3", Finger::Little), fingered("G3", Finger::Thumb)})}),
        0, 1, bravura());
    const std::vector<GlyphItem> under = fingerings(bass);
    REQUIRE(under.size() == 2);
    CHECK(glyphNames(under) == std::vector{Glyph::Fingering1, Glyph::Fingering5});
    CHECK(top(under.front()) > extentOf(bass, Role::Notehead).second);

    // Two voices on the right hand: the upper's finger over it, the lower's under.
    const DisplayList voices = layoutStaff(pianoOf({{chordOf({fingered("E5", Finger::Ring)})},
                                                    {chordOf({fingered("F4", Finger::Index)})}}),
                                           0, 0, bravura());
    const std::vector<GlyphItem> marks = fingerings(voices);
    REQUIRE(marks.size() == 2);
    CHECK(glyphNames(marks) == std::vector{Glyph::Fingering4, Glyph::Fingering2});
    const auto [headTop, headBottom] = extentOf(voices, Role::Notehead);
    CHECK(bottom(marks[0]) < headTop);
    CHECK(top(marks[1]) > headBottom);
}

TEST_CASE("Fingerings: a guitar's small left of the notes, the plucking fingers over them",
          "[notation][layout][fingering]") {
    // G on the low E string, under the staff on a ledger line, fretted with the
    // ring finger and plucked with the thumb; F# on the high E string with the
    // index and i: its fingering left of its sharp.
    const Tuning tuning = Tuning::standardGuitar();
    Note low = fingered("G2", Finger::Ring, Finger::Thumb);
    low.position = FretPosition{.string = 6, .fret = 3};
    Note high = fingered("F#4", Finger::Index, Finger::Index);
    high.position = FretPosition{.string = 1, .fret = 2};
    REQUIRE(tuning.midiAt(6, 3) == low.pitch.midiNumber());
    REQUIRE(tuning.midiAt(1, 2) == high.pitch.midiNumber());
    const DisplayList list = layoutStaff(guitarOf(chordOf({low, high})), 0, 0, bravura());

    std::vector<GlyphItem> small;
    std::vector<GlyphItem> plucks;
    for (const GlyphItem& item : glyphsOf(list, Role::Fingering)) {
        (item.scale < 1.0 ? small : plucks).push_back(item);
    }
    std::ranges::stable_sort(small, {}, [](const GlyphItem& item) { return item.origin.y; });
    std::ranges::stable_sort(plucks, {}, [](const GlyphItem& item) { return item.origin.y; });
    // Fretting fingers from the index: F#'s 1 and G's 3, level with their notes.
    REQUIRE(small.size() == 2);
    CHECK(glyphNames(small) == std::vector{Glyph::Fingering1, Glyph::Fingering3});
    std::vector<GlyphItem> heads = glyphsOf(list, Role::Notehead);
    std::ranges::stable_sort(heads, {}, [](const GlyphItem& item) { return item.origin.y; });
    REQUIRE(heads.size() == 2);
    const std::vector<GlyphItem> sharps = glyphsOf(list, Role::Accidental);
    REQUIRE(sharps.size() == 1);
    for (std::size_t i = 0; i < small.size(); ++i) {
        CHECK_THAT(centreY(small[i]), WithinAbs(heads[i].origin.y, kEpsilon));
        CHECK(right(small[i]) < left(sharps.front()));
    }
    // Clear of the ledger line under the staff.
    double ledgerLeft = 1e9;
    for (const auto& line : list.lines) {
        if (line.role == Role::LedgerLine) {
            ledgerLeft = std::min({ledgerLeft, line.from.x, line.to.x});
        }
    }
    REQUIRE(ledgerLeft < 1e9);
    CHECK(right(small[1]) < ledgerLeft);
    // Plucking fingers over the notes, the higher note's on top: i over p.
    REQUIRE(plucks.size() == 2);
    CHECK(glyphNames(plucks) == std::vector{Glyph::FingeringILower, Glyph::FingeringPLower});
    CHECK(bottom(plucks[1]) < top(heads.front()));
}

TEST_CASE("Fingerings: tablature draws none", "[notation][layout][fingering]") {
    Note note = fingered("A2", Finger::Middle, Finger::Middle);
    note.position = FretPosition{.string = 5, .fret = 0};
    const Song song = guitarOf(chordOf({note}));
    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    CHECK(glyphsOf(layoutSystem(song, both, bravura()), Role::Fingering).size() == 2);
    const std::array<StaffSelection, 1> tab{StaffSelection{.view = StaffView::Tablature}};
    CHECK(glyphsOf(layoutSystem(song, tab, bravura()), Role::Fingering).empty());
}
