// SPDX-License-Identifier: AGPL-3.0-or-later
// Painting benchmarks: hidden from CTest, run by `python scripts/elara.py bench`.
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include "../bench/bench_songs.hpp"
#include "score_painter.hpp"
#include <QString>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <utility>

TEST_CASE("Painting benchmarks", "[!benchmark][app][.]") {
    const std::filesystem::path fonts = ELARA_FONT_DIR;
    auto metrics = elara::notation::EngravingFont::fromMetadataFile(fonts / "Bravura.json");
    REQUIRE(metrics.has_value());
    const elara::app::ScorePainter painter(elara::app::ScoreFonts::fromDirectory(
        QString::fromStdU16String(fonts.generic_u16string())));
    const auto song = elara::bench::largeSong();

    const std::array<std::size_t, 1> track{0};
    const auto both = elara::notation::selectTracks(song, track, elara::notation::TrackView::Both);
    const auto pages = elara::notation::layoutPages(
        song, both, *metrics, elara::notation::pageOptions(elara::notation::kA4));
    const auto& page = pages.at(1);
    std::printf("a page of notation and tablature: %zu glyphs, %zu lines, %zu texts\n",
                page.glyphs.size(), page.lines.size(), page.texts.size());

    // 100 % on a 96 dpi screen is 6.6 pixels per staff space.
    BENCHMARK("paint an A4 page of notation and tablature at 100 %") {
        return painter.render(page, 6.6);
    };
    BENCHMARK("the same on a high-density screen (2 device pixels per pixel)") {
        return painter.render(page, 6.6, 2.0);
    };
    BENCHMARK("the same at 200 % on a high-density screen") {
        return painter.render(page, 13.2, 2.0);
    };

    std::vector<std::size_t> all;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        all.push_back(t);
    }
    const auto score = elara::notation::selectTracks(song, all, elara::notation::TrackView::Both);
    const auto scorePages = elara::notation::layoutPages(
        song, score, *metrics, elara::notation::pageOptions(elara::notation::kA4));
    const auto& full = scorePages.at(1);
    std::printf("a page of all 20 staves: %zu glyphs, %zu lines, %zu texts\n", full.glyphs.size(),
                full.lines.size(), full.texts.size());
    BENCHMARK("paint an A4 page of the 20-staff score at 100 %, high density") {
        return painter.render(full, 6.6, 2.0);
    };
}
