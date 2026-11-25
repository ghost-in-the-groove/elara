// SPDX-License-Identifier: AGPL-3.0-or-later
// Repeat barlines, repeat counts and alternate endings (ADR-0016).
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::KeyMode;
using elara::core::KeySignature;
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
using elara::notation::layoutPages;
using elara::notation::layoutStaff;
using elara::notation::LineItem;
using elara::notation::PageLayoutOptions;
using elara::notation::Role;
using elara::notation::StaffLayoutOptions;
using elara::notation::StaffSelection;
using elara::notation::SystemBox;
using elara::notation::TextAlign;
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

// `count` measures on one treble staff, each with four quarter notes B4.
Song quarters(std::size_t count) {
    Song song;
    song.appendMeasures(count);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    const Beat beat{.duration = Duration(NoteValue::Quarter),
                    .notes = {Note{.pitch = *Pitch::parse("B4")}}};
    for (std::size_t m = 0; m < count; ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = {beat, beat, beat, beat};
    }
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

std::vector<LineItem> lines(const DisplayList& list, Role role) {
    std::vector<LineItem> found;
    std::ranges::copy_if(list.lines, std::back_inserter(found),
                         [role](const LineItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const LineItem& item) { return item.from.x; });
    return found;
}

std::vector<TextItem> texts(const DisplayList& list, Role role) {
    std::vector<TextItem> found;
    std::ranges::copy_if(list.texts, std::back_inserter(found),
                         [role](const TextItem& item) { return item.role == role; });
    std::ranges::stable_sort(found, {}, [](const TextItem& item) { return item.origin.x; });
    return found;
}

double left(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.southWest.x;
}

double right(const GlyphItem& item) {
    return item.origin.x + bravura().metrics(item.glyph).box.northEast.x;
}

double centreY(const GlyphItem& item) {
    const auto& box = bravura().metrics(item.glyph).box;
    return item.origin.y - (box.southWest.y + box.northEast.y) / 2;
}

// The left and right of a vertical line.
double left(const LineItem& line) {
    return line.from.x - line.thickness / 2;
}

double right(const LineItem& line) {
    return line.from.x + line.thickness / 2;
}

bool vertical(const LineItem& line) {
    return line.from.x == line.to.x;
}

std::vector<LineItem> verticals(const std::vector<LineItem>& all, bool wanted = true) {
    std::vector<LineItem> found;
    std::ranges::copy_if(all, std::back_inserter(found),
                         [wanted](const LineItem& line) { return vertical(line) == wanted; });
    return found;
}

std::vector<LineItem> thick(const std::vector<LineItem>& barlines) {
    std::vector<LineItem> found;
    std::ranges::copy_if(barlines, std::back_inserter(found), [](const LineItem& line) {
        return line.thickness == bravura().defaults().thickBarlineThickness;
    });
    return found;
}

} // namespace

TEST_CASE("Repeat layout: repeat barlines with dots in the middle spaces", "[notation][repeats]") {
    // | 0 |: 1 | 2 :| 3 ||
    Song song = quarters(4);
    song.measureHeader(1).repeatStart = true;
    song.measureHeader(2).repeatTimes = 2;
    const DisplayList list = layout(song);
    const auto& defaults = bravura().defaults();

    const auto dots = glyphs(list, Role::Repeat);
    REQUIRE(dots.size() == 4);
    for (std::size_t i = 0; i < 4; i += 2) {
        CHECK(dots[i].glyph == Glyph::RepeatDot);
        CHECK(dots[i].measure == 1 + i / 2);
        CHECK_THAT(left(dots[i]), WithinAbs(left(dots[i + 1]), kEpsilon));
        const auto [high, low] = std::minmax({centreY(dots[i]), centreY(dots[i + 1])});
        CHECK_THAT(high, WithinAbs(1.5, kEpsilon)); // the second space from the top
        CHECK_THAT(low, WithinAbs(2.5, kEpsilon));  // the third
    }

    // thin, the start repeat (thick, thin), thin, the end repeat (thin, thick), the final
    // barline.
    const auto barlines = lines(list, Role::Barline);
    REQUIRE(barlines.size() == 8);
    std::vector<double> thicknesses;
    for (const LineItem& line : barlines) {
        thicknesses.push_back(line.thickness);
    }
    const double thin = defaults.thinBarlineThickness;
    const double thick = defaults.thickBarlineThickness;
    CHECK(thicknesses == std::vector<double>{thin, thick, thin, thin, thin, thick, thin, thick});

    // The start repeat replaces the barline before it: its thick line covers the thin one.
    CHECK_THAT(left(barlines[1]), WithinAbs(left(barlines[0]), kEpsilon));
    CHECK_THAT(left(barlines[2]) - right(barlines[1]),
               WithinAbs(defaults.thinThickBarlineSeparation, kEpsilon));
    CHECK_THAT(left(dots[0]) - right(barlines[2]),
               WithinAbs(defaults.repeatBarlineDotSeparation, kEpsilon));
    // The end repeat mirrors it.
    CHECK_THAT(left(barlines[4]) - right(dots[2]),
               WithinAbs(defaults.repeatBarlineDotSeparation, kEpsilon));
    CHECK_THAT(left(barlines[5]) - right(barlines[4]),
               WithinAbs(defaults.thinThickBarlineSeparation, kEpsilon));

    // Measures sit between the dots.
    const auto heads = glyphs(list, Role::Notehead);
    REQUIRE(heads.size() == 16);
    CHECK(list.measures[1].x >= right(dots[0]) - kEpsilon);
    CHECK(left(heads[4]) > right(dots[0]));
    CHECK_THAT(list.measures[2].x + list.measures[2].width, WithinAbs(left(dots[2]), kEpsilon));
    CHECK(right(heads[11]) < left(dots[2]));
    CHECK(texts(list, Role::Repeat).empty()); // played twice: no count
}

