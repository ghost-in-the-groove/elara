// SPDX-License-Identifier: AGPL-3.0-or-later
// Render subcommand: staves in standard notation, tablature or both, as the
// notation engine lays them out on one line (ADR-0012) or on pages (ADR-0014),
// printed as display lists or written as SVG.
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/svg.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace elara::cli {

namespace {

using notation::kRoleCount;
using notation::Role;
using notation::roleName;

struct RenderOptions {
    std::string file;
    std::size_t track = 0;
    std::size_t staff = 0;
    std::vector<std::string> tracks; // "all" or indices: every staff of these tracks
    std::size_t first = 0;
    std::size_t count = notation::kAllMeasures;
    std::string view = "standard";
    std::string layout = "line";
    std::string paper = "a4";
    double staffSpace = 1.75; // mm
    double margin = 15.0;     // mm
    double width = 0.0;       // staff spaces, for the scroll layout; 0: the paper's
    std::size_t measuresPerSystem = 0;
    bool noCredits = false;
    std::string svg;
    std::string metadata;
};

std::string point(const notation::Point& p) {
    return std::format("[{},{}]", jsonNumber(p.x), jsonNumber(p.y));
}

std::string_view viewName(notation::StaffView view) {
    return view == notation::StaffView::Tablature ? "tablature" : "standard";
}

std::string_view alignName(notation::TextAlign align) {
    switch (align) {
    case notation::TextAlign::Left:
        return "left";
    case notation::TextAlign::Right:
        return "right";
    case notation::TextAlign::Centre:
        break;
    }
    return "centre";
}

std::array<std::size_t, kRoleCount> countRoles(const notation::DisplayList& list) {
    std::array<std::size_t, kRoleCount> counts{};
    const auto count = [&counts](const auto& items) {
        for (const auto& item : items) {
            ++counts[static_cast<std::size_t>(item.role)];
        }
    };
    count(list.glyphs);
    count(list.lines);
    count(list.texts);
    count(list.curves);
    count(list.polygons);
    counts[static_cast<std::size_t>(Role::Beam)] += list.beams.size();
    return counts;
}

std::string displayListJson(const notation::DisplayList& list) {
    const auto counts = countRoles(list);
    std::string json =
        std::format(R"("width":{},"top":{},"bottom":{},"counts":{{)", jsonNumber(list.width),
                    jsonNumber(list.top), jsonNumber(list.bottom));
    for (std::size_t r = 0; r < kRoleCount; ++r) {
        json += std::format(R"({}"{}":{})", r == 0 ? "" : ",", roleName(static_cast<Role>(r)),
                            counts[r]);
    }
    json += R"(},"systems":[)";
    for (std::size_t i = 0; i < list.systems.size(); ++i) {
        const auto& box = list.systems[i];
        json += std::format(
            R"({}{{"firstMeasure":{},"measureCount":{},"x":{},"y":{},"width":{},"height":{},"top":{},"bottom":{}}})",
            i == 0 ? "" : ",", box.firstMeasure, box.measureCount, jsonNumber(box.x),
            jsonNumber(box.y), jsonNumber(box.width), jsonNumber(box.height), jsonNumber(box.top),
            jsonNumber(box.bottom));
    }
    json += R"(],"staves":[)";
    for (std::size_t i = 0; i < list.staves.size(); ++i) {
        const auto& box = list.staves[i];
        json += std::format(
            R"({}{{"track":{},"staff":{},"view":"{}","x":{},"y":{},"width":{},"height":{},"lines":{}}})",
            i == 0 ? "" : ",", box.track, box.staff, viewName(box.view), jsonNumber(box.x),
            jsonNumber(box.y), jsonNumber(box.width), jsonNumber(box.height), box.lines);
    }
    json += R"(],"measures":[)";
    for (std::size_t i = 0; i < list.measures.size(); ++i) {
        const auto& box = list.measures[i];
        json += std::format(R"({}{{"index":{},"x":{},"width":{},"y":{},"height":{}}})",
                            i == 0 ? "" : ",", box.index, jsonNumber(box.x), jsonNumber(box.width),
                            jsonNumber(box.y), jsonNumber(box.height));
    }
    json += R"(],"columns":[)";
    for (std::size_t i = 0; i < list.columns.size(); ++i) {
        const auto& column = list.columns[i];
        json += std::format(R"({}{{"measure":{},"start":"{}","x":{}}})", i == 0 ? "" : ",",
                            column.measure, column.start.toString(), jsonNumber(column.x));
    }
    json += R"(],"glyphs":[)";
    for (std::size_t i = 0; i < list.glyphs.size(); ++i) {
        const auto& item = list.glyphs[i];
        json += std::format(R"({}{{"glyph":"{}","role":"{}","measure":{},"at":{}}})",
                            i == 0 ? "" : ",", notation::glyphName(item.glyph), roleName(item.role),
                            item.measure, point(item.origin));
    }
    json += R"(],"lines":[)";
    for (std::size_t i = 0; i < list.lines.size(); ++i) {
        const auto& item = list.lines[i];
        const std::string dash =
            item.dash > 0.0 ? std::format(R"(,"dash":{})", jsonNumber(item.dash)) : "";
        json += std::format(R"({}{{"role":"{}","measure":{},"from":{},"to":{},"thickness":{}{}}})",
                            i == 0 ? "" : ",", roleName(item.role), item.measure, point(item.from),
                            point(item.to), jsonNumber(item.thickness), dash);
    }
    json += R"(],"beams":[)";
    for (std::size_t i = 0; i < list.beams.size(); ++i) {
        const auto& item = list.beams[i];
        json += std::format(R"({}{{"measure":{},"left":{},"right":{},"thickness":{}}})",
                            i == 0 ? "" : ",", item.measure, point(item.left), point(item.right),
                            jsonNumber(item.thickness));
    }
    json += R"(],"texts":[)";
    for (std::size_t i = 0; i < list.texts.size(); ++i) {
        const auto& item = list.texts[i];
        json += std::format(
            R"({}{{"text":"{}","role":"{}","measure":{},"at":{},"size":{},"align":"{}","italic":{},"knockout":{}}})",
            i == 0 ? "" : ",", jsonEscape(item.text), roleName(item.role), item.measure,
            point(item.origin), jsonNumber(item.size), alignName(item.align), item.italic,
            item.knockout);
    }
    json += R"(],"curves":[)";
    for (std::size_t i = 0; i < list.curves.size(); ++i) {
        const auto& item = list.curves[i];
        json += std::format(
            R"({}{{"role":"{}","measure":{},"from":{},"control1":{},"control2":{},"to":{},"thickness":{},"endThickness":{}}})",
            i == 0 ? "" : ",", roleName(item.role), item.measure, point(item.from),
            point(item.control1), point(item.control2), point(item.to), jsonNumber(item.thickness),
            jsonNumber(item.endThickness));
    }
    json += R"(],"polygons":[)";
    for (std::size_t i = 0; i < list.polygons.size(); ++i) {
        const auto& item = list.polygons[i];
        std::string points;
        for (const auto& p : item.points) {
            points += (points.empty() ? "" : ",") + point(p);
        }
        json += std::format(R"({}{{"role":"{}","measure":{},"points":[{}]}})", i == 0 ? "" : ",",
                            roleName(item.role), item.measure, points);
    }
    return json + ']';
}

// How the SVG finds the font file: relative to the SVG when possible, so the
// pair can be moved together.
std::string fontUrl(const std::filesystem::path& font, const std::filesystem::path& svg) {
    std::error_code error;
    const std::filesystem::path base = std::filesystem::absolute(svg, error).parent_path();
    const std::filesystem::path relative =
        error ? std::filesystem::path{} : std::filesystem::relative(font, base, error);
    if (error || relative.empty()) {
        return "file:///" + utf8(std::filesystem::absolute(font, error).generic_u8string());
    }
    return utf8(relative.generic_u8string());
}

// "score.svg" for page 2 of several: "score-2.svg".
std::filesystem::path pagePath(const std::filesystem::path& svg, std::size_t page,
                               std::size_t pages) {
    if (pages <= 1 && page == 0) {
        return svg;
    }
    std::filesystem::path path = svg;
    path.replace_filename(utf8(svg.stem().generic_u8string()) + "-" + std::to_string(page + 1) +
                          utf8(svg.extension().generic_u8string()));
    return path;
}

// The staves to lay out, or a usage error.
std::expected<std::vector<notation::StaffSelection>, std::string>
selectStaves(const core::Song& song, const RenderOptions& options) {
    const bool standard = options.view != "tab";
    const bool tablature = options.view != "standard";
    std::vector<notation::StaffSelection> selection;
    if (options.tracks.empty()) {
        if (options.track >= song.trackCount()) {
            return std::unexpected(
                std::format("no track {}: the song has {}", options.track, song.trackCount()));
        }
        const std::size_t staves = song.track(options.track).staffCount();
        if (options.staff >= staves) {
            return std::unexpected(
                std::format("no staff {}: track {} has {}", options.staff, options.track, staves));
        }
        if (tablature && !song.track(options.track).staff(options.staff).isFretted()) {
            return std::unexpected(std::format("staff {} of track {} has no tuning: no tablature",
                                               options.staff, options.track));
        }
        if (standard) {
            selection.push_back({.track = options.track,
                                 .staff = options.staff,
                                 .view = notation::StaffView::Standard});
        }
        if (tablature) {
            selection.push_back({.track = options.track,
                                 .staff = options.staff,
                                 .view = notation::StaffView::Tablature});
        }
        return selection;
    }
    // Whole tracks: tablature where the staff has a tuning, notation otherwise.
    std::vector<std::size_t> tracks;
    for (const std::string& text : options.tracks) {
        if (text == "all") {
            for (std::size_t t = 0; t < song.trackCount(); ++t) {
                tracks.push_back(t);
            }
            continue;
        }
        std::size_t index = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);
        if (error != std::errc{} || end != text.data() + text.size()) {
            return std::unexpected(std::format("--tracks: not a track index: {}", text));
        }
        if (index >= song.trackCount()) {
            return std::unexpected(
                std::format("no track {}: the song has {}", index, song.trackCount()));
        }
        tracks.push_back(index);
    }
    const notation::TrackView view = !tablature  ? notation::TrackView::Standard
                                     : !standard ? notation::TrackView::Tablature
                                                 : notation::TrackView::Both;
    return notation::selectTracks(song, tracks, view);
}

notation::PageLayoutOptions pageLayoutOptions(const RenderOptions& options) {
    notation::PageLayoutOptions page =
        notation::pageOptions(options.paper == "letter" ? notation::kLetter : notation::kA4,
                              options.staffSpace, options.margin);
    page.firstMeasure = options.first;
    page.measureCount = options.count;
    page.measuresPerSystem = options.measuresPerSystem;
    page.credits = !options.noCredits;
    if (options.layout == "scroll") {
        page.height = 0.0;
        if (options.width > 0.0) {
            page.width = options.width;
        }
    }
    return page;
}

int runRender(bool json, const RenderOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(std::format("{}: {}", options.file, song.error()));
    }
    const auto selection = selectStaves(*song, options);
    if (!selection) {
        return usageError(selection.error());
    }
    if (selection->empty()) {
        return usageError("no staves to render");
    }
    if (options.first >= song->measureCount()) {
        return usageError(
            std::format("no measure {}: the song has {}", options.first, song->measureCount()));
    }
    const std::filesystem::path metadata =
        options.metadata.empty() ? defaultFontMetadata() : std::filesystem::path(options.metadata);
    const auto font = notation::EngravingFont::fromMetadataFile(metadata);
    if (!font) {
        return fail(font.error().message);
    }
    const bool paged = options.layout != "line";
    std::vector<notation::DisplayList> pages;
    if (paged) {
        const notation::PageLayoutOptions page = pageLayoutOptions(options);
        if (page.width <= 2 * page.margin) {
            return usageError("the margins leave no room on the page");
        }
        pages = notation::layoutPages(*song, *selection, *font, page);
    } else {
        pages.push_back(
            notation::layoutSystem(*song, *selection, *font,
                                   {.firstMeasure = options.first, .measureCount = options.count}));
    }
    std::vector<std::string> written;
    if (!options.svg.empty()) {
        std::filesystem::path otf = metadata;
        otf.replace_extension(".otf");
        for (std::size_t p = 0; p < pages.size(); ++p) {
            const std::filesystem::path out = options.layout == "page"
                                                  ? pagePath(options.svg, p, pages.size())
                                                  : std::filesystem::path(options.svg);
            notation::SvgOptions svg;
            svg.fontFamily = font->name();
            svg.fontUrl = fontUrl(otf, out);
            if (paged) {
                svg.margin = 0.0; // the page has its own
            }
            std::ofstream file(out, std::ios::binary);
            file << notation::toSvg(pages[p], svg);
            if (!file) {
                return fail(std::format("cannot write {}", utf8(out.generic_u8string())));
            }
            written.push_back(utf8(out.generic_u8string()));
        }
    }
    std::size_t shown = 0;
    std::size_t systems = 0;
    for (const notation::DisplayList& page : pages) {
        shown += page.measures.size();
        systems += page.systems.size();
    }
    std::string staves;
    if (options.tracks.empty()) {
        staves = std::format(R"("track":{},"staff":{})", options.track, options.staff);
    } else {
        staves = R"("tracks":[)";
        std::size_t previous = notation::kAllMeasures;
        bool firstTrack = true;
        for (const notation::StaffSelection& each : *selection) {
            if (each.track != previous) {
                staves += std::format("{}{}", firstTrack ? "" : ",", each.track);
                previous = each.track;
                firstTrack = false;
            }
        }
        staves += ']';
    }
    if (json) {
        std::string svg = "null";
        if (!written.empty()) {
            if (options.layout == "page") {
                svg = "[";
                for (std::size_t i = 0; i < written.size(); ++i) {
                    svg += std::format(R"({}"{}")", i == 0 ? "" : ",", jsonEscape(written[i]));
                }
                svg += ']';
            } else {
                svg = std::format("\"{}\"", jsonEscape(options.svg));
            }
        }
        std::string body;
        if (paged) {
            body = R"("pages":[)";
            for (std::size_t p = 0; p < pages.size(); ++p) {
                body += std::format("{}{{{}}}", p == 0 ? "" : ",", displayListJson(pages[p]));
            }
            body += ']';
        } else {
            body = displayListJson(pages.front());
        }
        std::cout
            << std::format(
                   R"({{"file":"{}",{},"first":{},"count":{},"svg":{},"view":"{}","layout":"{}",{}}})",
                   jsonEscape(options.file), staves, options.first, shown, svg, options.view,
                   options.layout, body)
            << '\n';
        return kExitOk;
    }
    const std::string what = options.tracks.empty()
                                 ? std::format("track {}, staff {}", options.track, options.staff)
                                 : std::format("{} staves", selection->size());
    if (paged) {
        std::cout << std::format("{}: {} ({}), measures {} to {} of {}: {} {} on {} {}\n",
                                 options.file, what, options.view, options.first,
                                 options.first + shown - 1, song->measureCount(), systems,
                                 systems == 1 ? "system" : "systems", pages.size(),
                                 pages.size() == 1 ? "page" : "pages");
    } else {
        const notation::DisplayList& list = pages.front();
        std::cout << std::format("{}: {} ({}), measures {} to {} of {}: {} x {} staff spaces\n",
                                 options.file, what, options.view, options.first,
                                 options.first + shown - 1, song->measureCount(),
                                 jsonNumber(list.width), jsonNumber(list.bottom - list.top));
    }
    std::array<std::size_t, kRoleCount> counts{};
    for (const notation::DisplayList& page : pages) {
        const auto pageCounts = countRoles(page);
        for (std::size_t r = 0; r < kRoleCount; ++r) {
            counts[r] += pageCounts[r];
        }
    }
    for (std::size_t r = 0; r < kRoleCount; ++r) {
        if (counts[r] > 0) {
            std::cout << std::format("  {} {}\n", roleName(static_cast<Role>(r)), counts[r]);
        }
    }
    for (const std::string& file : written) {
        std::cout << std::format("saved {}\n", file);
    }
    return kExitOk;
}

} // namespace

void addRenderCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<RenderOptions>();
    auto* render = app.add_subcommand(
        "render", "Lay out staves of a song in standard notation, tablature or both, on one line "
                  "or on pages: print the display list (glyphs, lines, beams, text and curves in "
                  "staff spaces) or write it as SVG");
    render->add_option("file", options->file, "A .elara file or any file import reads")->required();
    auto* track = render->add_option("--track", options->track, "Track index (0 = first)")
                      ->capture_default_str();
    auto* staff = render->add_option("--staff", options->staff, "Staff index in the track")
                      ->capture_default_str();
    render
        ->add_option("--tracks", options->tracks,
                     "Whole tracks instead of one staff: indices (0,2) or all")
        ->delimiter(',')
        ->excludes(track)
        ->excludes(staff);
    render->add_option("--first", options->first, "First measure index")->capture_default_str();
    render->add_option("--count", options->count, "Number of measures (default: to the end)");
    render
        ->add_option("--view", options->view,
                     "standard, tab (tablature of a fretted staff) or both, notation above tab")
        ->check(CLI::IsMember({"standard", "tab", "both"}))
        ->capture_default_str();
    render
        ->add_option("--layout", options->layout,
                     "line (one line, natural spacing), page (systems on pages) or scroll (systems "
                     "on one endless page)")
        ->check(CLI::IsMember({"line", "page", "scroll"}))
        ->capture_default_str();
    render->add_option("--paper", options->paper, "Page size: a4 or letter")
        ->check(CLI::IsMember({"a4", "letter"}))
        ->capture_default_str();
    render->add_option("--staff-space", options->staffSpace, "Staff space in mm")
        ->check(CLI::Range(0.5, 10.0))
        ->capture_default_str();
    render->add_option("--margin", options->margin, "Page margins in mm")
        ->check(CLI::Range(0.0, 100.0))
        ->capture_default_str();
    render
        ->add_option("--width", options->width,
                     "Width of the scroll layout in staff spaces (default: the paper's)")
        ->check(CLI::PositiveNumber);
    render->add_option("--measures-per-system", options->measuresPerSystem,
                       "Measures in every system (default: as many as space evenly)");
    render->add_flag("--no-credits", options->noCredits,
                     "Leave out the title, credits and page numbers");
    render->add_option("--svg", options->svg,
                       "Also write SVG; with --layout page and several pages, one file per page "
                       "(name-1.svg, ...)");
    render->add_option("--metadata", options->metadata,
                       "SMuFL metadata file (default: fonts/Bravura.json next to elara-cli)");
    render->callback([&context, options] { context.exitCode = runRender(context.json, *options); });
}

} // namespace elara::cli
