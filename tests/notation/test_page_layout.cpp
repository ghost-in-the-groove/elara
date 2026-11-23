// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::FretPosition;
using elara::core::Instrument;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuning;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::EngravingFont;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::layoutPages;
using elara::notation::layoutSystem;
using elara::notation::PageLayoutOptions;
using elara::notation::Role;
using elara::notation::StaffSelection;
using elara::notation::StaffView;
using elara::notation::SystemBox;
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

Beat note(std::string_view text, NoteValue value = NoteValue::Quarter, bool tied = false) {
    const auto pitch = Pitch::parse(text);
    REQUIRE(pitch.has_value());
    Beat beat{.duration = Duration(value), .notes = {Note{.pitch = *pitch}}};
    beat.notes.front().effects.tiedFromPrevious = tied;
    return beat;
}

// `count` measures of four quarter notes on one treble staff.
Song quarters(std::size_t count, KeySignature key = {}) {
    Song song;
    song.appendMeasures(count);
    for (std::size_t m = 0; m < count; ++m) {
        song.measureHeader(m).keySignature = key;
    }
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < count; ++m) {
        song.track(0).staff(0).measure(m).voices[0].beats = {note("C5"), note("D5"), note("E5"),
                                                             note("F5")};
    }
    return song;
}

constexpr StaffSelection kStandard{};

std::vector<DisplayList> pages(const Song& song, PageLayoutOptions options) {
    return layoutPages(song, std::span(&kStandard, 1), bravura(), options);
}

PageLayoutOptions narrow() {
    return PageLayoutOptions{.width = 60.0, .height = 0.0, .margin = 5.0, .credits = false};
}

std::vector<SystemBox> systemsOf(const std::vector<DisplayList>& all) {
    std::vector<SystemBox> systems;
    for (const DisplayList& page : all) {
        systems.insert(systems.end(), page.systems.begin(), page.systems.end());
    }
    return systems;
}

template <typename Item>
std::vector<Item> inSystem(const std::vector<Item>& items, const SystemBox& system, Role role) {
    std::vector<Item> found;
    for (const Item& item : items) {
        if (item.role == role && item.measure >= system.firstMeasure &&
            item.measure < system.firstMeasure + system.measureCount) {
            found.push_back(item);
        }
    }
    return found;
}

std::vector<TextItem> textsWithRole(const DisplayList& page, Role role) {
    std::vector<TextItem> found;
    std::ranges::copy_if(page.texts, std::back_inserter(found),
                         [role](const TextItem& text) { return text.role == role; });
    return found;
}

} // namespace

TEST_CASE("Page layout: systems fill the width between the margins", "[notation]") {
    const auto all = pages(quarters(16), narrow());
    REQUIRE(all.size() == 1);
    const DisplayList& page = all.front();
    CHECK(page.width == 60.0);
    CHECK(page.top == 0.0);
    const auto& systems = page.systems;
    REQUIRE(systems.size() >= 3);
    std::size_t next = 0;
    for (std::size_t s = 0; s < systems.size(); ++s) {
        const SystemBox& system = systems[s];
        CHECK(system.firstMeasure == next);
        CHECK(system.measureCount >= 1);
        next += system.measureCount;
        CHECK_THAT(system.x, WithinAbs(5.0, kEpsilon));
        if (s + 1 < systems.size()) {
            CHECK_THAT(system.width, WithinAbs(50.0, kEpsilon));
            CHECK(system.bottom < systems[s + 1].top);
        }
        CHECK(system.top >= 5.0 - kEpsilon);
    }
    CHECK(next == 16);
    // Every measure once, in order, inside its system.
    REQUIRE(page.measures.size() == 16);
    for (std::size_t m = 0; m < 16; ++m) {
        CHECK(page.measures[m].index == m);
    }
    // Every staff line spans its system.
    for (const auto& staff : page.staves) {
        CHECK_THAT(staff.x, WithinAbs(5.0, kEpsilon));
    }
    CHECK(page.staves.size() == systems.size());
}

TEST_CASE("Page layout: stretched systems keep the rhythm even", "[notation]") {
    const auto all = pages(quarters(16), narrow());
    const DisplayList& page = all.front();
    const SystemBox& system = page.systems.front();
    const auto heads = inSystem(page.glyphs, system, Role::Notehead);
    REQUIRE(heads.size() == 4 * system.measureCount);
    const double step = heads[1].origin.x - heads[0].origin.x;
    for (std::size_t i = 1; i < 4; ++i) {
        CHECK_THAT(heads[i].origin.x - heads[i - 1].origin.x, WithinAbs(step, kEpsilon));
    }
    // Wider than the natural spacing of layoutSystem().
    const auto line = layoutSystem(quarters(16), std::span(&kStandard, 1), bravura());
    const auto natural = std::ranges::find(line.glyphs, Role::Notehead, &GlyphItem::role);
    CHECK(step > (natural + 1)->origin.x - natural->origin.x);
}

