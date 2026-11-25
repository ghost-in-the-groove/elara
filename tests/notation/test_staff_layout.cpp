// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuplet;
using elara::notation::BeamItem;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutStaff;
using elara::notation::LineItem;
using elara::notation::PolygonItem;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;

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

Pitch pitch(std::string_view text) {
    const auto parsed = Pitch::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

Beat chord(std::initializer_list<std::string_view> pitches, NoteValue value = NoteValue::Quarter,
           int dots = 0, elara::core::Tuplet tuplet = {}) {
    Beat beat{.duration = Duration(value, dots, tuplet)};
    for (const std::string_view text : pitches) {
        beat.notes.push_back(Note{.pitch = pitch(text)});
    }
    return beat;
}

Beat note(std::string_view text, NoteValue value = NoteValue::Quarter, int dots = 0) {
    return chord({text}, value, dots);
}

Beat rest(NoteValue value, int dots = 0) {
    return Beat{.duration = Duration(value, dots)};
}

Beat drum(std::initializer_list<int> keys, NoteValue value = NoteValue::Quarter) {
    Beat beat{.duration = Duration(value)};
    for (const int key : keys) {
        beat.notes.push_back(Note{.pitch = Pitch::fromMidi(key)});
    }
    return beat;
}

struct Part {
    Clef clef = Clef::Treble;
    TimeSignature time{};
    KeySignature key{};
    int writtenOffset = 0;
    InstrumentKind kind = InstrumentKind::Pitched;
};

// A song with one staff; measures[m][v] holds the beats of voice v in measure m.
Song songOf(const std::vector<std::vector<std::vector<Beat>>>& measures, const Part& part = {}) {
    Song song;
    song.appendMeasures(measures.size());
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.measureHeader(m).timeSignature = part.time;
        song.measureHeader(m).keySignature = part.key;
    }
    song.addTrack(Track(
        "Part", Instrument{.name = "Part", .kind = part.kind, .writtenOffset = part.writtenOffset},
        {Staff(part.clef)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        for (std::size_t v = 0; v < measures[m].size(); ++v) {
            song.track(0).staff(0).measure(m).voices.at(v).beats = measures[m][v];
        }
    }
    return song;
}

// One measure, one voice.
Song melody(const std::vector<Beat>& beats, const Part& part = {}) {
    return songOf({{beats}}, part);
}

DisplayList layout(const Song& song, const StaffLayoutOptions& options = {}) {
    return layoutStaff(song, 0, 0, bravura(), options);
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

std::vector<LineItem> lines(const DisplayList& list, Role role) {
    std::vector<LineItem> found;
    for (const LineItem& item : list.lines) {
        if (item.role == role) {
            found.push_back(item);
        }
    }
    std::ranges::stable_sort(found, {}, [](const LineItem& item) { return item.from.x; });
    return found;
}

std::vector<double> ysOf(const std::vector<GlyphItem>& items) {
    std::vector<double> ys;
    for (const GlyphItem& item : items) {
        ys.push_back(item.origin.y);
    }
    return ys;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x;
}

} // namespace

TEST_CASE("Staff layout: five staff lines, a clef and a final barline", "[notation][layout]") {
    const DisplayList list = layout(melody({}));

    const auto staffLines = lines(list, Role::StaffLine);
    REQUIRE(staffLines.size() == 5);
    std::vector<double> ys;
    for (const LineItem& line : staffLines) {
        ys.push_back(line.from.y);
        CHECK(line.to.y == line.from.y);
        CHECK(line.from.x == 0.0);
        CHECK_THAT(line.to.x, WithinAbs(list.width, kEpsilon));
        CHECK(line.thickness == bravura().defaults().staffLineThickness);
    }
    std::ranges::sort(ys);
    CHECK(ys == std::vector<double>{0, 1, 2, 3, 4});

    const auto clefs = glyphs(list, Role::Clef);
    REQUIRE(clefs.size() == 1);
    CHECK(clefs[0].glyph == Glyph::GClef);
    CHECK(clefs[0].origin.y == 3.0); // the G line

    // An empty measure shows a whole rest hanging from the fourth line.
    const auto rests = glyphs(list, Role::Rest);
    REQUIRE(rests.size() == 1);
    CHECK(rests[0].glyph == Glyph::RestWhole);
    CHECK(rests[0].origin.y == 1.0);

    // The song ends with a thin and a thick barline; the thick one ends the line.
    const auto barlines = lines(list, Role::Barline);
    REQUIRE(barlines.size() == 2);
    CHECK(barlines[0].thickness == bravura().defaults().thinBarlineThickness);
    CHECK(barlines[1].thickness == bravura().defaults().thickBarlineThickness);
    CHECK_THAT(barlines[1].from.x + barlines[1].thickness / 2, WithinAbs(list.width, kEpsilon));
    CHECK(barlines[0].from.y == 0.0);
    CHECK(barlines[0].to.y == 4.0);

    REQUIRE(list.measures.size() == 1);
    CHECK(list.measures[0].index == 0);
    CHECK(list.measures[0].x > right(clefs[0]));
}

TEST_CASE("Staff layout: clefs are drawn on their line", "[notation][layout]") {
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Bass})), Role::Clef)[0].glyph == Glyph::FClef);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Bass})), Role::Clef)[0].origin.y == 1.0);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Treble8vb})), Role::Clef)[0].glyph ==
          Glyph::GClef8vb);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Bass8vb})), Role::Clef)[0].glyph ==
          Glyph::FClef8vb);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Alto})), Role::Clef)[0].origin.y == 2.0);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Tenor})), Role::Clef)[0].origin.y == 1.0);
    CHECK(glyphs(layout(melody({}, {.clef = Clef::Percussion})), Role::Clef)[0].glyph ==
          Glyph::PercussionClef);
}

