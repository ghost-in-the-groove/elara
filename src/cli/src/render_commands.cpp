// SPDX-License-Identifier: AGPL-3.0-or-later
// Render subcommand: staves in standard notation, tablature or both, as the
// notation engine lays them out on one line (ADR-0012) or on pages (ADR-0014),
// printed as display lists or written as SVG.
#include <elara/audio/sequence.hpp>
#include <elara/io/compression.hpp>
#include <elara/notation/cursor.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/font_outlines.hpp>
#include <elara/notation/layout_issues.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/pdf.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/svg.hpp>
#include <elara/notation/symbol.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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
    double width = 0.0;       // staff spaces, for the scroll and strips layouts; 0: the paper's
    double stripHeight = 0.0; // staff spaces, for the strips layout; 0: the paper's height
    std::size_t measuresPerSystem = 0;
    bool noCredits = false;
    bool issues = false; // report what the layout could not keep (ADR-0150)
    std::string svg;
    std::string pdf;
    std::string metadata;
    double cursorAt = -1.0;       // seconds; negative for no cursor
    std::vector<double> timeAt;   // page (from 0), x and y in staff spaces; empty for none
    std::vector<double> symbolAt; // the same, for the symbol drawn there
};

/// The playback cursor at `seconds`, as JSON ("null" when no page shows the
/// measure played then), and the played measure it is in.
struct CursorReport {
    std::string json = "null";
    std::string text;
};

CursorReport cursorReport(const core::Song& song, std::span<const notation::DisplayList> pages,
                          double seconds) {
    const audio::Sequence sequence(song);
    const std::size_t played = sequence.playedMeasureAt(seconds);
    if (played >= sequence.measures().size()) {
        return {.json = "null", .text = std::format("cursor at {} s: after the end\n", seconds)};
    }
    const audio::PlayedMeasure& measure = sequence.measures()[played];
    const double length = measure.end - measure.start;
    const double fraction = length > 0.0 ? (seconds - measure.start) / length : 0.0;
    const std::optional<notation::Cursor> cursor =
        notation::cursorAt(pages, measure.measure, fraction);
    if (!cursor) {
        return {.json = "null",
                .text = std::format("cursor at {} s: measure {} is not shown\n", seconds,
                                    measure.measure + 1)};
    }
    JsonWriter out;
    out.beginObject()
        .field("seconds", seconds)
        .field("played", played)
        .field("measure", measure.measure)
        .field("page", cursor->page)
        .field("x", cursor->x)
        .field("y", cursor->y)
        .field("height", cursor->height)
        .endObject();
    return {.json = out.text(),
            .text = std::format("cursor at {} s: measure {}, page {}, x {}, y {}\n", seconds,
                                measure.measure + 1, cursor->page + 1, jsonNumber(cursor->x),
                                jsonNumber(cursor->y))};
}

/// When playback would start for a click at `at` (page, x, y), as JSON, with
/// the measure ("null" for both away from every measure).
CursorReport timeReport(const core::Song& song, std::span<const notation::DisplayList> pages,
                        std::span<const double> at) {
    const double page = at[0];
    const double x = at[1];
    const double y = at[2];
    std::optional<notation::MeasurePosition> position;
    if (page >= 0.0 && page == std::floor(page) && page < static_cast<double>(pages.size())) {
        position = notation::measurePositionAt(pages[static_cast<std::size_t>(page)], x, y);
    }
    std::optional<double> seconds;
    if (position) {
        seconds = audio::Sequence(song).secondsAt(position->measure, position->fraction);
    }
    JsonWriter out;
    out.beginObject().field("page", page).field("x", x).field("y", y);
    if (!position || !seconds) {
        out.key("measure").null().key("seconds").null().endObject();
        return {.json = out.text(),
                .text = std::format("time at page {}, x {}, y {}: no measure there\n",
                                    jsonNumber(page + 1.0), jsonNumber(x), jsonNumber(y))};
    }
    out.field("measure", position->measure).field("seconds", *seconds).endObject();
    return {.json = out.text(),
            .text = std::format("time at page {}, x {}, y {}: measure {}, {} s\n",
                                jsonNumber(page + 1.0), jsonNumber(x), jsonNumber(y),
                                position->measure + 1, jsonNumber(*seconds))};
}

