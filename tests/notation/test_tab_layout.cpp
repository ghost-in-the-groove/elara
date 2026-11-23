// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::BendCurve;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::FretPosition;
using elara::core::HarmonicKind;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::SlideIn;
using elara::core::SlideOut;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::capHeight;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutSystem;
using elara::notation::LineItem;
using elara::notation::PolygonItem;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::TextItem;

namespace {

constexpr double kEpsilon = 1e-9;
constexpr double kLineSpace = 1.5;

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// The note played at `fret` on `string` of a guitar in standard tuning.
Note fretted(int string, int fret) {
    return Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(string, fret)),
                .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                         .fret = static_cast<std::int8_t>(fret)}};
}

Beat beatOf(std::vector<Note> notes, NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value), .notes = std::move(notes)};
}

Beat play(int string, int fret, NoteValue value = NoteValue::Quarter) {
    return beatOf({fretted(string, fret)}, value);
}

Beat rest(NoteValue value = NoteValue::Quarter) {
    return Beat{.duration = Duration(value)};
}

// A guitar song (or a bass one) with one voice per measure.
Song guitar(const std::vector<std::vector<Beat>>& measures,
            const Tuning& tuning = Tuning::standardGuitar()) {
    Song song;
    song.appendMeasures(measures.size());
    song.addTrack(Track("Guitar",
                        Instrument{.name = "Guitar", .midiProgram = 25, .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, tuning)}));
    for (std::size_t m = 0; m < measures.size(); ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = measures[m];
    }
    return song;
}

DisplayList tab(const Song& song) {
    const StaffSelection selection{.view = StaffView::Tablature};
    return layoutSystem(song, std::span(&selection, 1), bravura());
}

DisplayList notationAndTab(const Song& song) {
    const std::array<StaffSelection, 2> staves{StaffSelection{.view = StaffView::Standard},
                                               StaffSelection{.view = StaffView::Tablature}};
    return layoutSystem(song, staves, bravura());
}

template <typename Item>
std::vector<Item> byRole(const std::vector<Item>& items, Role role) {
    std::vector<Item> found;
    for (const Item& item : items) {
        if (item.role == role) {
            found.push_back(item);
        }
    }
    return found;
}

std::vector<TextItem> texts(const DisplayList& list, Role role) {
    auto found = byRole(list.texts, role);
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

// The baseline that centres a fret number on the line of `string`.
double fretBaseline(int string, double staffY = 0.0) {
    return staffY + kLineSpace * (string - 1) + capHeight(1.3) / 2;
}

double noteheadCentre(const GlyphItem& head) {
    const auto& box = bravura().metrics(head.glyph).box;
    return head.origin.x + (box.southWest.x + box.northEast.x) / 2;
}

} // namespace

TEST_CASE("Tablature layout: a line per string and a TAB clef", "[notation][tab]") {
    const Song song = guitar({{play(1, 0)}});
    const DisplayList list = tab(song);
    const auto staff = byRole(list.lines, Role::StaffLine);
    REQUIRE(staff.size() == 6);
    for (std::size_t i = 0; i < staff.size(); ++i) {
        CHECK_THAT(staff[i].from.y, WithinAbs(kLineSpace * static_cast<double>(i), kEpsilon));
    }
    const auto clefs = byRole(list.glyphs, Role::Clef);
    REQUIRE(clefs.size() == 1);
    CHECK(clefs[0].glyph == Glyph::TabClef6);
    CHECK(byRole(list.glyphs, Role::KeySignature).empty());
    REQUIRE(list.staves.size() == 1);
    CHECK(list.staves[0].view == StaffView::Tablature);
    CHECK(list.staves[0].lines == 6);
    CHECK_THAT(list.staves[0].height, WithinAbs(7.5, kEpsilon));
    // Barlines run from the first string to the last.
    const auto barlines = byRole(list.lines, Role::Barline);
    REQUIRE_FALSE(barlines.empty());
    CHECK_THAT(barlines.front().to.y, WithinAbs(7.5, kEpsilon));

    const Song bass = guitar({{play(1, 0)}}, Tuning::standardBass());
    const DisplayList bassList = tab(bass);
    CHECK(byRole(bassList.lines, Role::StaffLine).size() == 4);
    CHECK(byRole(bassList.glyphs, Role::Clef).front().glyph == Glyph::TabClef4);
}

