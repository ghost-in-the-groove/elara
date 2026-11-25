// SPDX-License-Identifier: AGPL-3.0-or-later
// SMuFL glyph table and font metadata (ADR-0011). The names and code points
// are those of the SMuFL specification; the metrics come from the font.
#include <elara/notation/smufl.hpp>

#include <yyjson.h>

#include <format>
#include <fstream>
#include <memory>
#include <utility>

namespace elara::notation {

namespace {

struct GlyphInfo {
    std::string_view name;
    char32_t codepoint;
};

// In the order of the Glyph enum.
constexpr std::array<GlyphInfo, kGlyphCount> kGlyphs{{
    {"gClef", 0xE050},
    {"gClef8vb", 0xE052},
    {"fClef", 0xE062},
    {"fClef8vb", 0xE064},
    {"cClef", 0xE05C},
    {"unpitchedPercussionClef1", 0xE069},
    {"6stringTabClef", 0xE06D},
    {"4stringTabClef", 0xE06E},
    {"timeSig0", 0xE080},
    {"timeSig1", 0xE081},
    {"timeSig2", 0xE082},
    {"timeSig3", 0xE083},
    {"timeSig4", 0xE084},
    {"timeSig5", 0xE085},
    {"timeSig6", 0xE086},
    {"timeSig7", 0xE087},
    {"timeSig8", 0xE088},
    {"timeSig9", 0xE089},
    {"timeSigCommon", 0xE08A},
    {"timeSigCutCommon", 0xE08B},
    {"noteheadDoubleWhole", 0xE0A0},
    {"noteheadWhole", 0xE0A2},
    {"noteheadHalf", 0xE0A3},
    {"noteheadBlack", 0xE0A4},
    {"noteheadXWhole", 0xE0A7},
    {"noteheadXHalf", 0xE0A8},
    {"noteheadXBlack", 0xE0A9},
    {"noteheadDiamondWhole", 0xE0D8},
    {"noteheadDiamondHalf", 0xE0D9},
    {"noteheadDiamondBlack", 0xE0DB},
    {"noteheadParenthesisLeft", 0xE0F5},
    {"noteheadParenthesisRight", 0xE0F6},
    {"accidentalFlat", 0xE260},
    {"accidentalNatural", 0xE261},
    {"accidentalSharp", 0xE262},
    {"accidentalDoubleSharp", 0xE263},
    {"accidentalDoubleFlat", 0xE264},
    {"restDoubleWhole", 0xE4E2},
    {"restWhole", 0xE4E3},
    {"restHalf", 0xE4E4},
    {"restQuarter", 0xE4E5},
    {"rest8th", 0xE4E6},
    {"rest16th", 0xE4E7},
    {"rest32nd", 0xE4E8},
    {"rest64th", 0xE4E9},
    {"rest128th", 0xE4EA},
    {"flag8thUp", 0xE240},
    {"flag8thDown", 0xE241},
    {"flag16thUp", 0xE242},
    {"flag16thDown", 0xE243},
    {"flag32ndUp", 0xE244},
    {"flag32ndDown", 0xE245},
    {"flag64thUp", 0xE246},
    {"flag64thDown", 0xE247},
    {"flag128thUp", 0xE248},
    {"flag128thDown", 0xE249},
    {"augmentationDot", 0xE1E7},
    {"tuplet0", 0xE880},
    {"tuplet1", 0xE881},
    {"tuplet2", 0xE882},
    {"tuplet3", 0xE883},
    {"tuplet4", 0xE884},
    {"tuplet5", 0xE885},
    {"tuplet6", 0xE886},
    {"tuplet7", 0xE887},
    {"tuplet8", 0xE888},
    {"tuplet9", 0xE889},
    {"tupletColon", 0xE88A},
    {"articAccentAbove", 0xE4A0},
    {"articAccentBelow", 0xE4A1},
    {"articStaccatoAbove", 0xE4A2},
    {"articStaccatoBelow", 0xE4A3},
    {"articMarcatoAbove", 0xE4AC},
    {"articMarcatoBelow", 0xE4AD},
    {"articTenutoAbove", 0xE4A4},
    {"articTenutoBelow", 0xE4A5},
    {"fermataAbove", 0xE4C0},
    {"fermataBelow", 0xE4C1},
    {"fermataShortAbove", 0xE4C4},
    {"fermataShortBelow", 0xE4C5},
    {"fermataLongAbove", 0xE4C6},
    {"fermataLongBelow", 0xE4C7},
    {"ottavaAlta", 0xE511},
    {"ottavaBassaVb", 0xE51C},
    {"quindicesimaAlta", 0xE515},
    {"quindicesimaBassaMb", 0xE51D},
    {"ornamentTrill", 0xE566},
    {"dynamicPiano", 0xE520},
    {"dynamicMezzo", 0xE521},
    {"dynamicForte", 0xE522},
    {"dynamicPPP", 0xE52A},
    {"dynamicPP", 0xE52B},
    {"dynamicMP", 0xE52C},
    {"dynamicMF", 0xE52D},
    {"dynamicFF", 0xE52F},
    {"dynamicFFF", 0xE530},
    {"stringsDownBow", 0xE610},
    {"stringsUpBow", 0xE612},
    {"guitarVibratoStroke", 0xEAB2},
    {"guitarWideVibratoStroke", 0xEAB3},
    {"wiggleTrill", 0xEAA4},
    {"arrowBlackUp", 0xEB60},
    {"arrowBlackDown", 0xEB64},
    {"tremolo1", 0xE220},
    {"tremolo2", 0xE221},
    {"tremolo3", 0xE222},
    {"repeatDot", 0xE044},
    {"segno", 0xE047},
    {"coda", 0xE048},
    {"metNoteWhole", 0xECA2},
    {"metNoteHalfUp", 0xECA3},
    {"metNoteQuarterUp", 0xECA5},
    {"metNote8thUp", 0xECA7},
    {"metNote16thUp", 0xECA9},
    {"metNote32ndUp", 0xECAB},
    {"metAugmentationDot", 0xECB7},
    {"brace", 0xE000},
    {"fretboardFilledCircle", 0xE858},
    {"fretboardX", 0xE859},
    {"fretboardO", 0xE85A},
}};

constexpr std::array<std::string_view, kAnchorCount> kAnchorNames{
    "stemUpSE", "stemDownNW", "stemUpNW", "stemDownSW",    "cutOutNE",
    "cutOutSE", "cutOutSW",   "cutOutNW", "opticalCenter",
};

constexpr std::array<std::pair<std::string_view, double EngravingDefaults::*>, 19> kDefaults{{
    {"staffLineThickness", &EngravingDefaults::staffLineThickness},
    {"stemThickness", &EngravingDefaults::stemThickness},
    {"beamThickness", &EngravingDefaults::beamThickness},
    {"beamSpacing", &EngravingDefaults::beamSpacing},
    {"legerLineThickness", &EngravingDefaults::legerLineThickness},
    {"legerLineExtension", &EngravingDefaults::legerLineExtension},
    {"thinBarlineThickness", &EngravingDefaults::thinBarlineThickness},
    {"thickBarlineThickness", &EngravingDefaults::thickBarlineThickness},
    {"barlineSeparation", &EngravingDefaults::barlineSeparation},
    {"thinThickBarlineSeparation", &EngravingDefaults::thinThickBarlineSeparation},
    {"repeatBarlineDotSeparation", &EngravingDefaults::repeatBarlineDotSeparation},
    {"tieEndpointThickness", &EngravingDefaults::tieEndpointThickness},
    {"tieMidpointThickness", &EngravingDefaults::tieMidpointThickness},
    {"slurEndpointThickness", &EngravingDefaults::slurEndpointThickness},
    {"slurMidpointThickness", &EngravingDefaults::slurMidpointThickness},
    {"tupletBracketThickness", &EngravingDefaults::tupletBracketThickness},
    {"hairpinThickness", &EngravingDefaults::hairpinThickness},
    {"repeatEndingLineThickness", &EngravingDefaults::repeatEndingLineThickness},
    {"octaveLineThickness", &EngravingDefaults::octaveLineThickness},
}};

FontError error(std::string_view what) {
    return FontError{std::format("SMuFL metadata: {}", what)};
}

yyjson_val* member(yyjson_val* object, std::string_view key) {
    return yyjson_is_obj(object) ? yyjson_obj_getn(object, key.data(), key.size()) : nullptr;
}

/// A two-number array, [x, y].
std::optional<Point> pointOf(yyjson_val* value) {
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 2) {
        return std::nullopt;
    }
    yyjson_val* x = yyjson_arr_get(value, 0);
    yyjson_val* y = yyjson_arr_get(value, 1);
    if (!yyjson_is_num(x) || !yyjson_is_num(y)) {
        return std::nullopt;
    }
    return Point{.x = yyjson_get_num(x), .y = yyjson_get_num(y)};
}

std::string textOf(yyjson_val* value) {
    if (yyjson_is_str(value)) {
        return {yyjson_get_str(value), yyjson_get_len(value)};
    }
    if (yyjson_is_num(value)) { // Bravura writes its version as a number
        return std::format("{}", yyjson_get_num(value));
    }
    return {};
}

} // namespace