TEST_CASE("Staff layout: notes sit on the line or space of their pitch", "[notation][layout]") {
    SECTION("treble clef") {
        const DisplayList list = layout(melody({note("E4"), note("F5"), note("C4"), note("C6")}));
        CHECK(ysOf(glyphs(list, Role::Notehead)) == std::vector<double>{4.0, 0.0, 5.0, -2.0});
        // Middle C has one ledger line through it, C6 two above the staff.
        std::vector<double> ledgers;
        for (const LineItem& line : lines(list, Role::LedgerLine)) {
            ledgers.push_back(line.from.y);
            CHECK(line.thickness == bravura().defaults().legerLineThickness);
        }
        CHECK(ledgers == std::vector<double>{5.0, -1.0, -2.0});
    }
    SECTION("bass clef") {
        const DisplayList list = layout(melody({note("A3"), note("G2")}, {.clef = Clef::Bass}));
        CHECK(ysOf(glyphs(list, Role::Notehead)) == std::vector<double>{0.0, 4.0});
    }
    SECTION("alto and tenor clefs") {
        CHECK(ysOf(glyphs(layout(melody({note("C4")}, {.clef = Clef::Alto})), Role::Notehead)) ==
              std::vector<double>{2.0});
        CHECK(ysOf(glyphs(layout(melody({note("C4")}, {.clef = Clef::Tenor})), Role::Notehead)) ==
              std::vector<double>{1.0});
    }
    SECTION("a guitar is written an octave above the sound") {
        // The open low E string sounds E2 and is written E3, three ledger lines
        // below the treble staff.
        const DisplayList list =
            layout(melody({note("E2")}, {.clef = Clef::Treble8vb, .writtenOffset = 12}));
        CHECK(ysOf(glyphs(list, Role::Notehead)) == std::vector<double>{7.5});
        CHECK(lines(list, Role::LedgerLine).size() == 3);
    }
    SECTION("a B-flat trumpet is written a major second above the sound") {
        // Concert B-flat is written C; concert F major is written G major.
        const DisplayList list = layout(
            melody({note("Bb4")}, {.key = KeySignature(-1, KeyMode::Major), .writtenOffset = 2}));
        CHECK(ysOf(glyphs(list, Role::Notehead)) == std::vector<double>{1.5});
        const auto key = glyphs(list, Role::KeySignature);
        REQUIRE(key.size() == 1);
        CHECK(key[0].glyph == Glyph::AccidentalSharp);
        CHECK(glyphs(list, Role::Accidental).empty());
    }
    SECTION("note values choose the notehead") {
        const DisplayList list =
            layout(melody({note("G4", NoteValue::Whole), note("G4", NoteValue::Half),
                           note("G4", NoteValue::Quarter), note("G4", NoteValue::Eighth)},
                          {.time = TimeSignature(15, 8)}));
        std::vector<Glyph> heads;
        for (const GlyphItem& item : glyphs(list, Role::Notehead)) {
            heads.push_back(item.glyph);
        }
        CHECK(heads == std::vector<Glyph>{Glyph::NoteheadWhole, Glyph::NoteheadHalf,
                                          Glyph::NoteheadBlack, Glyph::NoteheadBlack});
    }
}

TEST_CASE("Staff layout: stems point away from the middle line", "[notation][layout]") {
    const auto& head = bravura().metrics(Glyph::NoteheadBlack);
    const double stemWidth = bravura().defaults().stemThickness;

    SECTION("below the middle line the stem goes up from the right of the notehead") {
        const DisplayList list = layout(melody({note("G4")}));
        const auto heads = glyphs(list, Role::Notehead);
        const auto stems = lines(list, Role::Stem);
        REQUIRE(stems.size() == 1);
        const auto attach = *head.anchor(elara::notation::Anchor::StemUpSE);
        CHECK_THAT(stems[0].from.x,
                   WithinAbs(heads[0].origin.x + attach.x - stemWidth / 2, kEpsilon));
        CHECK(stems[0].from.x == stems[0].to.x);
        CHECK(stems[0].thickness == stemWidth);
        CHECK_THAT(std::max(stems[0].from.y, stems[0].to.y), WithinAbs(3.0 - attach.y, kEpsilon));
        CHECK_THAT(std::min(stems[0].from.y, stems[0].to.y), WithinAbs(3.0 - 3.5, kEpsilon));
    }
    SECTION("on or above the middle line the stem goes down from the left") {
        for (const std::string_view text : {"B4", "D5"}) {
            const DisplayList list = layout(melody({note(text)}));
            const auto heads = glyphs(list, Role::Notehead);
            const auto stems = lines(list, Role::Stem);
            REQUIRE(stems.size() == 1);
            const auto attach = *head.anchor(elara::notation::Anchor::StemDownNW);
            CHECK_THAT(stems[0].from.x,
                       WithinAbs(heads[0].origin.x + attach.x + stemWidth / 2, kEpsilon));
            CHECK_THAT(std::min(stems[0].from.y, stems[0].to.y),
                       WithinAbs(heads[0].origin.y - attach.y, kEpsilon));
            CHECK_THAT(std::max(stems[0].from.y, stems[0].to.y),
                       WithinAbs(heads[0].origin.y + 3.5, kEpsilon));
        }
    }
    SECTION("notes far from the staff get stems that reach the middle line") {
        const DisplayList list = layout(melody({note("A3")})); // two ledger lines below
        const auto stems = lines(list, Role::Stem);
        REQUIRE(stems.size() == 1);
        CHECK(std::min(stems[0].from.y, stems[0].to.y) == 2.0);
    }
    SECTION("whole notes have no stem") {
        CHECK(lines(layout(melody({note("G4", NoteValue::Whole)})), Role::Stem).empty());
    }
}