TEST_CASE("Tablature layout: fret numbers sit on their string lines", "[notation][tab]") {
    const Song song = guitar({{play(1, 3), beatOf({fretted(6, 0), fretted(5, 2)}), play(3, 12)}});
    const DisplayList list = tab(song);
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(contents(frets) == std::vector<std::string>{"3", "0", "2", "12"});
    CHECK_THAT(frets[0].origin.y, WithinAbs(fretBaseline(1), kEpsilon));
    CHECK_THAT(frets[3].origin.y, WithinAbs(fretBaseline(3), kEpsilon));
    // Both notes of the chord share their x; each number interrupts its line.
    std::vector<double> chordYs{frets[1].origin.y, frets[2].origin.y};
    std::ranges::sort(chordYs);
    CHECK_THAT(frets[1].origin.x, WithinAbs(frets[2].origin.x, kEpsilon));
    CHECK_THAT(chordYs[0], WithinAbs(fretBaseline(5), kEpsilon));
    CHECK_THAT(chordYs[1], WithinAbs(fretBaseline(6), kEpsilon));
    CHECK(std::ranges::all_of(frets, [](const TextItem& item) { return item.knockout; }));
    CHECK(frets[0].origin.x < frets[1].origin.x);
    CHECK(frets[1].origin.x < frets[3].origin.x);
}

TEST_CASE("Tablature layout: notes without a position take the lowest fret", "[notation][tab]") {
    Song song = guitar({{beatOf({Note{.pitch = Pitch::fromMidi(64)}}),    // E4: string 1 open
                         beatOf({Note{.pitch = Pitch::fromMidi(55)}})}}); // G3: string 3 open
    const auto frets = texts(tab(song), Role::FretNumber);
    REQUIRE(contents(frets) == std::vector<std::string>{"0", "0"});
    CHECK_THAT(frets[0].origin.y, WithinAbs(fretBaseline(1), kEpsilon));
    CHECK_THAT(frets[1].origin.y, WithinAbs(fretBaseline(3), kEpsilon));
}

TEST_CASE("Tablature layout: notation and tablature line up", "[notation][tab]") {
    const Song song =
        guitar({{play(1, 3), play(2, 1, NoteValue::Eighth), play(2, 3, NoteValue::Eighth), rest(),
                 beatOf({fretted(3, 2), fretted(4, 2)})},
                {play(6, 3, NoteValue::Whole)}});
    const DisplayList list = notationAndTab(song);
    REQUIRE(list.staves.size() == 2);
    const double tabY = list.staves[1].y;
    CHECK(list.staves[0].y == 0.0);
    CHECK(list.staves[1].view == StaffView::Tablature);
    // The tablature is below every mark of the notation staff.
    double notationBottom = 0.0;
    for (const GlyphItem& item : list.glyphs) {
        if (item.origin.y < tabY - 1.0) {
            notationBottom = std::max(notationBottom, item.origin.y);
        }
    }
    CHECK(tabY > notationBottom);
    CHECK(tabY >= 4.0 + 4.0);

    // Each fret number is centred under its notehead.
    auto heads = byRole(list.glyphs, Role::Notehead);
    std::ranges::stable_sort(heads, {}, [](const GlyphItem& item) { return item.origin.x; });
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(frets.size() == heads.size());
    for (std::size_t i = 0; i < frets.size(); ++i) {
        CHECK_THAT(frets[i].origin.x, WithinAbs(noteheadCentre(heads[i]), 1e-6));
    }
    // Shared barlines, measures and a rest only in the notation.
    std::vector<double> notationBars;
    std::vector<double> tabBars;
    std::size_t joins = 0;
    for (const LineItem& line : byRole(list.lines, Role::Barline)) {
        if (line.from.y < tabY - kEpsilon && line.to.y > tabY + kEpsilon) {
            ++joins; // the line joining the staves at the start of the system
            continue;
        }
        (line.from.y < tabY - kEpsilon ? notationBars : tabBars).push_back(line.from.x);
    }
    CHECK(notationBars == tabBars);
    CHECK(joins == 1);
    CHECK(list.measures.size() == 2);
    for (const GlyphItem& item : byRole(list.glyphs, Role::Rest)) {
        CHECK(item.origin.y < tabY);
    }
    // On its own the tablature shows the rest.
    CHECK(byRole(tab(song).glyphs, Role::Rest).size() == 1);
}

