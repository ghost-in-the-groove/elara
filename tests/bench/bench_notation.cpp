// SPDX-License-Identifier: AGPL-3.0-or-later
// Benchmarks for elara_notation: laying out staves and writing them as SVG.
#include <elara/core/entry.hpp>
#include <elara/io/compression.hpp>
#include <elara/notation/caret.hpp>
#include <elara/notation/font_outlines.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/pdf.hpp>
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
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using elara::notation::EngravingFont;
using elara::notation::layoutPages;
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

// The bytes a display list holds: its vectors by capacity, with what their
// items hold (onsets, text beyond the small-string buffer, polygon points).
template <class T>
std::size_t heldBytes(const std::vector<T>& items) {
    return items.capacity() * sizeof(T);
}

std::size_t geometryBytes(const elara::notation::DisplayList& list) {
    std::size_t bytes = sizeof(list) + heldBytes(list.systems) + heldBytes(list.staves) +
                        heldBytes(list.measures) + heldBytes(list.columns) +
                        heldBytes(list.glyphs) + heldBytes(list.lines) + heldBytes(list.beams) +
                        heldBytes(list.texts) + heldBytes(list.curves) + heldBytes(list.polygons);
    for (const auto& measure : list.measures) {
        bytes += heldBytes(measure.onsets);
    }
    const std::size_t small = std::string().capacity();
    for (const auto& text : list.texts) {
        bytes += text.text.capacity() > small ? text.text.capacity() + 1 : 0;
    }
    for (const auto& polygon : list.polygons) {
        bytes += heldBytes(polygon.points);
    }
    return bytes;
}

std::size_t geometryBytes(const std::vector<elara::notation::DisplayList>& pages) {
    std::size_t bytes = 0;
    for (const auto& page : pages) {
        bytes += geometryBytes(page);
    }
    return bytes;
}

} // namespace