TEST_CASE("Staff layout: accidentals are written when the key or the measure needs them",
          "[notation][layout]") {
    const Part gMajor{.key = KeySignature(1, KeyMode::Major)};
    const DisplayList list = layout(
        songOf({{{note("F#4"), note("F4"), note("F4"), note("C#5")}}, {{note("F4")}}}, gMajor));

    const auto key = glyphs(list, Role::KeySignature);
    REQUIRE(key.size() == 1); // only at the start of the line
    CHECK(key[0].glyph == Glyph::AccidentalSharp);
    CHECK(key[0].origin.y == 0.0); // F5, the top line

    // F#4: in the key. F4: natural. F4 again: still natural in this measure.
    // C#5: sharp. F4 in the next measure: natural again.
    const auto accidentals = glyphs(list, Role::Accidental);
    std::vector<Glyph> written;
    for (const GlyphItem& item : accidentals) {
        written.push_back(item.glyph);
    }
    CHECK(written == std::vector<Glyph>{Glyph::AccidentalNatural, Glyph::AccidentalSharp,
                                        Glyph::AccidentalNatural});
    CHECK(ysOf(accidentals) == std::vector<double>{3.5, 1.5, 3.5});

    // Each accidental sits left of its notehead.
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 5);
    CHECK(right(accidentals[0]) < left(heads[1]));
    CHECK(right(accidentals[0]) > right(heads[0]));
    CHECK(right(accidentals[1]) < left(heads[3]));
    CHECK(right(accidentals[2]) < left(heads[4]));
}

TEST_CASE("Staff layout: key signatures in every clef", "[notation][layout]") {
    const auto keyYs = [](Clef clef, int fifths) {
        return ysOf(
            glyphs(layout(melody({}, {.clef = clef, .key = KeySignature(fifths, KeyMode::Major)})),
                   Role::KeySignature));
    };
    // Sharps F C G D A E B, flats B E A D G C F, in their usual places.
    CHECK(keyYs(Clef::Treble, 7) == std::vector<double>{0.0, 1.5, -0.5, 1.0, 2.5, 0.5, 2.0});
    CHECK(keyYs(Clef::Treble, -7) == std::vector<double>{2.0, 0.5, 2.5, 1.0, 3.0, 1.5, 3.5});
    CHECK(keyYs(Clef::Bass, 2) == std::vector<double>{1.0, 2.5});
    CHECK(keyYs(Clef::Bass, -2) == std::vector<double>{3.0, 1.5});
    CHECK(keyYs(Clef::Percussion, 3).empty());
}

TEST_CASE("Staff layout: time signatures", "[notation][layout]") {
    const auto digits = [](TimeSignature time) {
        std::vector<std::pair<Glyph, double>> found;
        for (const GlyphItem& item :
             glyphs(layout(melody({}, {.time = time})), Role::TimeSignature)) {
            found.emplace_back(item.glyph, item.origin.y);
        }
        // Numerator first, then denominator, each left to right.
        std::ranges::stable_sort(found, {}, [](const auto& digit) { return digit.second; });
        return found;
    };
    CHECK(digits(TimeSignature(3, 4)) ==
          std::vector<std::pair<Glyph, double>>{{Glyph::TimeSig3, 1.0}, {Glyph::TimeSig4, 3.0}});
    const auto twelveEight = digits(TimeSignature(12, 8));
    REQUIRE(twelveEight.size() == 3);
    CHECK(twelveEight == std::vector<std::pair<Glyph, double>>{{Glyph::TimeSig1, 1.0},
                                                               {Glyph::TimeSig2, 1.0},
                                                               {Glyph::TimeSig8, 3.0}});

    SECTION("a change is written where it happens, not repeated") {
        Song song = songOf({{{}}, {{}}, {{}}});
        song.measureHeader(2).timeSignature = TimeSignature(3, 4);
        std::vector<std::size_t> measures;
        for (const GlyphItem& item : glyphs(layout(song), Role::TimeSignature)) {
            measures.push_back(item.measure);
        }
        CHECK(measures == std::vector<std::size_t>{0, 0, 2, 2});
    }
}

