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
using elara::core::Grace;
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

TEST_CASE("Tablature layout: grace notes are small fret numbers before their note",
          "[notation][tab]") {
    // A grace note hammered on to the 7th fret (ADR-0079).
    Note hammer = fretted(3, 7);
    hammer.effects.legatoFromPrevious = true;
    Beat graced = beatOf({hammer});
    graced.graces = {Grace{.duration = Duration(NoteValue::Eighth), .notes = {fretted(3, 5)}}};
    const DisplayList list = tab(guitar({{play(1, 0), graced}}));
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(contents(frets) == std::vector<std::string>{"0", "5", "7"});
    const double small = 1.3 * 0.65;
    CHECK_THAT(frets[1].size, WithinAbs(small, kEpsilon));
    CHECK_THAT(frets[2].size, WithinAbs(1.3, kEpsilon));
    CHECK_THAT(frets[1].origin.y, WithinAbs(kLineSpace * 2 + capHeight(small) / 2, kEpsilon));
    CHECK(frets[1].knockout);
    // The hammer-on arc and its "H" join the grace note to its note.
    const auto arcs = byRole(list.curves, Role::Slur);
    REQUIRE(arcs.size() == 1);
    CHECK(arcs[0].from.x >= frets[1].origin.x);
    CHECK(arcs[0].to.x <= frets[2].origin.x);
    CHECK(contents(texts(list, Role::Technique)) == std::vector<std::string>{"H"});
    CHECK(frets[1].origin.x - frets[0].origin.x > 0.8);
    CHECK(frets[2].origin.x - frets[1].origin.x > 0.6);
    // Room is made for it when the note before leaves too little.
    const auto after = [](const Beat& beat) {
        return texts(tab(guitar({{play(1, 0, NoteValue::ThirtySecond), beat}})), Role::FretNumber)
            .back()
            .origin.x;
    };
    CHECK(after(graced) > after(beatOf({hammer})) + 0.5);
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

TEST_CASE("Tablature layout: strums and tremolo picking above the staff", "[notation][tab]") {
    Beat downStrum = beatOf({fretted(3, 0), fretted(2, 1), fretted(1, 0)});
    downStrum.effects.strum = elara::core::Strum::Down;
    Beat upStrum = downStrum;
    upStrum.effects.strum = elara::core::Strum::Up;
    upStrum.effects.pickStroke = elara::core::PickStroke::Up;
    Beat fast = play(1, 5);
    fast.effects.tremoloPicking = NoteValue::Sixteenth;
    Beat faster = play(1, 5);
    faster.effects.tremoloPicking = NoteValue::ThirtySecond;
    const Song song = guitar({{downStrum, upStrum, fast, faster}});
    const DisplayList list = tab(song);
    auto marks = byRole(list.glyphs, Role::Technique);
    std::ranges::stable_sort(marks, {}, [](const GlyphItem& item) { return item.origin.x; });
    REQUIRE(marks.size() == 5);
    // Arrows the way the pick goes over the strings, as strumming patterns write them.
    CHECK(marks[0].glyph == Glyph::ArrowBlackDown);
    const auto upArrow = std::ranges::find(marks, Glyph::ArrowBlackUp, &GlyphItem::glyph);
    const auto upBow = std::ranges::find(marks, Glyph::StringsUpBow, &GlyphItem::glyph);
    REQUIRE(upArrow != marks.end());
    REQUIRE(upBow != marks.end());
    // A pick stroke and a strum on one beat each have a lane.
    CHECK(upArrow->origin.y != upBow->origin.y);
    // Tremolo picking: one slash per beam of the repeated notes' value.
    CHECK(marks[3].glyph == Glyph::Tremolo2);
    CHECK(marks[4].glyph == Glyph::Tremolo3);
    // In one lane: the slashes' ink starts at the same height.
    const auto inkBottom = [](const GlyphItem& mark) {
        return mark.origin.y - bravura().metrics(mark.glyph).box.southWest.y * mark.scale;
    };
    CHECK_THAT(inkBottom(marks[3]), WithinAbs(inkBottom(marks[4]), 1e-9));
    for (const GlyphItem& mark : marks) {
        CHECK(mark.origin.y < 0.0); // above the staff
    }
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

TEST_CASE("Tablature layout: rhythm under the tablature without notation", "[notation][tab]") {
    constexpr double kBottomLine = 7.5;
    const auto stemsOf = [](const DisplayList& list) {
        auto found = byRole(list.lines, Role::Stem);
        std::ranges::stable_sort(found, {}, [](const LineItem& line) { return line.from.x; });
        return found;
    };
    const auto low = [](const LineItem& line) {
        return std::max(line.from.y, line.to.y);
    };
    const auto high = [](const LineItem& line) {
        return std::min(line.from.y, line.to.y);
    };
    const auto glyphTop = [](const GlyphItem& item) {
        return item.origin.y - bravura().metrics(item.glyph).box.northEast.y;
    };

    SECTION("stems under the fret numbers, beams for eighths, a shorter stem for a half") {
        const Song song = guitar({{play(1, 0), play(2, 1, NoteValue::Eighth),
                                   play(3, 2, NoteValue::Eighth), play(1, 3, NoteValue::Half)}});
        const DisplayList list = tab(song);
        const auto stems = stemsOf(list);
        const auto frets = texts(list, Role::FretNumber);
        REQUIRE(stems.size() == 4);
        REQUIRE(frets.size() == 4);
        for (std::size_t i = 0; i < stems.size(); ++i) {
            CHECK(stems[i].from.x == stems[i].to.x);
            CHECK_THAT(stems[i].from.x, WithinAbs(frets[i].origin.x, kEpsilon));
            CHECK(high(stems[i]) > kBottomLine + 0.5);
            CHECK_THAT(low(stems[i]), WithinAbs(low(stems[0]), kEpsilon));
        }
        CHECK(high(stems[3]) > high(stems[0]) + 0.5);
        CHECK_THAT(high(stems[1]), WithinAbs(high(stems[0]), kEpsilon));
        REQUIRE(list.beams.size() == 1);
        const auto& beam = list.beams[0];
        CHECK(beam.left.y == beam.right.y);
        CHECK_THAT(beam.left.y + beam.thickness / 2, WithinAbs(low(stems[1]), kEpsilon));
        CHECK(beam.left.x <= stems[1].from.x);
        CHECK(beam.right.x >= stems[2].from.x);
        CHECK(byRole(list.glyphs, Role::Flag).empty());
        CHECK(list.bottom >= low(stems[0]) - kEpsilon);
    }
    SECTION("flags, dots, rests and whole notes") {
        const Song song =
            guitar({{play(1, 0, NoteValue::Eighth), rest(NoteValue::Eighth),
                     Beat{.duration = Duration(NoteValue::Quarter, 1), .notes = {fretted(2, 1)}},
                     play(3, 2, NoteValue::Eighth), rest()},
                    {play(6, 3, NoteValue::Whole)}});
        const DisplayList list = tab(song);
        const auto stems = stemsOf(list);
        REQUIRE(stems.size() == 3); // no stem for rests or the whole note
        CHECK(list.beams.empty());
        const auto flags = byRole(list.glyphs, Role::Flag);
        REQUIRE(flags.size() == 2);
        CHECK(flags[0].glyph == Glyph::Flag8thDown);
        CHECK(flags[1].glyph == Glyph::Flag8thDown);
        const auto dots = byRole(list.glyphs, Role::Dot);
        REQUIRE(dots.size() == 1);
        CHECK(dots[0].origin.x > stems[1].from.x);
        CHECK(dots[0].origin.x < stems[2].from.x);
        CHECK(dots[0].origin.y > high(stems[1]));
        CHECK(dots[0].origin.y < low(stems[1]));
        CHECK(byRole(list.glyphs, Role::Rest).size() == 2);
    }
    SECTION("sixteenths: a second beam, a beamlet after a dotted eighth") {
        const Song song =
            guitar({{play(1, 0, NoteValue::Sixteenth), play(1, 1, NoteValue::Sixteenth),
                     play(1, 2, NoteValue::Sixteenth), play(1, 3, NoteValue::Sixteenth),
                     Beat{.duration = Duration(NoteValue::Eighth, 1), .notes = {fretted(2, 1)}},
                     play(2, 3, NoteValue::Sixteenth), rest(NoteValue::Half)}});
        const DisplayList list = tab(song);
        const auto stems = stemsOf(list);
        REQUIRE(stems.size() == 6);
        REQUIRE(list.beams.size() == 4);
        auto beams = list.beams;
        std::ranges::sort(beams, {}, [](const auto& beam) { return beam.left.x + beam.left.y; });
        // Secondary beams lie above the primary ones, towards the staff.
        const double primary =
            std::ranges::max(list.beams, {}, [](const auto& b) { return b.left.y; }).left.y;
        const auto secondary = std::ranges::count_if(
            list.beams, [&](const auto& b) { return b.left.y < primary - kEpsilon; });
        CHECK(secondary == 2);
        const auto shortest = std::ranges::min(
            list.beams, {}, [](const auto& beam) { return beam.right.x - beam.left.x; });
        CHECK(shortest.right.x - shortest.left.x < stems[5].from.x - stems[4].from.x);
    }
    SECTION("a triplet: its number under the beam") {
        const elara::core::Tuplet triplet(3, 2);
        const auto eighth = [&](int string, int fret) {
            return Beat{.duration = Duration(NoteValue::Eighth, 0, triplet),
                        .notes = {fretted(string, fret)}};
        };
        const Song song = guitar(
            {{eighth(1, 0), eighth(2, 1), eighth(3, 2), play(1, 3, NoteValue::Half), play(1, 0)}});
        const DisplayList list = tab(song);
        const auto numbers = byRole(list.glyphs, Role::Tuplet);
        REQUIRE(numbers.size() == 1);
        CHECK(numbers[0].glyph == Glyph::Tuplet3);
        REQUIRE(list.beams.size() == 1);
        const double beamBottom = list.beams[0].left.y + list.beams[0].thickness / 2;
        CHECK(glyphTop(numbers[0]) > beamBottom + 0.25);
        CHECK(glyphTop(numbers[0]) < beamBottom + 1.0);
        CHECK(byRole(list.lines, Role::Tuplet).empty());
    }
    SECTION("under standard notation the tablature has no rhythm") {
        const Song song = guitar({{play(1, 0), play(2, 1, NoteValue::Eighth),
                                   play(3, 2, NoteValue::Eighth), play(1, 3, NoteValue::Half)}});
        const DisplayList list = notationAndTab(song);
        REQUIRE(list.staves.size() == 2);
        const double tabY = list.staves[1].y;
        const auto stems = byRole(list.lines, Role::Stem);
        CHECK(stems.size() == 4);
        for (const LineItem& stem : stems) {
            CHECK(std::max(stem.from.y, stem.to.y) < tabY);
        }
        CHECK(list.beams.size() == 1);
    }
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

TEST_CASE("Tablature layout: whole tracks as notation, tablature or both", "[notation][tab]") {
    using elara::notation::selectTracks;
    using elara::notation::TrackView;
    Song song = guitar({{play(1, 0)}});
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano"}, {Staff(Clef::Treble), Staff(Clef::Bass)}));
    const std::array<std::size_t, 2> both{0, 1};

    const auto standard = selectTracks(song, both, TrackView::Standard);
    CHECK(standard == std::vector<StaffSelection>{{.track = 0, .staff = 0},
                                                  {.track = 1, .staff = 0},
                                                  {.track = 1, .staff = 1}});
    // Staves without a tuning stay in standard notation.
    const auto tablature = selectTracks(song, both, TrackView::Tablature);
    CHECK(tablature ==
          std::vector<StaffSelection>{{.track = 0, .staff = 0, .view = StaffView::Tablature},
                                      {.track = 1, .staff = 0},
                                      {.track = 1, .staff = 1}});
    const auto all = selectTracks(song, both, TrackView::Both);
    CHECK(all == std::vector<StaffSelection>{{.track = 0, .staff = 0},
                                             {.track = 0, .staff = 0, .view = StaffView::Tablature},
                                             {.track = 1, .staff = 0},
                                             {.track = 1, .staff = 1}});
    CHECK_NOTHROW(layoutSystem(song, all, bravura()));

    CHECK(elara::notation::hasTablature(song, both));
    const std::array<std::size_t, 1> piano{1};
    CHECK_FALSE(elara::notation::hasTablature(song, piano));
    const std::array<std::size_t, 1> missing{2};
    CHECK_THROWS_AS(selectTracks(song, missing, TrackView::Both), std::out_of_range);
}

TEST_CASE("Tablature layout: whammy bar and bass techniques above the staff", "[notation][tab]") {
    using elara::core::BendForm;
    using elara::core::whammyCurve;
    Beat dip = play(3, 5);
    dip.effects.whammyBar = whammyCurve(BendForm::BendRelease, -300);
    Beat dive = play(3, 7);
    dive.effects.whammyBar = whammyCurve(BendForm::Bend, -200);
    Beat slap = play(4, 3);
    slap.effects.bassTechnique = elara::core::BassTechnique::Slap;
    Beat pull = play(1, 5);
    pull.effects.whammyBar = whammyCurve(BendForm::PreBend, 100);
    pull.effects.bassTechnique = elara::core::BassTechnique::Pop;
    const Song song = guitar({{dip, dive, slap, pull}});
    const DisplayList list = tab(song);

    // "w/bar" starts each run of whammy-bar beats; every beat says how far
    // the bar goes, in steps from the written pitch.
    const auto labels = texts(list, Role::Bend);
    CHECK(contents(labels) == std::vector<std::string>{"w/bar -1 1/2", "-1", "w/bar +1/2"});
    const auto frets = texts(list, Role::FretNumber);
    REQUIRE(frets.size() == 4);
    for (const TextItem& label : labels) {
        CHECK(label.origin.y < 0.0); // above the staff
        CHECK_THAT(label.origin.y, WithinAbs(labels.front().origin.y, kEpsilon));
    }

    // The curve, drawn after its beat's number: the dip goes down and comes
    // back, the dive goes down and holds, and the pre-pull rises from the
    // written pitch before the attack, then holds.
    auto lines = byRole(list.lines, Role::Bend);
    std::ranges::stable_sort(lines, {}, [](const LineItem& line) { return line.from.x; });
    REQUIRE(lines.size() == 7); // three, two, then a rise and a hold
    for (const LineItem& line : lines) {
        CHECK(line.from.y < 0.0);
        CHECK(line.to.y < 0.0);
        CHECK(line.from.y > labels.front().origin.y); // under the labels
    }
    CHECK(lines[0].to.y > lines[0].from.y);                          // the dip goes down,
    CHECK_THAT(lines[1].to.y, WithinAbs(lines[1].from.y, kEpsilon)); // holds,
    CHECK(lines[2].to.y < lines[2].from.y);                          // and comes back
    CHECK_THAT(lines[2].to.y, WithinAbs(lines[0].from.y, kEpsilon));
    CHECK(lines[3].from.x > frets[1].origin.x - kEpsilon);
    CHECK(lines[3].to.y > lines[3].from.y);
    CHECK_THAT(lines[5].from.x, WithinAbs(lines[5].to.x, kEpsilon)); // the rise is upright
    CHECK(lines[5].to.y < lines[5].from.y);

    // Slap and pop are lettered over their beats.
    const auto techniques = texts(list, Role::Technique);
    REQUIRE(contents(techniques) == std::vector<std::string>{"S", "P"});
    CHECK_THAT(techniques[0].origin.x, WithinAbs(frets[2].origin.x, kEpsilon));
    CHECK(techniques[0].origin.y < 0.0);
}
