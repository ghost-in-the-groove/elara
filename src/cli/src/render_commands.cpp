// SPDX-License-Identifier: AGPL-3.0-or-later
// Render subcommand: one staff as the notation engine lays it out (ADR-0012),
// printed as a display list or written as SVG.
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>
#include <elara/notation/svg.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

namespace elara::cli {

namespace {

using notation::kRoleCount;
using notation::Role;
using notation::roleName;

struct RenderOptions {
    std::string file;
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t first = 0;
    std::size_t count = notation::kAllMeasures;
    std::string svg;
    std::string metadata;
};

std::string point(const notation::Point& p) {
    return std::format("[{},{}]", jsonNumber(p.x), jsonNumber(p.y));
}

std::array<std::size_t, kRoleCount> countRoles(const notation::DisplayList& list) {
    std::array<std::size_t, kRoleCount> counts{};
    for (const auto& item : list.glyphs) {
        ++counts[static_cast<std::size_t>(item.role)];
    }
    for (const auto& item : list.lines) {
        ++counts[static_cast<std::size_t>(item.role)];
    }
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
    json += R"(},"measures":[)";
    for (std::size_t i = 0; i < list.measures.size(); ++i) {
        const auto& box = list.measures[i];
        json += std::format(R"({}{{"index":{},"x":{},"width":{}}})", i == 0 ? "" : ",", box.index,
                            jsonNumber(box.x), jsonNumber(box.width));
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
        json += std::format(R"({}{{"role":"{}","measure":{},"from":{},"to":{},"thickness":{}}})",
                            i == 0 ? "" : ",", roleName(item.role), item.measure, point(item.from),
                            point(item.to), jsonNumber(item.thickness));
    }
    json += R"(],"beams":[)";
    for (std::size_t i = 0; i < list.beams.size(); ++i) {
        const auto& item = list.beams[i];
        json += std::format(R"({}{{"measure":{},"left":{},"right":{},"thickness":{}}})",
                            i == 0 ? "" : ",", item.measure, point(item.left), point(item.right),
                            jsonNumber(item.thickness));
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

int runRender(bool json, const RenderOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(std::format("{}: {}", options.file, song.error()));
    }
    if (options.track >= song->trackCount()) {
        return usageError(
            std::format("no track {}: the song has {}", options.track, song->trackCount()));
    }
    const std::size_t staves = song->track(options.track).staffCount();
    if (options.staff >= staves) {
        return usageError(
            std::format("no staff {}: track {} has {}", options.staff, options.track, staves));
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
    const notation::DisplayList list =
        notation::layoutStaff(*song, options.track, options.staff, *font,
                              {.firstMeasure = options.first, .measureCount = options.count});
    if (!options.svg.empty()) {
        const std::filesystem::path out(options.svg);
        std::filesystem::path otf = metadata;
        otf.replace_extension(".otf");
        std::ofstream file(out, std::ios::binary);
        file << notation::toSvg(list, {.fontFamily = font->name(), .fontUrl = fontUrl(otf, out)});
        if (!file) {
            return fail(std::format("cannot write {}", options.svg));
        }
    }
    const std::size_t shown = list.measures.size();
    if (json) {
        const std::string svg =
            options.svg.empty() ? "null" : std::format("\"{}\"", jsonEscape(options.svg));
        std::cout
            << std::format(
                   R"({{"file":"{}","track":{},"staff":{},"first":{},"count":{},"svg":{},{}}})",
                   jsonEscape(options.file), options.track, options.staff, options.first, shown,
                   svg, displayListJson(list))
            << '\n';
        return kExitOk;
    }
    std::cout << std::format("{}: track {}, staff {}, measures {} to {} of {}: {} x {} staff "
                             "spaces\n",
                             options.file, options.track, options.staff, options.first,
                             options.first + shown - 1, song->measureCount(),
                             jsonNumber(list.width), jsonNumber(list.bottom - list.top));
    const auto counts = countRoles(list);
    for (std::size_t r = 0; r < kRoleCount; ++r) {
        if (counts[r] > 0) {
            std::cout << std::format("  {} {}\n", roleName(static_cast<Role>(r)), counts[r]);
        }
    }
    if (!options.svg.empty()) {
        std::cout << std::format("saved {}\n", options.svg);
    }
    return kExitOk;
}

} // namespace

void addRenderCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<RenderOptions>();
    auto* render = app.add_subcommand(
        "render", "Lay out one staff of a song in standard notation: print the display list "
                  "(glyphs, lines and beams in staff spaces) or write it as SVG");
    render->add_option("file", options->file, "A .elara file or any file import reads")->required();
    render->add_option("--track", options->track, "Track index (0 = first)")->capture_default_str();
    render->add_option("--staff", options->staff, "Staff index in the track")
        ->capture_default_str();
    render->add_option("--first", options->first, "First measure index")->capture_default_str();
    render->add_option("--count", options->count, "Number of measures (default: to the end)");
    render->add_option("--svg", options->svg, "Also write the staff as an SVG file");
    render->add_option("--metadata", options->metadata,
                       "SMuFL metadata file (default: fonts/Bravura.json next to elara-cli)");
    render->callback([&context, options] { context.exitCode = runRender(context.json, *options); });
}

} // namespace elara::cli