TEST_CASE("Staff layout: barlines and measure boxes", "[notation][layout]") {
    const DisplayList list = layout(songOf({{{note("G4", NoteValue::Whole)}},
                                            {{note("A4", NoteValue::Whole)}},
                                            {{note("B4", NoteValue::Whole)}}}));
    CHECK(lines(list, Role::Barline).size() == 4); // two thin, then thin and thick
    REQUIRE(list.measures.size() == 3);
    for (std::size_t m = 0; m < 3; ++m) {
        CHECK(list.measures[m].index == m);
        CHECK(list.measures[m].width > 0.0);
    }
    CHECK(list.measures[1].x > list.measures[0].x + list.measures[0].width);
    CHECK(list.measures[2].x > list.measures[1].x + list.measures[1].width);

    SECTION("a range of measures") {
        const DisplayList middle = layout(songOf({{{note("G4", NoteValue::Whole)}},
                                                  {{note("A4", NoteValue::Whole)}},
                                                  {{note("B4", NoteValue::Whole)}}}),
                                          {.firstMeasure = 1, .measureCount = 1});
        REQUIRE(middle.measures.size() == 1);
        CHECK(middle.measures[0].index == 1);
        CHECK(glyphs(middle, Role::Clef).size() == 1);
        CHECK(glyphs(middle, Role::TimeSignature).empty()); // no change at measure 2
        CHECK(lines(middle, Role::Barline).size() == 1);    // not the end of the song
        CHECK(glyphs(middle, Role::Notehead)[0].measure == 1);
    }
    SECTION("the columns of notes, where the playback cursor stops") {
        const DisplayList quarters =
            layout(melody({note("G4", NoteValue::Half), note("A4"), note("B4")}));
        REQUIRE(quarters.measures.size() == 1);
        const auto& box = quarters.measures[0];
        const auto heads = glyphs(quarters, Role::Notehead);
        REQUIRE(box.onsets.size() == 3);
        REQUIRE(heads.size() == 3);
        const std::array<double, 3> at{0.0, 0.5, 0.75};
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(box.onsets[i].at, WithinAbs(at[i], kEpsilon));
            CHECK_THAT(box.onsets[i].x, WithinAbs(heads[i].origin.x, kEpsilon));
        }
        CHECK(box.onsets.front().x > box.x);
        CHECK(box.onsets.back().x < box.x + box.width);
    }
    SECTION("missing parts throw") {
        const Song song = melody({});
        CHECK_THROWS_AS(layoutStaff(song, 1, 0, bravura()), std::out_of_range);
        CHECK_THROWS_AS(layoutStaff(song, 0, 1, bravura()), std::out_of_range);
        CHECK_THROWS_AS(layout(song, {.firstMeasure = 1}), std::out_of_range);
    }
}

TEST_CASE("Staff layout: eighths and shorter are beamed by beat", "[notation][layout]") {
    const auto eighths = [](int count) {
        return std::vector<Beat>(static_cast<std::size_t>(count), note("G4", NoteValue::Eighth));
    };
    SECTION("4/4: one beam per quarter") {
        const DisplayList list = layout(melody(eighths(8)));
        CHECK(list.beams.size() == 4);
        CHECK(glyphs(list, Role::Flag).empty());
        CHECK(lines(list, Role::Stem).size() == 8);
    }
    SECTION("6/8: one beam per dotted quarter") {
        const DisplayList list = layout(melody(eighths(6), {.time = TimeSignature(6, 8)}));
        CHECK(list.beams.size() == 2);
    }
    SECTION("a lone eighth gets a flag") {
        const DisplayList list =
            layout(melody({note("G4", NoteValue::Eighth), note("G4"), note("D5", NoteValue::Eighth),
                           note("G4", NoteValue::Half)},
                          {.time = TimeSignature(5, 4)}));
        CHECK(list.beams.empty());
        const auto flags = glyphs(list, Role::Flag);
        REQUIRE(flags.size() == 2);
        CHECK(flags[0].glyph == Glyph::Flag8thUp);
        CHECK(flags[1].glyph == Glyph::Flag8thDown);
        // The flag hangs from the end of its stem.
        const auto stems = lines(list, Role::Stem);
        const double tip = std::min(stems[0].from.y, stems[0].to.y);
        const auto anchor =
            *bravura().metrics(Glyph::Flag8thUp).anchor(elara::notation::Anchor::StemUpNW);
        CHECK_THAT(flags[0].origin.y - anchor.y, WithinAbs(tip, kEpsilon));
    }
    SECTION("rests break beams") {
        const DisplayList list = layout(melody(
            {note("G4", NoteValue::Eighth), rest(NoteValue::Eighth), note("G4", NoteValue::Eighth),
             note("G4", NoteValue::Eighth), rest(NoteValue::Half)}));
        CHECK(list.beams.size() == 1);
        CHECK(glyphs(list, Role::Flag).size() == 1);
    }
    SECTION("triplets are beamed together") {
        const Tuplet triplet(3, 2);
        const DisplayList list = layout(melody({chord({"G4"}, NoteValue::Eighth, 0, triplet),
                                                chord({"A4"}, NoteValue::Eighth, 0, triplet),
                                                chord({"B4"}, NoteValue::Eighth, 0, triplet),
                                                note("G4", NoteValue::Half), note("G4")}));
        CHECK(list.beams.size() == 1);
    }
}

TEST_CASE("Staff layout: beams meet every stem of their group", "[notation][layout]") {
    const DisplayList list =
        layout(melody({note("G4", NoteValue::Sixteenth), note("A4", NoteValue::Sixteenth),
                       note("B4", NoteValue::Sixteenth), note("C5", NoteValue::Sixteenth),
                       note("E4", NoteValue::Eighth, 1), note("F4", NoteValue::Sixteenth),
                       rest(NoteValue::Half)}));
    // Four sixteenths: primary and secondary beam. Dotted eighth and sixteenth:
    // primary beam and a short secondary beam on the sixteenth.
    REQUIRE(list.beams.size() == 4);
    const double thickness = bravura().defaults().beamThickness;
    for (const BeamItem& beam : list.beams) {
        CHECK(beam.thickness == thickness);
        CHECK(beam.right.x > beam.left.x);
    }
    const auto heads = glyphs(list, Role::Notehead);
    const auto stems = lines(list, Role::Stem);
    REQUIRE(stems.size() == 6);
    // All the stems go up (the notes lie mostly below the middle line) and end
    // on the outer edge of the primary beam, at least 3.5 spaces from the note.
    for (std::size_t i = 0; i < stems.size(); ++i) {
        const double tip = std::min(stems[i].from.y, stems[i].to.y);
        CHECK(heads[i].origin.y - tip >= 3.5 - kEpsilon);
        const auto onBeam = std::ranges::any_of(list.beams, [&](const BeamItem& beam) {
            if (stems[i].from.x < beam.left.x - kEpsilon ||
                stems[i].from.x > beam.right.x + kEpsilon) {
                return false;
            }
            const double t = (stems[i].from.x - beam.left.x) / (beam.right.x - beam.left.x);
            const double centre = beam.left.y + t * (beam.right.y - beam.left.y);
            return std::abs(centre - thickness / 2 - tip) < 1e-6;
        });
        CHECK(onBeam);
    }
    // The beamlet is shorter than the gap between the two stems it would join.
    const auto shortest = std::ranges::min(
        list.beams, {}, [](const BeamItem& beam) { return beam.right.x - beam.left.x; });
    CHECK(shortest.right.x - shortest.left.x < stems[5].from.x - stems[4].from.x);
    CHECK_THAT(shortest.right.x, WithinAbs(stems[5].from.x + stems[5].thickness / 2, kEpsilon));
}