TEST_CASE("Tablature layout: dead, ghost, tied and harmonic notes", "[notation][tab]") {
    Note dead = fretted(5, 0);
    dead.effects.deadNote = true;
    Note ghost = fretted(4, 5);
    ghost.effects.ghost = true;
    Note tied = fretted(4, 5);
    tied.effects.tiedFromPrevious = true;
    Note harmonic = fretted(3, 12);
    harmonic.effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = 120};
    Note node = fretted(2, 3);
    node.effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = 32};
    const Song song = guitar(
        {{beatOf({dead}), beatOf({ghost}), beatOf({tied}), beatOf({harmonic})}, {beatOf({node})}});
    CHECK(contents(texts(tab(song), Role::FretNumber)) ==
          std::vector<std::string>{"X", "(5)", "(5)", "<12>", "<3.2>"});
}

TEST_CASE("Tablature layout: hammer-ons and pull-offs", "[notation][tab]") {
    Note hammer = fretted(2, 7);
    hammer.effects.legatoFromPrevious = true;
    Note pull = fretted(2, 5);
    pull.effects.legatoFromPrevious = true;
    const Song song = guitar({{play(2, 5), beatOf({hammer}), beatOf({pull}), play(2, 5)}});
    const DisplayList list = tab(song);
    const auto arcs = byRole(list.curves, Role::Slur);
    REQUIRE(arcs.size() == 2);
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(frets.size() == 4);
    // Each arc goes over the numbers it joins, between their centres.
    for (std::size_t i = 0; i < arcs.size(); ++i) {
        const CurveItem& arc = arcs[i];
        CHECK(arc.from.x >= frets[i].origin.x);
        CHECK(arc.to.x <= frets[i + 1].origin.x);
        CHECK(arc.control1.y < arc.from.y);
        CHECK(arc.from.y < kLineSpace - capHeight(1.3) / 2);
        CHECK(arc.thickness > arc.endThickness);
    }
    CHECK(contents(texts(list, Role::Technique)) == std::vector<std::string>{"H", "P"});
}

TEST_CASE("Tablature layout: slides between, into and out of notes", "[notation][tab]") {
    Note up = fretted(3, 5);
    up.effects.slideOut = SlideOut::Shift;
    Note into = fretted(3, 9);
    into.effects.slideIn = SlideIn::FromBelow;
    Note off = fretted(3, 7);
    off.effects.slideOut = SlideOut::Downwards;
    const Song song = guitar({{beatOf({up}), play(3, 7), beatOf({into}), beatOf({off})}});
    const DisplayList list = tab(song);
    auto slides = byRole(list.lines, Role::Slide);
    std::ranges::stable_sort(slides, {}, [](const LineItem& item) { return item.from.x; });
    REQUIRE(slides.size() == 3);
    const auto frets = texts(list, Role::FretNumber);
    // 5 to 7: a line climbing between the two numbers.
    CHECK(slides[0].from.x > frets[0].origin.x);
    CHECK(slides[0].to.x < frets[1].origin.x);
    CHECK(slides[0].to.y < slides[0].from.y);
    // Into the 9 from below, ending before it.
    CHECK(slides[1].to.x < frets[2].origin.x);
    CHECK(slides[1].from.x > frets[1].origin.x);
    CHECK(slides[1].to.y < slides[1].from.y);
    // Out of the last note downwards.
    CHECK(slides[2].from.x > frets[3].origin.x);
    CHECK(slides[2].to.y > slides[2].from.y);
    CHECK(slides[2].to.x < list.width);
}