std::string_view glyphName(Glyph glyph) noexcept {
    return kGlyphs[static_cast<std::size_t>(glyph)].name;
}

char32_t codepoint(Glyph glyph) noexcept {
    return kGlyphs[static_cast<std::size_t>(glyph)].codepoint;
}

std::optional<Glyph> glyphNamed(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kGlyphs.size(); ++i) {
        if (kGlyphs[i].name == name) {
            return static_cast<Glyph>(i);
        }
    }
    return std::nullopt;
}

std::string_view anchorName(Anchor anchor) noexcept {
    return kAnchorNames[static_cast<std::size_t>(anchor)];
}

std::expected<EngravingFont, FontError> EngravingFont::fromMetadata(std::string_view json) {
    const std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document(
        yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG), &yyjson_doc_free);
    if (!document) {
        return std::unexpected(error("not JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root)) {
        return std::unexpected(error("not a JSON object"));
    }
    yyjson_val* boxes = member(root, "glyphBBoxes");
    if (!yyjson_is_obj(boxes)) {
        return std::unexpected(error("no glyphBBoxes"));
    }

    EngravingFont font;
    font.name_ = textOf(member(root, "fontName"));
    font.version_ = textOf(member(root, "fontVersion"));
    yyjson_val* defaults = member(root, "engravingDefaults");
    for (const auto& [key, field] : kDefaults) {
        if (yyjson_val* value = member(defaults, key); yyjson_is_num(value)) {
            font.defaults_.*field = yyjson_get_num(value);
        }
    }

    yyjson_val* advances = member(root, "glyphAdvanceWidths");
    yyjson_val* anchors = member(root, "glyphsWithAnchors");
    bool any = false;
    for (std::size_t i = 0; i < kGlyphCount; ++i) {
        const std::string_view name = kGlyphs[i].name;
        yyjson_val* box = member(boxes, name);
        const auto southWest = pointOf(member(box, "bBoxSW"));
        const auto northEast = pointOf(member(box, "bBoxNE"));
        if (!southWest || !northEast) {
            continue;
        }
        GlyphMetrics& metrics = font.glyphs_[i];
        metrics.box = BoundingBox{.southWest = *southWest, .northEast = *northEast};
        if (yyjson_val* advance = member(advances, name); yyjson_is_num(advance)) {
            metrics.advanceWidth = yyjson_get_num(advance);
        } else {
            metrics.advanceWidth = northEast->x; // SMuFL: the box's width from the origin
        }
        yyjson_val* glyphAnchors = member(anchors, name);
        for (std::size_t a = 0; a < kAnchorCount; ++a) {
            metrics.anchors[a] = pointOf(member(glyphAnchors, kAnchorNames[a]));
        }
        font.described_[i] = true;
        any = true;
    }
    if (!any) {
        return std::unexpected(error("it describes none of the glyphs Elara draws"));
    }
    return font;
}

std::expected<EngravingFont, FontError>
EngravingFont::fromMetadataFile(const std::filesystem::path& path) {
    // One sized read: istreambuf_iterator costs a call per byte, and GCC's
    // optimiser reports a null dereference inside it.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? static_cast<std::streamoff>(in.tellg()) : -1;
    if (size < 0) {
        return std::unexpected(error(std::format("cannot read {}", path.string())));
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in) {
        return std::unexpected(error(std::format("cannot read {}", path.string())));
    }
    return fromMetadata(text);
}

std::vector<Glyph> EngravingFont::missingGlyphs() const {
    std::vector<Glyph> missing;
    for (std::size_t i = 0; i < kGlyphCount; ++i) {
        if (!described_[i]) {
            missing.push_back(static_cast<Glyph>(i));
        }
    }
    return missing;
}

} // namespace elara::notation