TEST_CASE("Page layout: later systems repeat clef and key but not the time signature",
          "[notation]") {
    const auto all = pages(quarters(16, KeySignature(2, KeyMode::Major)), narrow());
    const auto systems = systemsOf(all);
    REQUIRE(systems.size() >= 2);
    const DisplayList& page = all.front();
    for (std::size_t s = 0; s < systems.size(); ++s) {
        INFO("system " << s);
        CHECK(inSystem(page.glyphs, systems[s], Role::Clef).size() == 1);
        CHECK(inSystem(page.glyphs, systems[s], Role::KeySignature).size() == 2);
        CHECK(inSystem(page.glyphs, systems[s], Role::TimeSignature).size() == (s == 0 ? 2 : 0));
    }
}

TEST_CASE("Page layout: a time change at a system's start goes in its header", "[notation]") {
    Song song = quarters(16);
    PageLayoutOptions options = narrow();
    options.measuresPerSystem = 4;
    for (std::size_t m = 4; m < 16; ++m) {
        song.measureHeader(m).timeSignature = TimeSignature(3, 4);
        song.track(0).staff(0).measure(m).voices[0].beats.pop_back();
    }
    const auto all = pages(song, options);
    const auto systems = systemsOf(all);
    REQUIRE(systems.size() == 4);
    const DisplayList& page = all.front();
    CHECK(inSystem(page.glyphs, systems[1], Role::TimeSignature).size() == 2);
    CHECK(inSystem(page.glyphs, systems[2], Role::TimeSignature).empty());
    // In the header: before the first note of the system.
    const auto time = inSystem(page.glyphs, systems[1], Role::TimeSignature).front();
    const auto head = inSystem(page.glyphs, systems[1], Role::Notehead).front();
    CHECK(time.origin.x < head.origin.x);
}

TEST_CASE("Page layout: ties across systems are drawn in two halves", "[notation]") {
    Song song = quarters(8);
    for (std::size_t m = 0; m + 1 < 8; ++m) {
        auto& beats = song.track(0).staff(0).measure(m + 1).voices[0].beats;
        beats.front() = note("F5", NoteValue::Quarter, true);
    }
    PageLayoutOptions options = narrow();
    options.measuresPerSystem = 4;
    const auto all = pages(song, options);
    const auto systems = systemsOf(all);
    REQUIRE(systems.size() == 2);
    const DisplayList& page = all.front();
    std::vector<CurveItem> first = inSystem(page.curves, systems[0], Role::Tie);
    std::vector<CurveItem> second = inSystem(page.curves, systems[1], Role::Tie);
    // Three ties inside each system plus one half at the end of the first and
    // one at the start of the second.
    REQUIRE(first.size() == 4);
    REQUIRE(second.size() == 4);
    const auto& out =
        *std::ranges::max_element(first, {}, [](const CurveItem& c) { return c.to.x; });
    const auto& in =
        *std::ranges::min_element(second, {}, [](const CurveItem& c) { return c.from.x; });
    CHECK(out.to.x <= systems[0].x + systems[0].width + kEpsilon);
    CHECK(out.to.x > systems[0].x + systems[0].width - 2.0);
    const auto clef = inSystem(page.glyphs, systems[1], Role::Clef).front();
    const auto head = inSystem(page.glyphs, systems[1], Role::Notehead).front();
    CHECK(in.from.x > clef.origin.x);
    CHECK(in.to.x < head.origin.x);
    CHECK_THAT(in.from.y, WithinAbs(in.to.y, kEpsilon));
}

TEST_CASE("Page layout: systems after the first show their measure number", "[notation]") {
    const auto all = pages(quarters(16), narrow());
    const DisplayList& page = all.front();
    const auto numbers = textsWithRole(page, Role::MeasureNumber);
    REQUIRE(numbers.size() == page.systems.size() - 1);
    for (std::size_t s = 1; s < page.systems.size(); ++s) {
        const SystemBox& system = page.systems[s];
        const TextItem& number = numbers[s - 1];
        CHECK(number.text == std::to_string(system.firstMeasure + 1));
        CHECK(number.measure == system.firstMeasure);
        CHECK(number.origin.y < system.y);
        CHECK(number.origin.y > system.top);
    }
}

