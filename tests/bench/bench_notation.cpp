// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmarks for elara_notation: laying out staves and writing them as SVG.
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/svg.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>
#include <utility>

using elara::notation::EngravingFont;
using elara::notation::layoutStaff;
using elara::notation::layoutSystem;
using elara::notation::StaffSelection;
using elara::notation::StaffView;

namespace {

const EngravingFont& bravura() {
    static const EngravingFont font = [] {
        auto loaded =
            EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

} // namespace

TEST_CASE("Notation benchmarks", "[!benchmark][notation]") {
    const auto song = elara::bench::largeSong();
    const EngravingFont& font = bravura();
    const auto list = layoutStaff(song, 0, 0, font);
    std::printf("one staff of the large song: %zu glyphs, %zu lines, %zu beams, %.0f staff "
                "spaces wide\n",
                list.glyphs.size(), list.lines.size(), list.beams.size(), list.width);

    BENCHMARK("load the Bravura metadata") {
        return EngravingFont::fromMetadataFile(std::filesystem::path(ELARA_FONT_DIR) /
                                               "Bravura.json");
    };
    BENCHMARK("lay out one 500-measure staff (4000 two-note chords)") {
        return layoutStaff(song, 0, 0, font);
    };
    BENCHMARK("lay out 8 measures of that staff (a screenful)") {
        return layoutStaff(song, 0, 0, font, {.firstMeasure = 100, .measureCount = 8});
    };
    BENCHMARK("lay out all 20 staves of the large song") {
        std::size_t items = 0;
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            items += layoutStaff(song, t, 0, font).glyphs.size();
        }
        return items;
    };
    BENCHMARK("write the 500-measure staff as SVG") {
        return elara::notation::toSvg(list);
    };

    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const auto system = layoutSystem(song, both, font);
    std::printf("notation and tablature of one track: %zu glyphs, %zu lines, %zu texts\n",
                system.glyphs.size(), system.lines.size(), system.texts.size());
    BENCHMARK("lay out the tablature of one 500-measure staff") {
        const StaffSelection tab{.view = StaffView::Tablature};
        return layoutSystem(song, std::span(&tab, 1), font);
    };
    BENCHMARK("lay out notation and tablature of one 500-measure staff") {
        return layoutSystem(song, both, font);
    };
    BENCHMARK("lay out notation and tablature of 8 measures") {
        return layoutSystem(song, both, font, {.firstMeasure = 100, .measureCount = 8});
    };
    BENCHMARK("write notation and tablature of the staff as SVG") {
        return elara::notation::toSvg(system);
    };
}
