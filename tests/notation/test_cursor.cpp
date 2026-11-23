// SPDX-License-Identifier: AGPL-3.0-or-later
// Where the playback cursor stands on laid-out pages.
#include <elara/notation/cursor.hpp>
#include <elara/notation/display_list.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;
using elara::notation::cursorAt;
using elara::notation::DisplayList;
using elara::notation::MeasureBox;
using elara::notation::MeasureOnset;

namespace {

constexpr double kEpsilon = 1e-9;

// Two pages: measures 0 and 1 on one system of the first page, measure 2 on
// the second page.
std::vector<DisplayList> pages() {
    DisplayList first;
    first.measures = {MeasureBox{.index = 0, .x = 10.0, .width = 40.0, .y = 20.0, .height = 12.0},
                      MeasureBox{.index = 1, .x = 50.0, .width = 30.0, .y = 20.0, .height = 12.0}};
    DisplayList second;
    second.measures = {MeasureBox{.index = 2, .x = 10.0, .width = 70.0, .y = 8.0, .height = 4.0}};
    return {first, second};
}

} // namespace

TEST_CASE("cursorAt: through a measure in proportion to the time played", "[notation][cursor]") {
    const auto all = pages();
    const auto start = cursorAt(all, 1, 0.0);
    REQUIRE(start.has_value());
    CHECK(start->page == 0);
    CHECK_THAT(start->x, WithinAbs(50.0, kEpsilon));
    CHECK_THAT(start->y, WithinAbs(20.0, kEpsilon));
    CHECK_THAT(start->height, WithinAbs(12.0, kEpsilon));

    const auto middle = cursorAt(all, 0, 0.25);
    REQUIRE(middle.has_value());
    CHECK_THAT(middle->x, WithinAbs(20.0, kEpsilon));

    const auto later = cursorAt(all, 2, 0.5);
    REQUIRE(later.has_value());
    CHECK(later->page == 1);
    CHECK_THAT(later->x, WithinAbs(45.0, kEpsilon));
    CHECK_THAT(later->y, WithinAbs(8.0, kEpsilon));
}

TEST_CASE("cursorAt: fractions outside the measure stay at its ends", "[notation][cursor]") {
    const auto all = pages();
    CHECK_THAT(cursorAt(all, 0, -0.5)->x, WithinAbs(10.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 1.5)->x, WithinAbs(50.0, kEpsilon));
}

// A measure of a half note at x 14 and two quarters at 30 and 38, from x 10
// to 50: the cursor reaches each note as it starts, and the barline as the
// measure ends.
TEST_CASE("cursorAt: from note to note through a measure", "[notation][cursor]") {
    DisplayList page;
    page.measures = {MeasureBox{.index = 0,
                                .x = 10.0,
                                .width = 40.0,
                                .y = 0.0,
                                .height = 4.0,
                                .onsets = {MeasureOnset{.at = 0.0, .x = 14.0},
                                           MeasureOnset{.at = 0.5, .x = 30.0},
                                           MeasureOnset{.at = 0.75, .x = 38.0}}}};
    const std::vector<DisplayList> all{page};
    CHECK_THAT(cursorAt(all, 0, 0.0)->x, WithinAbs(14.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 0.25)->x, WithinAbs(22.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 0.5)->x, WithinAbs(30.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 0.875)->x, WithinAbs(44.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 1.0)->x, WithinAbs(50.0, kEpsilon));
}

// A measure whose first note comes after a rest nobody wrote: the cursor
// walks from the start of the measure to it.
TEST_CASE("cursorAt: from the start of the measure to a late first note", "[notation][cursor]") {
    DisplayList page;
    page.measures = {MeasureBox{.index = 0,
                                .x = 10.0,
                                .width = 40.0,
                                .y = 0.0,
                                .height = 4.0,
                                .onsets = {MeasureOnset{.at = 0.5, .x = 30.0}}}};
    const std::vector<DisplayList> all{page};
    CHECK_THAT(cursorAt(all, 0, 0.25)->x, WithinAbs(20.0, kEpsilon));
    CHECK_THAT(cursorAt(all, 0, 0.75)->x, WithinAbs(40.0, kEpsilon));
}

TEST_CASE("cursorAt: nothing for a measure no page shows", "[notation][cursor]") {
    CHECK_FALSE(cursorAt(pages(), 3, 0.0).has_value());
    CHECK_FALSE(cursorAt({}, 0, 0.0).has_value());
}