TEST_CASE("Page layout: systems are stacked on pages inside the margins", "[notation]") {
    PageLayoutOptions options = narrow();
    options.height = 60.0;
    options.credits = true;
    const auto all = pages(quarters(64), options);
    REQUIRE(all.size() >= 3);
    std::size_t next = 0;
    for (std::size_t p = 0; p < all.size(); ++p) {
        INFO("page " << p);
        const DisplayList& page = all[p];
        CHECK(page.width == 60.0);
        CHECK(page.top == 0.0);
        CHECK(page.bottom == 60.0);
        REQUIRE_FALSE(page.systems.empty());
        for (const SystemBox& system : page.systems) {
            CHECK(system.firstMeasure == next);
            next += system.measureCount;
            CHECK(system.top >= 5.0 - kEpsilon);
            CHECK(system.bottom <= 55.0 + kEpsilon);
        }
        for (const GlyphItem& glyph : page.glyphs) {
            CHECK(glyph.origin.y > 0.0);
            CHECK(glyph.origin.y < 60.0);
        }
        // Page numbers from the second page on.
        const auto credits = textsWithRole(page, Role::Credit);
        if (p > 0) {
            REQUIRE(credits.size() == 1);
            CHECK(credits.front().text == std::to_string(p + 1));
            CHECK(credits.front().origin.y > page.systems.back().bottom);
        }
    }
    CHECK(next == 64);
}

TEST_CASE("Page layout: the first page shows the title and credits", "[notation]") {
    Song song = quarters(8);
    song.info().title = "Night Train";
    song.info().subtitle = "Live";
    song.info().artist = "The Examples";
    song.info().music = "A. Composer";
    PageLayoutOptions options = narrow();
    options.credits = true;
    const auto all = pages(song, options);
    const DisplayList& page = all.front();
    const auto credits = textsWithRole(page, Role::Credit);
    std::vector<std::string> texts;
    for (const TextItem& text : credits) {
        texts.push_back(text.text);
        CHECK(text.origin.y < page.systems.front().top);
        CHECK(text.origin.y > 5.0);
    }
    CHECK(texts ==
          std::vector<std::string>{"Night Train", "Live", "The Examples", "Music by A. Composer"});
    CHECK_THAT(credits[0].origin.x, WithinAbs(30.0, kEpsilon));
    CHECK(credits[0].size > credits[1].size);
    CHECK(credits[3].align == elara::notation::TextAlign::Right);
    // Without credits, the first system starts at the top margin.
    const auto bare = pages(song, narrow());
    CHECK(textsWithRole(bare.front(), Role::Credit).empty());
    CHECK(bare.front().systems.front().top < page.systems.front().top);
}

TEST_CASE("Page layout: a fixed number of measures per system", "[notation]") {
    PageLayoutOptions options = narrow();
    options.measuresPerSystem = 3;
    const auto systems = systemsOf(pages(quarters(10), options));
    REQUIRE(systems.size() == 4);
    CHECK(systems[0].measureCount == 3);
    CHECK(systems[2].measureCount == 3);
    CHECK(systems[3].measureCount == 1);
}

TEST_CASE("Page layout: systems tighten their spacing rather than leave a measure out",
          "[notation]") {
    // Four measures need slightly more than the width at their natural spacing.
    const Song song = quarters(8);
    StaffSelection selection;
    const auto natural = layoutSystem(song, std::span(&selection, 1), bravura(),
                                      {.firstMeasure = 0, .measureCount = 4});
    PageLayoutOptions options = narrow();
    options.width = 10.0 + natural.width * 0.97;
    const auto all = pages(song, options);
    const auto systems = systemsOf(all);
    REQUIRE(systems.size() == 2);
    CHECK(systems[0].measureCount == 4);
    CHECK_THAT(systems[0].width, WithinAbs(natural.width * 0.97, kEpsilon));
}

TEST_CASE("Page layout: a measure wider than the page gets a system of its own", "[notation]") {
    PageLayoutOptions options = narrow();
    options.width = 22.0;
    const auto systems = systemsOf(pages(quarters(3), options));
    REQUIRE(systems.size() == 3);
    for (const SystemBox& system : systems) {
        CHECK(system.measureCount == 1);
        CHECK(system.width > 12.0);
    }
}

TEST_CASE("Page layout: a short last system keeps its natural width", "[notation]") {
    PageLayoutOptions options = narrow();
    options.measuresPerSystem = 4;
    const auto systems = systemsOf(pages(quarters(5), options));
    REQUIRE(systems.size() == 2);
    CHECK(systems[1].width < 25.0);
    options.lastSystemFill = 0.0;
    const auto filled = systemsOf(pages(quarters(5), options));
    CHECK_THAT(filled[1].width, WithinAbs(50.0, kEpsilon));
}