/// What the layout of `pages` could not keep (ADR-0150): overlaps, and on pages
/// laid out with `page` the measures wider than the page, as a JSON array and
/// a line each.
CursorReport issuesReport(std::span<const notation::DisplayList> pages,
                          const notation::EngravingFont& font,
                          const std::optional<notation::PageLayoutOptions>& page) {
    const std::vector<notation::LayoutIssue> issues =
        page ? notation::layoutIssues(pages, font, *page) : notation::overlaps(pages, font);
    CursorReport report{.json = "[", .text = {}};
    for (std::size_t i = 0; i < issues.size(); ++i) {
        const notation::LayoutIssue& issue = issues[i];
        const bool overlap = issue.kind == notation::LayoutIssueKind::Overlap;
        const std::string roles =
            overlap ? std::format(R"(["{}","{}"])", roleName(issue.first), roleName(issue.second))
                    : std::string("[]");
        report.json += std::format(
            R"({}{{"kind":"{}","page":{},"measure":{},"roles":{},"box":[{},{},{},{}]}})",
            i == 0 ? "" : ",", notation::issueKindName(issue.kind), issue.page, issue.measure,
            roles, jsonNumber(issue.box.x), jsonNumber(issue.box.y), jsonNumber(issue.box.width),
            jsonNumber(issue.box.height));
        report.text +=
            overlap ? std::format("issue: {} overlaps {} in measure {} on page {} at x {}, y {}\n",
                                  roleName(issue.first), roleName(issue.second), issue.measure + 1,
                                  issue.page + 1, jsonNumber(issue.box.x), jsonNumber(issue.box.y))
                    : std::format("issue: measure {} on page {} is {} staff spaces too wide\n",
                                  issue.measure + 1, issue.page + 1, jsonNumber(issue.box.width));
    }
    report.json += ']';
    if (issues.empty()) {
        report.text = "issues: none\n";
    }
    return report;
}

/// The symbol that is edited where it is drawn (ADR-0130) at `at` (page, x,
/// y), as JSON, with its role "null" when there is none there.
CursorReport symbolReport(std::span<const notation::DisplayList> pages,
                          const notation::EngravingFont& font, std::span<const double> at) {
    const double page = at[0];
    const double x = at[1];
    const double y = at[2];
    std::optional<notation::Symbol> symbol;
    const notation::DisplayList* list = nullptr;
    if (page >= 0.0 && page == std::floor(page) && page < static_cast<double>(pages.size())) {
        list = &pages[static_cast<std::size_t>(page)];
        symbol = notation::symbolAt(*list, {x, y}, notation::kEditedRoles, font);
    }
    JsonWriter out;
    out.beginObject().field("page", page).field("x", x).field("y", y).key("role");
    if (!symbol) {
        out.null().endObject();
        return {.json = out.text(),
                .text = std::format("symbol at page {}, x {}, y {}: none there\n",
                                    jsonNumber(page + 1.0), jsonNumber(x), jsonNumber(y))};
    }
    out.string(notation::roleName(symbol->role))
        .field("measure", symbol->measure)
        .key("box")
        .beginArray()
        .value(symbol->box.x)
        .value(symbol->box.y)
        .value(symbol->box.width)
        .value(symbol->box.height)
        .endArray()
        .key("staff");
    if (symbol->staff) {
        const notation::StaffBox& on = list->staves[*symbol->staff];
        out.beginObject().field("track", on.track).field("staff", on.staff).endObject();
    } else {
        out.null();
    }
    out.endObject();
    return {.json = out.text(),
            .text = std::format("symbol at page {}, x {}, y {}: {} of measure {}\n",
                                jsonNumber(page + 1.0), jsonNumber(x), jsonNumber(y),
                                notation::roleName(symbol->role), symbol->measure + 1)};
}