TEST_CASE("Tablature layout: bends rise to arrows above the staff", "[notation][tab]") {
    Note full = fretted(2, 8);
    full.effects.bend = BendCurve::bend(200);
    Note half = fretted(3, 7);
    half.effects.bend = BendCurve::bendRelease(100);
    Note pre = fretted(2, 10);
    pre.effects.bend = BendCurve::preBend(300);
    const Song song = guitar({{beatOf({full}), beatOf({half}), beatOf({pre})}});
    const DisplayList list = tab(song);
    const auto labels = texts(list, Role::Bend);
    CHECK(contents(labels) == std::vector<std::string>{"full", "1/2", "1 1/2"});
    const auto arrows = byRole(list.polygons, Role::Bend);
    REQUIRE(arrows.size() == 4); // three up, one release down
    const auto frets = texts(list, Role::FretNumber);
    int up = 0;
    for (const PolygonItem& arrow : arrows) {
        REQUIRE(arrow.points.size() == 3);
        const elara::notation::Point tip = arrow.points[0];
        const bool pointsUp = tip.y < arrow.points[1].y;
        up += pointsUp ? 1 : 0;
        if (pointsUp) {
            CHECK(tip.y < 0.0); // above the first string
        } else {
            CHECK(tip.y > 0.0); // back down towards the string
        }
    }
    CHECK(up == 3);
    // Every label is above its arrow, all on one lane.
    for (const TextItem& label : labels) {
        CHECK_THAT(label.origin.y, WithinAbs(labels.front().origin.y, kEpsilon));
        CHECK(label.origin.y < 0.0);
    }
    // The prebend rises straight from its number.
    const auto straight = byRole(list.lines, Role::Bend);
    REQUIRE(straight.size() == 1);
    CHECK_THAT(straight[0].from.x, WithinAbs(frets[2].origin.x, kEpsilon));
    CHECK_THAT(straight[0].to.x, WithinAbs(frets[2].origin.x, kEpsilon));
    CHECK(byRole(list.curves, Role::Bend).size() == 3); // two bends, one release
}

TEST_CASE("Tablature layout: palm mute and let ring run over their notes", "[notation][tab]") {
    std::vector<Beat> beats;
    for (int i = 0; i < 4; ++i) {
        Note note = fretted(6, 0);
        note.effects.palmMute = i < 3;
        note.effects.letRing = i == 3;
        beats.push_back(beatOf({note}, NoteValue::Quarter));
    }
    Note ring = fretted(1, 0);
    ring.effects.letRing = true;
    const Song song = guitar({beats, {beatOf({ring}, NoteValue::Whole)}});
    const DisplayList list = tab(song);
    const auto marks = texts(list, Role::Technique);
    REQUIRE(contents(marks) == std::vector<std::string>{"P.M.", "let ring"});
    CHECK(marks[0].origin.y < 0.0);
    CHECK(marks[1].italic);
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(frets.size() == 5);
    std::vector<LineItem> dashed;
    for (const LineItem& line : byRole(list.lines, Role::Technique)) {
        if (line.dash > 0.0) {
            dashed.push_back(line);
        }
    }
    std::ranges::stable_sort(dashed, {}, [](const LineItem& item) { return item.from.x; });
    REQUIRE(dashed.size() == 2);
    // P.M. ends before the fourth note; let ring carries over the barline.
    CHECK(dashed[0].to.x > frets[2].origin.x);
    CHECK(dashed[0].to.x < frets[3].origin.x);
    CHECK(dashed[1].to.x > frets[4].origin.x);
    CHECK(list.measures.size() == 2);
    // The two marks have lanes of their own.
    CHECK(marks[0].origin.y != marks[1].origin.y);
}

TEST_CASE("Tablature layout: vibrato and pick strokes above the staff", "[notation][tab]") {
    Note shaken = fretted(2, 7);
    shaken.effects.vibrato = elara::core::Vibrato::Slight;
    Beat down = play(1, 0);
    down.effects.pickStroke = elara::core::PickStroke::Down;
    Beat up = play(1, 0);
    up.effects.pickStroke = elara::core::PickStroke::Up;
    const Song song = guitar({{beatOf({shaken}, NoteValue::Half), down, up}});
    const DisplayList list = tab(song);
    const auto waves = byRole(list.glyphs, Role::Vibrato);
    REQUIRE(waves.size() >= 2);
    for (const GlyphItem& wave : waves) {
        CHECK(wave.glyph == Glyph::GuitarVibratoStroke);
        CHECK(wave.origin.y < 0.0);
    }
    const auto strokes = byRole(list.glyphs, Role::Technique);
    REQUIRE(strokes.size() == 2);
    CHECK(strokes[0].glyph == Glyph::StringsDownBow);
    CHECK(strokes[1].glyph == Glyph::StringsUpBow);
    CHECK(strokes[0].origin.y < waves[0].origin.y);
}