TEST_CASE("Staff layout: tuplets show their number", "[notation][layout]") {
    const Tuplet triplet(3, 2);
    const auto group = [](std::initializer_list<std::string_view> pitches, NoteValue value,
                          Tuplet tuplet) {
        std::vector<Beat> beats;
        for (const std::string_view text : pitches) {
            beats.push_back(chord({text}, value, 0, tuplet));
        }
        return beats;
    };
    const auto then = [](std::vector<Beat> beats, const std::vector<Beat>& more) {
        beats.insert(beats.end(), more.begin(), more.end());
        return beats;
    };
    const auto top = [](const GlyphItem& item) {
        return item.origin.y - bravura().metrics(item.glyph).box.northEast.y;
    };
    const auto bottom = [](const GlyphItem& item) {
        return item.origin.y - bravura().metrics(item.glyph).box.southWest.y;
    };
    const auto centreX = [](const std::vector<GlyphItem>& digits) {
        return (left(digits.front()) + right(digits.back())) / 2;
    };
    const auto tip = [](const LineItem& stem) {
        return std::min(stem.from.y, stem.to.y);
    };
    // The y of a beam's edge at x: its top edge, or its bottom one.
    const auto beamEdge = [](const BeamItem& beam, double x, bool upper) {
        const double t = (x - beam.left.x) / (beam.right.x - beam.left.x);
        return beam.left.y + t * (beam.right.y - beam.left.y) +
               (upper ? -beam.thickness / 2 : beam.thickness / 2);
    };
    const std::vector<Beat> rests{note("G4", NoteValue::Half), note("G4")};

    SECTION("a beamed triplet: the number over the beam, no bracket") {
        const DisplayList list =
            layout(melody(then(group({"G4", "A4", "B4"}, NoteValue::Eighth, triplet), rests)));
        const auto numbers = glyphs(list, Role::Tuplet);
        REQUIRE(numbers.size() == 1);
        CHECK(numbers[0].glyph == Glyph::Tuplet3);
        CHECK(lines(list, Role::Tuplet).empty());
        const auto stems = lines(list, Role::Stem);
        REQUIRE(list.beams.size() == 1);
        const double middle = (stems[0].from.x + stems[2].from.x) / 2;
        CHECK_THAT(centreX(numbers), WithinAbs(middle, kEpsilon));
        const double beam = beamEdge(list.beams[0], middle, true);
        CHECK(bottom(numbers[0]) < beam - 0.25);
        CHECK(bottom(numbers[0]) > beam - 1.0);
        CHECK(list.top <= top(numbers[0]) + kEpsilon);
    }
    SECTION("stems down: the number under the beam") {
        const DisplayList list =
            layout(melody(then(group({"D5", "E5", "F5"}, NoteValue::Eighth, triplet), rests)));
        const auto numbers = glyphs(list, Role::Tuplet);
        REQUIRE(numbers.size() == 1);
        REQUIRE(list.beams.size() == 1);
        const double beam = beamEdge(list.beams[0], centreX(numbers), false);
        CHECK(top(numbers[0]) > beam + 0.25);
        CHECK(top(numbers[0]) < beam + 1.0);
        CHECK(list.bottom >= bottom(numbers[0]) - kEpsilon);
    }
    SECTION("unbeamed notes: a bracket with the number in a gap") {
        const DisplayList list =
            layout(melody(then(group({"G4", "A4", "B4"}, NoteValue::Quarter, triplet),
                               {note("G4", NoteValue::Half)})));
        CHECK(list.beams.empty());
        const auto numbers = glyphs(list, Role::Tuplet);
        REQUIRE(numbers.size() == 1);
        const auto bracket = lines(list, Role::Tuplet);
        REQUIRE(bracket.size() == 4);
        // Left hook, left half, right half, right hook (sorted by their left end).
        const double y = bracket[1].from.y;
        CHECK(bracket[1].to.y == y);
        CHECK(bracket[2].from.y == y);
        CHECK(bracket[2].to.y == y);
        CHECK(bracket[0].from.x == bracket[0].to.x);
        CHECK(bracket[3].from.x == bracket[3].to.x);
        CHECK(std::max(bracket[0].from.y, bracket[0].to.y) > y); // hooks point to the notes
        CHECK(std::max(bracket[3].from.y, bracket[3].to.y) > y);
        // Above every stem, the number in its gap and clear of the stems too.
        const auto stems = lines(list, Role::Stem);
        double highest = 0.0;
        for (const LineItem& stem : stems) {
            CHECK(tip(stem) > y);
            highest = std::min(highest, tip(stem));
        }
        CHECK(bottom(numbers[0]) < highest);
        CHECK(std::max(bracket[1].from.x, bracket[1].to.x) < left(numbers[0]));
        CHECK(std::min(bracket[2].from.x, bracket[2].to.x) > right(numbers[0]));
        const auto heads = glyphs(list, Role::Notehead);
        CHECK(bracket[0].from.x <= left(heads[0]) + kEpsilon);
        CHECK(bracket[3].from.x >= right(heads[2]) - kEpsilon);
        CHECK(bracket[3].from.x < left(heads[3]));
    }
    SECTION("a rest at the end of a beamed triplet asks for a bracket") {
        auto beats = group({"G4", "A4"}, NoteValue::Eighth, triplet);
        beats.push_back(Beat{.duration = Duration(NoteValue::Eighth, 0, triplet)});
        const DisplayList list = layout(melody(then(beats, rests)));
        CHECK(list.beams.size() == 1);
        CHECK(glyphs(list, Role::Tuplet).size() == 1);
        CHECK(lines(list, Role::Tuplet).size() == 4);
    }
    SECTION("a group ends when its notes fill the time of its normal count") {
        const auto numbersOf = [](const DisplayList& list) {
            std::vector<Glyph> found;
            for (const GlyphItem& item : glyphs(list, Role::Tuplet)) {
                found.push_back(item.glyph);
            }
            return found;
        };
        const std::initializer_list<std::string_view> six{"G4", "A4", "B4", "C5", "B4", "A4"};
        // Two triplets of sixteenths under one beam: a number each, no bracket.
        const DisplayList triplets =
            layout(melody(then(group(six, NoteValue::Sixteenth, triplet), rests)));
        CHECK(triplets.beams.size() == 2); // primary and secondary
        CHECK(numbersOf(triplets) == std::vector<Glyph>{Glyph::Tuplet3, Glyph::Tuplet3});
        CHECK(lines(triplets, Role::Tuplet).empty());
        // One sextuplet.
        const DisplayList sextuplet =
            layout(melody(then(group(six, NoteValue::Sixteenth, Tuplet(6, 4)), rests)));
        CHECK(numbersOf(sextuplet) == std::vector<Glyph>{Glyph::Tuplet6});
        // A quarter and an eighth, then three eighths: two triplets.
        const DisplayList two =
            layout(melody(then(then(group({"G4"}, NoteValue::Quarter, triplet),
                                    group({"A4", "B4", "C5", "D5"}, NoteValue::Eighth, triplet)),
                               {note("G4", NoteValue::Half)})));
        CHECK(numbersOf(two) == std::vector<Glyph>{Glyph::Tuplet3, Glyph::Tuplet3});
        // Ten thirty-seconds in the time of eight: both digits, side by side.
        const DisplayList ten =
            layout(melody(then(group({"G4", "A4", "B4", "C5", "D5", "C5", "B4", "A4", "G4", "A4"},
                                     NoteValue::ThirtySecond, Tuplet(10, 8)),
                               rests)));
        const auto digits = glyphs(ten, Role::Tuplet);
        REQUIRE(digits.size() == 2);
        CHECK(digits[0].glyph == Glyph::Tuplet1);
        CHECK(digits[1].glyph == Glyph::Tuplet0);
        CHECK_THAT(left(digits[1]), WithinAbs(right(digits[0]), 0.05));
        CHECK_THAT(digits[0].origin.y, WithinAbs(digits[1].origin.y, kEpsilon));
    }
    SECTION("no number without tuplets") {
        const DisplayList list =
            layout(melody(std::vector<Beat>(8, note("G4", NoteValue::Eighth))));
        CHECK(glyphs(list, Role::Tuplet).empty());
        CHECK(lines(list, Role::Tuplet).empty());
    }
}

