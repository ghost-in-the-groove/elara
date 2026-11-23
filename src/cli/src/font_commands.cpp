// SPDX-License-Identifier: AGPL-3.0-or-later
// Font subcommand: what the notation engine reads from the SMuFL music font
// (ADR-0011).
#include <elara/notation/smufl.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using notation::Anchor;
using notation::EngravingFont;
using notation::Glyph;

struct FontOptions {
    std::string metadata;
    std::string glyph;
};

std::string number(double value) { return jsonNumber(value); }

std::string point(const notation::Point& p) {
    return std::format("[{},{}]", number(p.x), number(p.y));
}

std::string codepointText(Glyph glyph) {
    return std::format("U+{:04X}", static_cast<std::uint32_t>(notation::codepoint(glyph)));
}

// (name, value) of every engraving default, in the order of the struct.
std::vector<std::pair<std::string_view, double>> defaultsOf(const notation::EngravingDefaults& d) {
    return {{"staffLineThickness", d.staffLineThickness},
            {"stemThickness", d.stemThickness},
            {"beamThickness", d.beamThickness},
            {"beamSpacing", d.beamSpacing},
            {"legerLineThickness", d.legerLineThickness},
            {"legerLineExtension", d.legerLineExtension},
            {"thinBarlineThickness", d.thinBarlineThickness},
            {"thickBarlineThickness", d.thickBarlineThickness},
            {"barlineSeparation", d.barlineSeparation},
            {"thinThickBarlineSeparation", d.thinThickBarlineSeparation},
            {"repeatBarlineDotSeparation", d.repeatBarlineDotSeparation},
            {"tieEndpointThickness", d.tieEndpointThickness},
            {"tieMidpointThickness", d.tieMidpointThickness},
            {"slurEndpointThickness", d.slurEndpointThickness},
            {"slurMidpointThickness", d.slurMidpointThickness},
            {"tupletBracketThickness", d.tupletBracketThickness},
            {"hairpinThickness", d.hairpinThickness},
            {"repeatEndingLineThickness", d.repeatEndingLineThickness}};
}

int printGlyph(bool json, const EngravingFont& font, Glyph glyph) {
    const notation::GlyphMetrics& metrics = font.metrics(glyph);
    std::vector<std::pair<std::string_view, notation::Point>> anchors;
    for (std::size_t a = 0; a < notation::kAnchorCount; ++a) {
        if (const auto at = metrics.anchor(static_cast<Anchor>(a))) {
            anchors.emplace_back(notation::anchorName(static_cast<Anchor>(a)), *at);
        }
    }
    if (json) {
        std::string anchorJson;
        for (const auto& [name, at] : anchors) {
            anchorJson +=
                std::format("{}\"{}\":{}", anchorJson.empty() ? "" : ",", name, point(at));
        }
        std::cout
            << std::format(
                   R"({{"glyph":"{}","codepoint":"{}","bbox":{{"sw":{},"ne":{}}},"advanceWidth":{},)"
                   R"("anchors":{{{}}}}})",
                   notation::glyphName(glyph), codepointText(glyph), point(metrics.box.southWest),
                   point(metrics.box.northEast), number(metrics.advanceWidth), anchorJson)
            << '\n';
        return kExitOk;
    }
    std::cout << std::format("{} ({})\n", notation::glyphName(glyph), codepointText(glyph));
    std::cout << std::format("  box {} to {} ({} x {} staff spaces), advance {}\n",
                             point(metrics.box.southWest), point(metrics.box.northEast),
                             number(metrics.box.width()), number(metrics.box.height()),
                             number(metrics.advanceWidth));
    for (const auto& [name, at] : anchors) {
        std::cout << std::format("  {} {}\n", name, point(at));
    }
    return kExitOk;
}

int printFont(bool json, const EngravingFont& font, const std::filesystem::path& path) {
    const std::vector<Glyph> missing = font.missingGlyphs();
    const auto described = notation::kGlyphCount - missing.size();
    if (json) {
        std::string missingJson;
        for (const Glyph glyph : missing) {
            missingJson +=
                std::format("{}\"{}\"", missingJson.empty() ? "" : ",", notation::glyphName(glyph));
        }
        std::string defaultsJson;
        for (const auto& [name, value] : defaultsOf(font.defaults())) {
            defaultsJson +=
                std::format("{}\"{}\":{}", defaultsJson.empty() ? "" : ",", name, number(value));
        }
        std::cout
            << std::format(
                   R"({{"name":"{}","version":"{}","metadata":"{}","glyphs":{},"described":{},)"
                   R"("missing":[{}],"defaults":{{{}}}}})",
                   jsonEscape(font.name()), jsonEscape(font.version()), jsonEscape(utf8(path)),
                   notation::kGlyphCount, described, missingJson, defaultsJson)
            << '\n';
        return kExitOk;
    }
    std::cout << std::format("{} {} ({})\n", font.name(), font.version(), utf8(path));
    std::cout << std::format("{} of the {} glyphs Elara draws are described\n", described,
                             notation::kGlyphCount);
    for (const Glyph glyph : missing) {
        std::cout << "  missing: " << notation::glyphName(glyph) << '\n';
    }
    for (const auto& [name, value] : defaultsOf(font.defaults())) {
        std::cout << std::format("  {} {}\n", name, number(value));
    }
    return kExitOk;
}

int runFont(bool json, const FontOptions& options) {
    const std::filesystem::path path = options.metadata.empty()
                                           ? defaultFontMetadata()
                                           : std::filesystem::path(options.metadata);
    const auto font = EngravingFont::fromMetadataFile(path);
    if (!font) {
        return fail(font.error().message);
    }
    if (options.glyph.empty()) {
        return printFont(json, *font, path);
    }
    const auto glyph = notation::glyphNamed(options.glyph);
    if (!glyph) {
        return usageError(std::format("unknown glyph \"{}\" (use a SMuFL name such as "
                                      "noteheadBlack)",
                                      options.glyph));
    }
    return printGlyph(json, *font, *glyph);
}

} // namespace

void addFontCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<FontOptions>();
    auto* font = app.add_subcommand(
        "font", "Show what the notation engine reads from the music font: its engraving "
                "defaults and the glyphs it describes, or one glyph's metrics");
    font->add_option("--metadata", options->metadata,
                     "SMuFL metadata file (default: fonts/Bravura.json next to elara-cli)");
    font->add_option("--glyph", options->glyph, "Show one glyph by its SMuFL name");
    font->callback([&context, options] { context.exitCode = runFont(context.json, *options); });
}

} // namespace elara::cli