TEST_CASE("Page layout: notation and tablature systems stay together", "[notation]") {
    Song song;
    song.appendMeasures(12);
    song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    for (std::size_t m = 0; m < 12; ++m) {
        auto& beats = song.track(0).staff(0).measure(m).voices[0].beats;
        for (int fret = 0; fret < 4; ++fret) {
            beats.push_back(Beat{
                .duration = Duration(NoteValue::Quarter),
                .notes = {Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(2, fret)),
                               .position = FretPosition{.string = 2,
                                                        .fret = static_cast<std::int8_t>(fret)}}}});
        }
    }
    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    PageLayoutOptions options = narrow();
    options.height = 70.0;
    const auto all = layoutPages(song, both, bravura(), options);
    for (const DisplayList& page : all) {
        REQUIRE(page.staves.size() == 2 * page.systems.size());
        for (std::size_t s = 0; s < page.systems.size(); ++s) {
            CHECK(page.staves[2 * s].view == StaffView::Standard);
            CHECK(page.staves[2 * s + 1].view == StaffView::Tablature);
            CHECK(page.staves[2 * s + 1].y > page.staves[2 * s].y);
            CHECK_THAT(page.staves[2 * s].width, WithinAbs(page.systems[s].width, kEpsilon));
        }
        // A line joins the staves of every system at its start.
        const auto joins =
            std::ranges::count_if(page.lines, [&](const elara::notation::LineItem& line) {
                return line.role == Role::Barline && std::abs(line.from.x - 5.0) < 0.2 &&
                       line.to.y - line.from.y > 8.0;
            });
        CHECK(static_cast<std::size_t>(joins) == page.systems.size());
    }
}

TEST_CASE("Page layout: the first system of several tracks names them", "[notation]") {
    Song song = quarters(16);
    song.addTrack(Track("Bass", Instrument{.name = "Bass"}, {Staff(Clef::Bass)}));
    for (std::size_t m = 0; m < 16; ++m) {
        song.track(1).staff(0).measure(m).voices[0].beats = {note("C3", NoteValue::Whole)};
    }
    const std::array<StaffSelection, 2> staves{StaffSelection{.track = 0},
                                               StaffSelection{.track = 1}};
    const auto all = layoutPages(song, staves, bravura(), narrow());
    const DisplayList& page = all.front();
    REQUIRE(page.systems.size() >= 2);
    const auto names = textsWithRole(page, Role::TrackName);
    REQUIRE(names.size() == 2);
    CHECK(names[0].text == "Part");
    CHECK(names[1].text == "Bass");
    // Left of the staves of the first system, each centred on its staff.
    const auto& first = page.staves[0];
    const auto& second = page.staves[1];
    CHECK(first.x > 5.0 + elara::notation::textWidth("Part", names[0].size));
    CHECK(second.x == first.x);
    CHECK(names[0].align == elara::notation::TextAlign::Right);
    CHECK(names[0].origin.x <= first.x);
    CHECK(names[0].origin.x >= 5.0 + elara::notation::textWidth("Part", names[0].size));
    CHECK(names[0].origin.y > first.y);
    CHECK(names[0].origin.y < first.y + first.height + 1.0);
    CHECK(names[1].origin.y > second.y);
    // The first system still ends at the right margin; later ones start at it.
    CHECK_THAT(page.systems[0].x + page.systems[0].width, WithinAbs(55.0, kEpsilon));
    CHECK_THAT(page.staves[0].x + page.staves[0].width, WithinAbs(55.0, kEpsilon));
    CHECK_THAT(page.staves[2].x, WithinAbs(5.0, kEpsilon));
    // A single track goes unnamed.
    CHECK(textsWithRole(pages(song, narrow()).front(), Role::TrackName).empty());
}

TEST_CASE("Page layout: one thread or many give the same pages", "[notation]") {
    PageLayoutOptions options = narrow();
    options.height = 80.0;
    const Song song = quarters(96, KeySignature(-3, KeyMode::Major));
    options.threads = 1;
    const auto serial = pages(song, options);
    options.threads = 4;
    const auto parallel = pages(song, options);
    CHECK(serial == parallel);
}

TEST_CASE("Page layout: rejected options", "[notation]") {
    const Song song = quarters(4);
    PageLayoutOptions options = narrow();
    options.width = 10.0;
    CHECK_THROWS_AS(pages(song, options), std::invalid_argument);
    options = narrow();
    options.firstMeasure = 4;
    CHECK_THROWS_AS(pages(song, options), std::out_of_range);
    CHECK_THROWS_AS(layoutPages(song, {}, bravura(), narrow()), std::invalid_argument);
}

TEST_CASE("Page layout: paper sizes in staff spaces", "[notation]") {
    const PageLayoutOptions a4 = elara::notation::pageOptions(elara::notation::kA4, 2.0, 10.0);
    CHECK_THAT(a4.width, WithinAbs(105.0, kEpsilon));
    CHECK_THAT(a4.height, WithinAbs(148.5, kEpsilon));
    CHECK_THAT(a4.margin, WithinAbs(5.0, kEpsilon));
}
