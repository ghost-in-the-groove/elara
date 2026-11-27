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

std::string number(double value) {
    return jsonNumber(value);
}

std::string point(const notation::Point& p) {
    return std::format("[{},{}]", number(p.x), number(p.y));
}

void writePoint(JsonWriter& json, const notation::Point& p) {
    json.beginArray().value(p.x).value(p.y).endArray();
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
            {"repeatEndingLineThickness", d.repeatEndingLineThickness},
            {"octaveLineThickness", d.octaveLineThickness},
            {"pedalLineThickness", d.pedalLineThickness}};
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
        JsonWriter out;
        out.beginObject()
            .field("glyph", notation::glyphName(glyph))
            .field("codepoint", codepointText(glyph))
            .key("bbox")
            .beginObject()
            .key("sw");
        writePoint(out, metrics.box.southWest);
        out.key("ne");
        writePoint(out, metrics.box.northEast);
        out.endObject().field("advanceWidth", metrics.advanceWidth).key("anchors").beginObject();
        for (const auto& [name, at] : anchors) {
            out.key(name);
            writePoint(out, at);
        }
        out.endObject().endObject();
        std::cout << out.text() << '\n';
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
        JsonWriter out;
        out.beginObject()
            .field("name", font.name())
            .field("version", font.version())
            .field("metadata", utf8(path))
            .field("glyphs", notation::kGlyphCount)
            .field("described", described)
            .key("missing")
            .beginArray();
        for (const Glyph glyph : missing) {
            out.value(notation::glyphName(glyph));
        }
        out.endArray().key("defaults").beginObject();
        for (const auto& [name, value] : defaultsOf(font.defaults())) {
            out.field(name, value);
        }
        out.endObject().endObject();
        std::cout << out.text() << '\n';
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
    const std::filesystem::path path =
        options.metadata.empty() ? defaultFontMetadata() : std::filesystem::path(options.metadata);
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