void writePoint(JsonWriter& json, const notation::Point& p) {
    json.beginArray().value(p.x).value(p.y).endArray();
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

// The display list's fields, written into the object that holds them.
void writeDisplayList(JsonWriter& json, const notation::DisplayList& list) {
    const auto counts = countRoles(list);
    json.field("width", list.width)
        .field("top", list.top)
        .field("bottom", list.bottom)
        .key("counts")
        .beginObject();
    for (std::size_t r = 0; r < kRoleCount; ++r) {
        json.field(roleName(static_cast<Role>(r)), counts[r]);
    }
    json.endObject().key("systems").beginArray();
    for (const auto& box : list.systems) {
        json.beginObject()
            .field("firstMeasure", box.firstMeasure)
            .field("measureCount", box.measureCount)
            .field("x", box.x)
            .field("y", box.y)
            .field("width", box.width)
            .field("height", box.height)
            .field("top", box.top)
            .field("bottom", box.bottom)
            .endObject();
    }
    json.endArray().key("staves").beginArray();
    for (const auto& box : list.staves) {
        json.beginObject()
            .field("track", box.track)
            .field("staff", box.staff)
            .field("view", viewName(box.view))
            .field("x", box.x)
            .field("y", box.y)
            .field("width", box.width)
            .field("height", box.height)
            .field("lines", box.lines)
            .endObject();
    }
    json.endArray().key("measures").beginArray();
    for (const auto& box : list.measures) {
        json.beginObject()
            .field("index", box.index)
            .field("x", box.x)
            .field("width", box.width)
            .field("y", box.y)
            .field("height", box.height)
            .endObject();
    }
    json.endArray().key("columns").beginArray();
    for (const auto& column : list.columns) {
        json.beginObject()
            .field("measure", column.measure)
            .field("start", column.start.toString())
            .field("x", column.x)
            .endObject();
    }
    json.endArray().key("glyphs").beginArray();
    for (const auto& item : list.glyphs) {
        json.beginObject()
            .field("glyph", notation::glyphName(item.glyph))
            .field("role", roleName(item.role))
            .field("measure", item.measure)
            .key("at");
        writePoint(json, item.origin);
        if (item.scale != 1.0) {
            json.field("scale", item.scale);
        }
        json.endObject();
    }
    json.endArray().key("lines").beginArray();
    for (const auto& item : list.lines) {
        json.beginObject()
            .field("role", roleName(item.role))
            .field("measure", item.measure)
            .key("from");
        writePoint(json, item.from);
        json.key("to");
        writePoint(json, item.to);
        json.field("thickness", item.thickness);
        if (item.dash > 0.0) {
            json.field("dash", item.dash);
        }
        json.endObject();
    }
    json.endArray().key("beams").beginArray();
    for (const auto& item : list.beams) {
        json.beginObject().field("measure", item.measure).key("left");
        writePoint(json, item.left);
        json.key("right");
        writePoint(json, item.right);
        json.field("thickness", item.thickness).endObject();
    }
    json.endArray().key("texts").beginArray();
    for (const auto& item : list.texts) {
        json.beginObject()
            .field("text", item.text)
            .field("role", roleName(item.role))
            .field("measure", item.measure)
            .key("at");
        writePoint(json, item.origin);
        json.field("size", item.size)
            .field("align", alignName(item.align))
            .field("face", faceName(item.face))
            .field("knockout", item.knockout)
            .endObject();
    }
    json.endArray().key("curves").beginArray();
    for (const auto& item : list.curves) {
        json.beginObject()
            .field("role", roleName(item.role))
            .field("measure", item.measure)
            .key("from");
        writePoint(json, item.from);
        json.key("control1");
        writePoint(json, item.control1);
        json.key("control2");
        writePoint(json, item.control2);
        json.key("to");
        writePoint(json, item.to);
        json.field("thickness", item.thickness)
            .field("endThickness", item.endThickness)
            .endObject();
    }
    json.endArray().key("polygons").beginArray();
    for (const auto& item : list.polygons) {
        json.beginObject()
            .field("role", roleName(item.role))
            .field("measure", item.measure)
            .key("points")
            .beginArray();
        for (const auto& p : item.points) {
            writePoint(json, p);
        }
        json.endArray().endObject();
    }
    json.endArray();
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
    if (options.layout == "scroll" || options.layout == "strips") {
        page.height = 0.0; // strips are cut from that one page
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
    std::optional<notation::PageLayoutOptions> laidOut;
    if (paged) {
        const notation::PageLayoutOptions page = pageLayoutOptions(options);
        laidOut = page;
        if (page.width <= 2 * page.margin) {
            return usageError("the margins leave no room on the page");
        }
        if (options.layout == "strips") {
            const double height =
                options.stripHeight > 0.0
                    ? options.stripHeight
                    : notation::pageOptions(options.paper == "letter" ? notation::kLetter
                                                                      : notation::kA4,
                                            options.staffSpace, options.margin)
                          .height;
            pages = notation::cutIntoStrips(
                notation::layoutPages(*song, *selection, *font, page).front(), height);
        } else {
            pages = notation::layoutPages(*song, *selection, *font, page);
        }
    } else {
        pages.push_back(
            notation::layoutSystem(*song, *selection, *font,
                                   {.firstMeasure = options.first, .measureCount = options.count}));
    }
    // Pages and strips are written one file each.
    const bool split = options.layout == "page" || options.layout == "strips";
    std::vector<std::string> written;
    if (!options.svg.empty()) {
        std::filesystem::path otf = metadata;
        otf.replace_extension(".otf");
        for (std::size_t p = 0; p < pages.size(); ++p) {
            const std::filesystem::path out =
                split ? pagePath(options.svg, p, pages.size()) : std::filesystem::path(options.svg);
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
    std::size_t pdfBytes = 0;
    if (!options.pdf.empty()) {
        // One document of every page, glyphs drawn from the font's outlines.
        std::filesystem::path otf = metadata;
        otf.replace_extension(".otf");
        const auto outlines = notation::FontOutlines::fromFile(otf);
        if (!outlines) {
            return fail(outlines.error());
        }
        const notation::PdfOptions pdf{.staffSpace = options.staffSpace * 72.0 / 25.4,
                                       .margin = paged ? 0.0 : 2.0, // pages have their own
                                       .title = song->info().title,
                                       .compress = io::zlibCompress};
        const std::string document = notation::toPdf(pages, *outlines, pdf);
        const std::filesystem::path out(options.pdf);
        std::ofstream file(out, std::ios::binary);
        file << document;
        if (!file) {
            return fail(std::format("cannot write {}", utf8(out.generic_u8string())));
        }
        pdfBytes = document.size();
    }
    std::size_t shown = 0;
    std::size_t systems = 0;
    for (const notation::DisplayList& page : pages) {
        shown += page.measures.size();
        systems += page.systems.size();
    }
    if (json) {
        JsonWriter out;
        out.beginObject().field("file", options.file);
        if (options.tracks.empty()) {
            out.field("track", options.track).field("staff", options.staff);
        } else {
            out.key("tracks").beginArray();
            std::size_t previous = notation::kAllMeasures;
            for (const notation::StaffSelection& each : *selection) {
                if (each.track != previous) {
                    out.value(each.track);
                    previous = each.track;
                }
            }
            out.endArray();
        }
        out.field("first", options.first).field("count", shown).key("svg");
        if (written.empty()) {
            out.null();
        } else if (split) {
            out.beginArray();
            for (const std::string& file : written) {
                out.value(file);
            }
            out.endArray();
        } else {
            out.string(options.svg);
        }
        out.key("pdf");
        if (options.pdf.empty()) {
            out.null();
        } else {
            out.beginObject()
                .field("file", options.pdf)
                .field("pages", pages.size())
                .field("bytes", pdfBytes)
                .endObject();
        }
        out.field("view", options.view).field("layout", options.layout);
        if (options.cursorAt >= 0.0) {
            out.key("cursor").raw(cursorReport(*song, pages, options.cursorAt).json);
        }
        if (!options.timeAt.empty()) {
            out.key("timeAt").raw(timeReport(*song, pages, options.timeAt).json);
        }
        if (!options.symbolAt.empty()) {
            out.key("symbolAt").raw(symbolReport(pages, *font, options.symbolAt).json);
        }
        if (options.issues) {
            out.key("issues").raw(issuesReport(pages, *font, laidOut).json);
        }
        if (paged) {
            out.key("pages").beginArray();
            for (const notation::DisplayList& page : pages) {
                out.beginObject();
                writeDisplayList(out, page);
                out.endObject();
            }
            out.endArray();
        } else {
            writeDisplayList(out, pages.front());
        }
        out.endObject();
        std::cout << out.text() << '\n';
        return kExitOk;
    }
    const std::string what = options.tracks.empty()
                                 ? std::format("track {}, staff {}", options.track, options.staff)
                                 : std::format("{} staves", selection->size());
    if (paged) {
        const bool strips = options.layout == "strips";
        std::cout << std::format(
            "{}: {} ({}), measures {} to {} of {}: {} {} on {} {}\n", options.file, what,
            options.view, options.first, options.first + shown - 1, song->measureCount(), systems,
            systems == 1 ? "system" : "systems", pages.size(),
            pages.size() == 1 ? (strips ? "strip" : "page") : (strips ? "strips" : "pages"));
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
    if (!options.pdf.empty()) {
        std::cout << std::format("saved {} ({} {}, {} bytes)\n", options.pdf, pages.size(),
                                 pages.size() == 1 ? "page" : "pages", pdfBytes);
    }
    if (options.cursorAt >= 0.0) {
        std::cout << cursorReport(*song, pages, options.cursorAt).text;
    }
    if (!options.timeAt.empty()) {
        std::cout << timeReport(*song, pages, options.timeAt).text;
    }
    if (!options.symbolAt.empty()) {
        std::cout << symbolReport(pages, *font, options.symbolAt).text;
    }
    if (options.issues) {
        std::cout << issuesReport(pages, *font, laidOut).text;
    }
    return kExitOk;
}

} // namespace

void addRenderCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<RenderOptions>();
    auto* render = app.add_subcommand(
        "render", "Lay out staves of a song in standard notation, tablature or both, on one line "
                  "or on pages: print the display list (glyphs, lines, beams, text and curves in "
                  "staff spaces) or write it as SVG or PDF");
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
                     "line (one line, natural spacing), page (systems on pages), scroll (systems "
                     "on one endless page) or strips (that page cut between systems)")
        ->check(CLI::IsMember({"line", "page", "scroll", "strips"}))
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
                     "Width of the scroll and strips layouts in staff spaces (default: the "
                     "paper's)")
        ->check(CLI::PositiveNumber);
    render
        ->add_option("--strip-height", options->stripHeight,
                     "Height of the strips layout's strips in staff spaces (default: the paper's)")
        ->check(CLI::PositiveNumber);
    render->add_option("--measures-per-system", options->measuresPerSystem,
                       "Measures in every system (default: as many as space evenly)");
    render
        ->add_option("--cursor-at", options->cursorAt,
                     "Report where the playback cursor stands this many seconds into the song")
        ->check(CLI::NonNegativeNumber);
    render
        ->add_option("--time-at", options->timeAt,
                     "Report when playback would start for a click at PAGE X Y (the page from 0, "
                     "x and y in staff spaces, as the cursor reports them)")
        ->expected(3);
    render
        ->add_option("--symbol-at", options->symbolAt,
                     "Report the symbol drawn at PAGE X Y that is edited where it is drawn: a "
                     "clef, a key or time signature, a tempo mark, a section, a chord symbol or "
                     "a beat's text")
        ->expected(3);
    render->add_flag("--no-credits", options->noCredits,
                     "Leave out the title, credits and page numbers");
    render->add_flag("--issues", options->issues,
                     "Report what the layout could not keep: noteheads, accidentals, fret "
                     "numbers, syllables or chord names whose ink overlaps, and measures wider "
                     "than the page");
    render->add_option("--svg", options->svg,
                       "Also write SVG; with --layout page or strips and several pages or "
                       "strips, one file each (name-1.svg, ...)");
    render->add_option("--pdf", options->pdf,
                       "Also write a PDF document of every page (or of the line), the glyphs "
                       "drawn from the music font next to the metadata (Bravura.otf)");
    render->add_option("--metadata", options->metadata,
                       "SMuFL metadata file (default: fonts/Bravura.json next to elara-cli)");
    render->callback([&context, options] { context.exitCode = runRender(context.json, *options); });
}

} // namespace elara::cli