TEST_CASE("Tablature layout: standard notation shows ties and articulations", "[notation][tab]") {
    Note held = fretted(1, 0); // E4, written E5: stem down
    held.effects.staccato = true;
    Note tied = fretted(1, 0);
    tied.effects.tiedFromPrevious = true;
    tied.effects.accent = elara::core::Accent::Accent;
    Note low = fretted(6, 3); // G2, written G3: stem up
    low.effects.staccato = true;
    const Song song = guitar({{beatOf({held}, NoteValue::Half), beatOf({tied}), beatOf({low})}});
    const StaffSelection standard{};
    const DisplayList list = layoutSystem(song, std::span(&standard, 1), bravura());
    auto heads = byRole(list.glyphs, Role::Notehead);
    std::ranges::stable_sort(heads, {}, [](const GlyphItem& item) { return item.origin.x; });
    REQUIRE(heads.size() == 3);
    const auto ties = byRole(list.curves, Role::Tie);
    REQUIRE(ties.size() == 1);
    const auto& box = bravura().metrics(heads[0].glyph).box;
    CHECK(ties[0].from.x > heads[0].origin.x + box.northEast.x);
    CHECK(ties[0].to.x < heads[1].origin.x);
    // Stems down: the tie curves above the notes.
    CHECK(ties[0].from.y < heads[0].origin.y);
    CHECK(ties[0].control1.y < ties[0].from.y);
    auto marks = byRole(list.glyphs, Role::Articulation);
    std::ranges::stable_sort(marks, {}, [](const GlyphItem& item) { return item.origin.x; });
    REQUIRE(marks.size() == 3);
    CHECK(marks[0].glyph == Glyph::ArticStaccatoAbove);
    CHECK(marks[0].origin.y < heads[0].origin.y);
    CHECK(marks[1].glyph == Glyph::ArticAccentAbove);
    CHECK(marks[2].glyph == Glyph::ArticStaccatoBelow);
    CHECK(marks[2].origin.y > heads[2].origin.y);
}

TEST_CASE("Tablature layout: techniques go above the notation without tablature",
          "[notation][tab]") {
    Note muted = fretted(6, 0);
    muted.effects.palmMute = true;
    const Song song = guitar({{beatOf({muted}), beatOf({muted})}});
    const StaffSelection standard{};
    const DisplayList alone = layoutSystem(song, std::span(&standard, 1), bravura());
    CHECK(contents(texts(alone, Role::Technique)) == std::vector<std::string>{"P.M."});
    // With tablature the mark moves above the tablature, once.
    const DisplayList both = notationAndTab(song);
    const auto marks = texts(both, Role::Technique);
    REQUIRE(marks.size() == 1);
    CHECK(marks[0].origin.y > 4.0);
    CHECK(marks[0].origin.y < both.staves[1].y);
}

TEST_CASE("Tablature layout: rejects what it cannot show", "[notation][tab]") {
    Song song = guitar({{play(1, 0)}});
    song.addTrack(Track("Flute", Instrument{.name = "Flute"}, {Staff(Clef::Treble)}));
    const StaffSelection flute{.track = 1, .view = StaffView::Tablature};
    CHECK_THROWS_AS(layoutSystem(song, std::span(&flute, 1), bravura()), std::invalid_argument);
    CHECK_THROWS_AS(layoutSystem(song, {}, bravura()), std::invalid_argument);
    const StaffSelection missing{.track = 7};
    CHECK_THROWS_AS(layoutSystem(song, std::span(&missing, 1), bravura()), std::out_of_range);
}