TEST_CASE("Notation benchmarks", "[benchmark][notation]") {
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

    // Pages (ADR-0014): line breaking and justification on top of the layout.
    elara::notation::PageLayoutOptions pages;
    const auto paged = layoutPages(song, both, font, pages);
    std::size_t systems = 0;
    for (const auto& page : paged) {
        systems += page.systems.size();
    }
    std::printf("notation and tablature on A4: %zu systems on %zu pages, %.1f MB of geometry\n",
                systems, paged.size(), static_cast<double>(geometryBytes(paged)) / 1e6);
    BENCHMARK("lay out notation and tablature of one track on A4 pages") {
        return layoutPages(song, both, font, pages);
    };
    pages.threads = 1;
    BENCHMARK("the same on one thread") {
        return layoutPages(song, both, font, pages);
    };

    // Tempo context (C03 in the optimization study): a tempo change at the start
    // of every measure, every other one gradual, is the worst case for looking up
    // the tempo in force before a system and the end of each ramp.
    elara::core::Song changing = song;
    for (std::size_t m = 0; m < changing.measureCount(); ++m) {
        changing.measureHeader(m).tempoChanges.push_back(
            {.tempo = elara::core::Tempo(100.0 + static_cast<double>(m % 40)),
             .gradual = m % 2 == 0});
    }
    pages.threads = 0;
    BENCHMARK("the same, a tempo change in every measure") {
        return layoutPages(changing, both, font, pages);
    };

    // Keyboard entry (ADR-0032): the app looks for the caret on every page
    // after each move; the last measure is the worst case. Then a click.
    const elara::core::Caret last{.measure = song.measureCount() - 1, .string = 3};
    BENCHMARK("find the caret on the pages of one track (last measure)") {
        for (const auto& page : paged) {
            if (auto box = elara::notation::caretBox(page, song, last, font)) {
                return box;
            }
        }
        return std::optional<elara::notation::Rect>{};
    };
    const auto& lastPage = paged.back();
    const auto box = elara::notation::caretBox(lastPage, song, last, font);
    REQUIRE(box.has_value());
    const elara::notation::Point centre{box->x + box->width / 2, box->y + box->height / 2};
    BENCHMARK("pick the caret a click on a page points at") {
        return elara::notation::caretAt(lastPage, song, centre, elara::core::Caret{}, font);
    };
    std::vector<StaffSelection> score;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        score.push_back({.track = t});
    }
    {
        const auto all = layoutPages(song, score, font, pages);
        std::printf("all 20 staves on A4: %zu pages, %.1f MB of geometry\n", all.size(),
                    static_cast<double>(geometryBytes(all)) / 1e6);
    }
    pages.threads = 0;
    BENCHMARK("lay out all 20 staves of the large song on A4 pages") {
        return layoutPages(song, score, font, pages);
    };
    pages.threads = 1;
    BENCHMARK("the same on one thread ") {
        return layoutPages(song, score, font, pages);
    };

    // Incremental relayout (ADR-0052): each call lays out the other of two
    // versions of the song a note apart (measure 250 of the first track), as
    // the app does after an edit and its undo.
    pages.threads = 0;
    auto before = std::make_shared<const elara::core::Song>(song);
    elara::core::Song changed = song;
    auto& note = changed.track(0).staff(0).measure(250).voices[0].beats[0].notes.front();
    note.pitch = elara::core::Pitch::fromMidi(note.pitch.midiNumber() + 1);
    auto after = std::make_shared<const elara::core::Song>(std::move(changed));
    const auto relayout = [&](std::span<const StaffSelection> staves, const char* what) {
        elara::notation::PageLayoutCache cache;
        bool flip = false;
        (void)cache.layout(before, staves, font, pages);
        BENCHMARK(what) {
            flip = !flip;
            return cache.layout(flip ? after : before, staves, font, pages);
        };
        const auto reuse = cache.lastReuse();
        std::printf("%s: %zu of %zu measures measured, %zu of %zu systems laid out\n", what,
                    reuse.measured, reuse.measures, reuse.laidOut, reuse.systems);
    };
    relayout(both, "lay out one track on A4 pages again after an edit");
    relayout(score, "lay out all 20 staves on A4 pages again after an edit");
}

// PDF (ADR-0074): the font's outlines read once, then the A4 pages of one
// track (notation and tablature) written plain and compressed, as the app and
// the CLI write them, on every core and on one thread.
TEST_CASE("PDF benchmarks", "[benchmark][notation][pdf]") {
    const auto song = elara::bench::largeSong();
    const std::array<StaffSelection, 2> both{StaffSelection{.view = StaffView::Standard},
                                             StaffSelection{.view = StaffView::Tablature}};
    const auto paged = layoutPages(song, both, bravura(), {});
    const auto otf = std::filesystem::path(ELARA_FONT_DIR) / "Bravura.otf";
    BENCHMARK("read the outlines of the glyphs from Bravura.otf") {
        return elara::notation::FontOutlines::fromFile(otf);
    };
    const auto outlines = elara::notation::FontOutlines::fromFile(otf);
    REQUIRE(outlines.has_value());
    elara::notation::PdfOptions compressed{.compress = elara::io::zlibCompress};
    std::printf("%zu A4 pages as PDF: %zu bytes, %zu compressed\n", paged.size(),
                elara::notation::toPdf(paged, *outlines).size(),
                elara::notation::toPdf(paged, *outlines, compressed).size());
    BENCHMARK("write those pages as PDF") {
        return elara::notation::toPdf(paged, *outlines);
    };
    BENCHMARK("write those pages as PDF, compressed") {
        return elara::notation::toPdf(paged, *outlines, compressed);
    };
    compressed.threads = 1;
    BENCHMARK("the same on one thread") {
        return elara::notation::toPdf(paged, *outlines, compressed);
    };
    BENCHMARK("write those pages as PDF, on one thread") {
        return elara::notation::toPdf(paged, *outlines, {.threads = 1});
    };
}