TEST_CASE("Barline layout: a double barline closes a measure unless a repeat or the end does",
          "[notation][repeats][barline]") {
    // ADR-0103: 0 || 1 :| 2 |: 3 || 4 |, a double barline asked for on each.
    Song song = quarters(5);
    for (std::size_t m = 0; m < 5; ++m) {
        song.measureHeader(m).doubleBar = true;
    }
    song.measureHeader(1).repeatTimes = 2;    // the end repeat shows instead
    song.measureHeader(3).repeatStart = true; // stands for the double barline before it
    const DisplayList list = layout(song);
    const auto& defaults = bravura().defaults();
    const double thin = defaults.thinBarlineThickness;
    const double thick = defaults.thickBarlineThickness;

    // thin thin, the end repeat (thin, thick), thin under the start repeat
    // (thick, thin), thin thin, the final barline (thin, thick).
    const auto barlines = lines(list, Role::Barline);
    std::vector<double> thicknesses;
    for (const LineItem& line : barlines) {
        thicknesses.push_back(line.thickness);
    }
    CHECK(thicknesses ==
          std::vector<double>{thin, thin, thin, thick, thin, thick, thin, thin, thin, thin, thick});
    REQUIRE(barlines.size() == 11);
    CHECK(barlines[0].measure == 0);
    CHECK(barlines[1].measure == 0);
    CHECK_THAT(left(barlines[1]) - right(barlines[0]),
               WithinAbs(defaults.barlineSeparation, kEpsilon));
    // The next measure starts after both lines.
    CHECK(list.measures[1].x >= right(barlines[1]) - kEpsilon);
}

TEST_CASE("Repeat layout: start repeats at the start of a line follow the signatures",
          "[notation][repeats]") {
    // |: 0 :|: 1 :|
    Song song = quarters(2);
    for (std::size_t m = 0; m < 2; ++m) {
        song.measureHeader(m).repeatStart = true;
        song.measureHeader(m).repeatTimes = 2;
        song.measureHeader(m).keySignature = KeySignature(2, KeyMode::Major);
    }
    const DisplayList list = layout(song);
    CHECK(glyphs(list, Role::Repeat).size() == 8);
    const auto thickLines = thick(lines(list, Role::Barline));
    REQUIRE(thickLines.size() == 4);
    const auto times = glyphs(list, Role::TimeSignature);
    REQUIRE(times.size() == 2);
    CHECK(left(thickLines[0]) > right(times[0]));
    // Back-to-back repeats share their thick line.
    CHECK_THAT(left(thickLines[2]), WithinAbs(left(thickLines[1]), kEpsilon));

    SECTION("a line starting with a start repeat") {
        const DisplayList second = layout(song, {.firstMeasure = 1});
        const auto keys = glyphs(second, Role::KeySignature);
        REQUIRE(keys.size() == 2);
        const auto barlines = lines(second, Role::Barline);
        REQUIRE(barlines.size() >= 2);
        CHECK(barlines[0].thickness == bravura().defaults().thickBarlineThickness);
        CHECK(left(barlines[0]) > right(keys[1]));
        CHECK(glyphs(second, Role::Repeat).size() == 4);
    }
}