TEST_CASE("Staff layout: rests", "[notation][layout]") {
    const DisplayList list =
        layout(melody({rest(NoteValue::Half), rest(NoteValue::Quarter), rest(NoteValue::Eighth),
                       rest(NoteValue::Sixteenth), rest(NoteValue::Sixteenth)}));
    std::vector<Glyph> written;
    for (const GlyphItem& item : glyphs(list, Role::Rest)) {
        written.push_back(item.glyph);
        CHECK(item.origin.y == 2.0); // on the middle line
    }
    CHECK(written == std::vector<Glyph>{Glyph::RestHalf, Glyph::RestQuarter, Glyph::Rest8th,
                                        Glyph::Rest16th, Glyph::Rest16th});

    SECTION("a rest filling the measure is a centred whole rest") {
        const DisplayList full =
            layout(melody({rest(NoteValue::Half, 1)}, {.time = TimeSignature(3, 4)}));
        const auto rests = glyphs(full, Role::Rest);
        REQUIRE(rests.size() == 1);
        CHECK(rests[0].glyph == Glyph::RestWhole);
        const double middle = full.measures[0].x + full.measures[0].width / 2;
        CHECK_THAT((left(rests[0]) + right(rests[0])) / 2, WithinAbs(middle, kEpsilon));
        CHECK(glyphs(full, Role::Dot).empty());
    }
}

TEST_CASE("Staff layout: dots sit in a space right of the notehead", "[notation][layout]") {
    const DisplayList list =
        layout(melody({note("G4", NoteValue::Quarter, 1), note("A4", NoteValue::Quarter, 2),
                       note("G4", NoteValue::Eighth)},
                      {.time = TimeSignature(4, 4)}));
    const auto dots = glyphs(list, Role::Dot);
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(dots.size() == 3);
    CHECK(dots[0].origin.y == 2.5); // G4 is on a line: the dot moves to the space above
    CHECK(dots[1].origin.y == 2.5); // A4 is in that space
    CHECK(dots[2].origin.y == 2.5);
    CHECK(left(dots[0]) > right(heads[0]));
    CHECK(left(dots[1]) > right(heads[1]));
    CHECK(left(dots[2]) > right(dots[1]));
    CHECK(right(dots[2]) < left(heads[2]));
}