TEST_CASE("Repeat layout: a start repeat follows a key change", "[notation][repeats]") {
    Song song = quarters(2);
    song.measureHeader(1).keySignature = KeySignature(-3, KeyMode::Major);
    song.measureHeader(1).repeatStart = true;
    const DisplayList list = layout(song);
    const auto keys = glyphs(list, Role::KeySignature);
    REQUIRE(keys.size() == 3);
    const auto barlines = lines(list, Role::Barline);
    REQUIRE(barlines.size() == 5); // thin, thick and thin, final thin and thick
    CHECK(barlines[0].thickness == bravura().defaults().thinBarlineThickness);
    CHECK(right(barlines[0]) < left(keys[0]));
    CHECK(left(barlines[1]) > right(keys[2]));
}

TEST_CASE("Repeat layout: a count above end repeats played more than twice",
          "[notation][repeats]") {
    Song song = quarters(2);
    song.measureHeader(0).repeatTimes = 3;
    const DisplayList list = layout(song);
    const auto counts = texts(list, Role::Repeat);
    REQUIRE(counts.size() == 1);
    CHECK(counts[0].text == "x3");
    CHECK(counts[0].measure == 0);
    CHECK(counts[0].align == TextAlign::Right);
    const auto barlines = lines(list, Role::Barline);
    REQUIRE(barlines.size() == 4);
    CHECK_THAT(counts[0].origin.x, WithinAbs(right(barlines[1]), kEpsilon));
    CHECK(counts[0].origin.y < 0.0);
    CHECK(list.top < counts[0].origin.y - 0.5 * counts[0].size);
}