TEST_CASE("Staff layout: spacing grows with duration and nothing overlaps", "[notation][layout]") {
    const DisplayList list =
        layout(melody({note("G4", NoteValue::Half), note("G4"), note("G4", NoteValue::Eighth),
                       note("G4", NoteValue::Eighth)}));
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 4);
    const double half = heads[1].origin.x - heads[0].origin.x;
    const double quarter = heads[2].origin.x - heads[1].origin.x;
    const double eighth = heads[3].origin.x - heads[2].origin.x;
    CHECK(half > quarter);
    CHECK(quarter > eighth);
    CHECK(eighth > bravura().metrics(Glyph::NoteheadBlack).box.width());

    SECTION("accidentals push their note to the right") {
        const DisplayList sharps =
            layout(melody({note("G#4", NoteValue::Sixteenth), note("A#4", NoteValue::Sixteenth),
                           note("Bb4", NoteValue::Sixteenth), note("C#5", NoteValue::Sixteenth),
                           rest(NoteValue::Quarter), rest(NoteValue::Half)}));
        const auto sharpHeads = glyphs(sharps, Role::Notehead);
        const auto accidentals = glyphs(sharps, Role::Accidental);
        REQUIRE(accidentals.size() == 4);
        for (std::size_t i = 1; i < 4; ++i) {
            CHECK(left(accidentals[i]) > right(sharpHeads[i - 1]));
        }
    }
}

TEST_CASE("Staff layout: chords share one stem; seconds sit either side", "[notation][layout]") {
    const DisplayList list = layout(melody({chord({"G4", "A4", "C5"}), chord({"E5", "F5"})}));
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 5);
    CHECK(lines(list, Role::Stem).size() == 2);

    // G4 A4 C5, stem up (G4 is farther from the middle line): A4 moves right of
    // the stem.
    std::vector<GlyphItem> first(heads.begin(), heads.begin() + 3);
    const auto at = [&](double y) {
        return *std::ranges::find_if(heads, [&](const GlyphItem& h) { return h.origin.y == y; });
    };
    CHECK(at(2.5).origin.x > at(3.0).origin.x);  // A4 right of G4
    CHECK(at(1.5).origin.x == at(3.0).origin.x); // C5 with G4
    // E5 F5, stem down: E5 moves left of the stem.
    CHECK(at(0.5).origin.x < at(0.0).origin.x);
}

TEST_CASE("Staff layout: with two voices the first points up, the second down",
          "[notation][layout]") {
    const DisplayList list =
        layout(songOf({{{note("D5", NoteValue::Half), note("D5", NoteValue::Half)},
                        {note("G4", NoteValue::Whole)}}}));
    const auto stems = lines(list, Role::Stem);
    REQUIRE(stems.size() == 2); // the whole note has none
    const auto heads = glyphs(list, Role::Notehead);
    for (const LineItem& stem : stems) {
        // D5 is above the middle line, yet its stem goes up.
        CHECK(std::min(stem.from.y, stem.to.y) < 1.0 - 3.0);
    }

    SECTION("rests of the voices move apart") {
        const DisplayList rests =
            layout(songOf({{{rest(NoteValue::Half), note("D5", NoteValue::Half)},
                            {note("G4", NoteValue::Half), rest(NoteValue::Half)}}}));
        const auto found = glyphs(rests, Role::Rest);
        REQUIRE(found.size() == 2);
        CHECK(found[0].origin.y < 2.0);
        CHECK(found[1].origin.y > 2.0);
    }
}

TEST_CASE("Staff layout: drums follow the drum map", "[notation][layout]") {
    const Part kit{.clef = Clef::Percussion,
                   .key = KeySignature(2, KeyMode::Major),
                   .kind = InstrumentKind::Percussion};
    const DisplayList list =
        layout(melody({drum({36, 42}), drum({38, 42}), drum({49}), drum({44})}, kit));
    std::vector<std::pair<Glyph, double>> heads;
    for (const GlyphItem& item : glyphs(list, Role::Notehead)) {
        heads.emplace_back(item.glyph, item.origin.y);
    }
    std::ranges::sort(heads, {}, [](const auto& head) { return head.second; });
    // Crash on the ledger line above, hi-hat above the staff, snare in the third
    // space, bass drum in the first, hi-hat pedal below the staff.
    CHECK(heads == std::vector<std::pair<Glyph, double>>{{Glyph::NoteheadXBlack, -1.0},
                                                         {Glyph::NoteheadXBlack, -0.5},
                                                         {Glyph::NoteheadXBlack, -0.5},
                                                         {Glyph::NoteheadBlack, 1.5},
                                                         {Glyph::NoteheadBlack, 3.5},
                                                         {Glyph::NoteheadXBlack, 4.5}});
    CHECK(glyphs(list, Role::KeySignature).empty());
    CHECK(glyphs(list, Role::Accidental).empty());

    SECTION("hands point up and feet down, whatever voice holds them") {
        // Bass drum in the first voice, hi-hat in the second.
        const DisplayList voices =
            layout(songOf({{{drum({36}, NoteValue::Half), drum({36}, NoteValue::Half)},
                            {drum({42}), drum({42}), drum({42}), drum({42})}}},
                          kit));
        for (const LineItem& stem : lines(voices, Role::Stem)) {
            const double top = std::min(stem.from.y, stem.to.y);
            const double bottom = std::max(stem.from.y, stem.to.y);
            if (bottom < 0.0) { // a hi-hat stem lies above the staff
                CHECK(top < -0.5);
            } else {
                CHECK(top > 3.0); // a bass drum stem goes down from the first space
            }
        }
    }
}