TEST_CASE("Repeat layout: alternate endings get numbered brackets", "[notation][repeats]") {
    // |: 0 |1.2. 1 :|3. 2 | 3 ||, the repeat played three times
    Song song = quarters(4);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).repeatTimes = 3;
    song.measureHeader(1).alternateEndings = 0b011;
    song.measureHeader(2).alternateEndings = 0b100;
    const DisplayList list = layout(song);

    const auto labels = texts(list, Role::Volta);
    REQUIRE(labels.size() == 2);
    CHECK(labels[0].text == "1., 2.");
    CHECK(labels[0].measure == 1);
    CHECK(labels[0].align == TextAlign::Left);
    CHECK(labels[1].text == "3.");
    CHECK(labels[1].measure == 2);

    // The first ending closes at its end repeat; the last one stays open.
    const auto brackets = lines(list, Role::Volta);
    const auto hooks = verticals(brackets);
    const auto tops = verticals(brackets, false);
    REQUIRE(hooks.size() == 3);
    REQUIRE(tops.size() == 2);
    const double y = tops[0].from.y;
    CHECK(tops[1].from.y == y);
    CHECK(tops[0].to.y == y);
    CHECK(y < -1.0);
    CHECK(list.top <= y - tops[0].thickness / 2 + kEpsilon);
    for (const LineItem& hook : hooks) {
        CHECK(std::min(hook.from.y, hook.to.y) == y);
        CHECK(std::max(hook.from.y, hook.to.y) < 0.0); // above the staff
    }
    const auto& first = list.measures[1];
    const auto& second = list.measures[2];
    CHECK(tops[0].from.x >= first.x - kEpsilon);
    CHECK(tops[0].to.x <= first.x + first.width + kEpsilon);
    CHECK(tops[1].from.x >= second.x - kEpsilon);
    CHECK(hooks[0].from.x == tops[0].from.x);
    CHECK(hooks[1].from.x == tops[0].to.x);
    CHECK(hooks[2].from.x == tops[1].from.x);
    // Numbers inside the brackets, above the staff and the notes.
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(labels[i].origin.x > tops[i].from.x);
        CHECK(labels[i].origin.y > y);
        CHECK(labels[i].origin.y < 0.0);
    }
    // The count goes with the end repeat, not the bracket.
    CHECK(texts(list, Role::Repeat).size() == 1);

    SECTION("an ending over two measures is one bracket") {
        Song longer = quarters(4);
        longer.measureHeader(0).repeatStart = true;
        longer.measureHeader(1).alternateEndings = 0b01;
        longer.measureHeader(2).alternateEndings = 0b01;
        longer.measureHeader(2).repeatTimes = 2;
        longer.measureHeader(3).alternateEndings = 0b10;
        const DisplayList two = layout(longer);
        const auto numbers = texts(two, Role::Volta);
        REQUIRE(numbers.size() == 2);
        CHECK(numbers[0].text == "1.");
        CHECK(numbers[1].text == "2.");
        CHECK(numbers[1].measure == 3);
        const auto spans = verticals(lines(two, Role::Volta), false);
        REQUIRE(spans.size() == 2);
        CHECK(spans[0].from.x < two.measures[1].x + two.measures[1].width);
        CHECK(spans[0].to.x > two.measures[2].x);
        CHECK(spans[0].to.x <= two.measures[2].x + two.measures[2].width + kEpsilon);
    }
    SECTION("an ending starts at its barline, before signature changes") {
        Song changed = song;
        changed.measureHeader(2).keySignature = KeySignature(3, KeyMode::Major);
        changed.measureHeader(3).keySignature = KeySignature(3, KeyMode::Major);
        const DisplayList withKey = layout(changed);
        const auto keys = glyphs(withKey, Role::KeySignature);
        REQUIRE(keys.size() == 3);
        const auto spans = verticals(lines(withKey, Role::Volta), false);
        REQUIRE(spans.size() == 2);
        CHECK(spans[1].from.x < left(keys[0]));
        CHECK(spans[1].from.x > withKey.measures[1].x + withKey.measures[1].width);
    }
    SECTION("an ending broken across lines") {
        Song longer = quarters(4);
        longer.measureHeader(1).alternateEndings = 0b01;
        longer.measureHeader(2).alternateEndings = 0b01;
        longer.measureHeader(2).repeatTimes = 2;
        // The first line ends inside the ending: number and start hook, open end.
        const DisplayList before = layout(longer, {.firstMeasure = 0, .measureCount = 2});
        CHECK(texts(before, Role::Volta).size() == 1);
        CHECK(verticals(lines(before, Role::Volta)).size() == 1);
        CHECK(verticals(lines(before, Role::Volta), false).size() == 1);
        // The next line goes on without a number or a start hook and closes it.
        const DisplayList after = layout(longer, {.firstMeasure = 2});
        CHECK(texts(after, Role::Volta).empty());
        const auto rest = lines(after, Role::Volta);
        REQUIRE(verticals(rest).size() == 1);
        REQUIRE(verticals(rest, false).size() == 1);
        CHECK(verticals(rest)[0].from.x == verticals(rest, false)[0].to.x);
        CHECK(verticals(rest, false)[0].from.x < after.measures[0].x + kEpsilon);
    }
}

TEST_CASE("Repeat layout: systems with repeats still fill the page width", "[notation][repeats]") {
    Song song = quarters(16);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(3).repeatTimes = 2;
    song.measureHeader(4).repeatStart = true;
    song.measureHeader(4).keySignature = KeySignature(1, KeyMode::Major);
    song.measureHeader(5).alternateEndings = 0b01;
    song.measureHeader(5).repeatTimes = 2;
    song.measureHeader(6).alternateEndings = 0b10;
    song.measureHeader(7).repeatStart = true;
    song.measureHeader(8).repeatTimes = 4;
    song.measureHeader(9).repeatStart = true;
    song.measureHeader(11).repeatStart = true;
    song.measureHeader(11).repeatTimes = 2;
    song.measureHeader(12).repeatStart = true;
    song.measureHeader(14).repeatTimes = 2;
    const StaffSelection standard{};
    for (const std::size_t perSystem : {std::size_t{0}, std::size_t{2}, std::size_t{3}}) {
        INFO("measures per system " << perSystem);
        PageLayoutOptions options;
        options.width = 60.0;
        options.height = 0.0;
        options.margin = 5.0;
        options.measuresPerSystem = perSystem;
        options.credits = false;
        const auto pages = layoutPages(song, std::span(&standard, 1), bravura(), options);
        REQUIRE(pages.size() == 1);
        const auto& systems = pages.front().systems;
        REQUIRE(systems.size() >= 3);
        for (std::size_t s = 0; s + 1 < systems.size(); ++s) {
            INFO("system " << s);
            CHECK_THAT(systems[s].width, WithinAbs(50.0, kEpsilon));
        }
    }
}