TEST_CASE("Staff layout: a keyboard's strummed chord is rolled with an arpeggio line",
          "[notation][layout]") {
    const Part piano{.kind = InstrumentKind::Keyboard};
    Beat upwards = chord({"C4", "E4", "G4"}); // played like a downstroke, low notes first
    upwards.effects.strum = elara::core::Strum::Down;
    Beat downwards = chord({"C4", "Eb4", "G4"});
    downwards.effects.strum = elara::core::Strum::Up;
    const DisplayList list =
        layout(melody({upwards, downwards, chord({"C4", "E4", "G4"}, NoteValue::Half)}, piano));

    std::vector<CurveItem> waves;
    for (const CurveItem& curve : list.curves) {
        if (curve.role == Role::Arpeggio) {
            waves.push_back(curve);
        }
    }
    REQUIRE(waves.size() >= 4);
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 9);
    const double firstX = waves.front().from.x;
    const auto lineOf = [&](double x) {
        double top = std::numeric_limits<double>::max();
        double bottom = std::numeric_limits<double>::lowest();
        for (const CurveItem& wave : waves) {
            if (std::abs(wave.from.x - x) < kEpsilon) {
                CHECK(wave.to.x == wave.from.x); // a vertical line
                top = std::min(top, wave.from.y);
                bottom = std::max(bottom, wave.to.y);
            }
        }
        return std::pair{top, bottom};
    };
    // One line per rolled chord, left of its noteheads and accidentals.
    double secondX = firstX;
    for (const CurveItem& wave : waves) {
        if (std::abs(wave.from.x - firstX) > kEpsilon) {
            secondX = wave.from.x;
        }
    }
    REQUIRE(secondX > firstX);
    CHECK(firstX < left(heads[0]) - 0.3);
    CHECK(secondX > right(heads[2]));
    const auto accidentals = glyphs(list, Role::Accidental); // the flat, then a natural
    REQUIRE(accidentals.size() == 2);
    CHECK(secondX < left(accidentals[0]) - 0.3);

    // From half a space above the top notehead (G4) to half a space below the
    // bottom one (C4).
    const auto [top, bottom] = lineOf(firstX);
    const auto spanOf = [&](std::size_t first) {
        const auto ys =
            ysOf(std::vector<GlyphItem>(heads.begin() + static_cast<std::ptrdiff_t>(first),
                                        heads.begin() + static_cast<std::ptrdiff_t>(first + 3)));
        return std::pair{std::ranges::min(ys), std::ranges::max(ys)};
    };
    CHECK_THAT(top, WithinAbs(spanOf(0).first - 0.5, 1e-9));
    CHECK_THAT(bottom, WithinAbs(spanOf(0).second + 0.5, 1e-9));
    CHECK_THAT(spanOf(0).second - spanOf(0).first, WithinAbs(2.0, 1e-9));

    // The downward roll ends in an arrowhead at the bottom; the upward one has none.
    std::vector<PolygonItem> arrows;
    for (const PolygonItem& polygon : list.polygons) {
        if (polygon.role == Role::Arpeggio) {
            arrows.push_back(polygon);
        }
    }
    REQUIRE(arrows.size() == 1);
    const auto tip = std::ranges::max(arrows[0].points, {}, &elara::notation::Point::y);
    CHECK_THAT(tip.x, WithinAbs(secondX, 1e-9));
    CHECK_THAT(tip.y, WithinAbs(spanOf(3).second + 0.5, 1e-9));
    CHECK(lineOf(secondX).second < tip.y);

    // No strum arrows above the staff, and the line takes room of its own.
    CHECK(glyphs(list, Role::Technique).empty());
    const DisplayList plain = layout(melody({chord({"C4", "E4", "G4"}), chord({"C4", "Eb4", "G4"}),
                                             chord({"C4", "E4", "G4"}, NoteValue::Half)},
                                            piano));
    CHECK(glyphs(list, Role::Notehead)[0].origin.x >
          glyphs(plain, Role::Notehead)[0].origin.x + 0.6);

    SECTION("a single note's line is two spaces long, centred on it") {
        Beat alone = note("F4"); // the other hand's notes on the other staff
        alone.effects.strum = elara::core::Strum::Down;
        const DisplayList single = layout(melody({alone}, piano));
        double highest = std::numeric_limits<double>::max();
        double lowest = std::numeric_limits<double>::lowest();
        for (const CurveItem& curve : single.curves) {
            if (curve.role == Role::Arpeggio) {
                highest = std::min(highest, curve.from.y);
                lowest = std::max(lowest, curve.to.y);
            }
        }
        const auto head = glyphs(single, Role::Notehead);
        REQUIRE(head.size() == 1);
        CHECK_THAT(highest, WithinAbs(head[0].origin.y - 1.0, 1e-9));
        CHECK_THAT(lowest, WithinAbs(head[0].origin.y + 1.0, 1e-9));
    }

    SECTION("a fretted instrument keeps its strum arrows") {
        const DisplayList guitar =
            layout(melody({upwards, downwards}, Part{.kind = InstrumentKind::Fretted}));
        CHECK(std::ranges::none_of(
            guitar.curves, [](const CurveItem& curve) { return curve.role == Role::Arpeggio; }));
        const auto arrowsAbove = glyphs(guitar, Role::Technique);
        REQUIRE(arrowsAbove.size() == 2);
        CHECK(arrowsAbove[0].glyph == Glyph::ArrowBlackDown);
        CHECK(arrowsAbove[1].glyph == Glyph::ArrowBlackUp);
    }
}
