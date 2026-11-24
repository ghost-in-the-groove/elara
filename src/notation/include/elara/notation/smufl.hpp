// SPDX-License-Identifier: AGPL-3.0-or-later
// SMuFL music fonts: the glyphs Elara draws and the metrics a font gives them
// (ADR-0011). Lengths are in staff spaces, measured up from a glyph's origin.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elara::notation {

/// The SMuFL glyphs Elara draws, each with the name and code point the SMuFL
/// specification gives it (glyphName(), codepoint()).
enum class Glyph : std::uint8_t {
    // Clefs
    GClef,
    GClef8vb,
    FClef,
    FClef8vb,
    CClef,
    PercussionClef,
    TabClef6,
    TabClef4,
    // Time signatures
    TimeSig0,
    TimeSig1,
    TimeSig2,
    TimeSig3,
    TimeSig4,
    TimeSig5,
    TimeSig6,
    TimeSig7,
    TimeSig8,
    TimeSig9,
    TimeSigCommon,
    TimeSigCutCommon,
    // Noteheads
    NoteheadDoubleWhole,
    NoteheadWhole,
    NoteheadHalf,
    NoteheadBlack,
    NoteheadXWhole,
    NoteheadXHalf,
    NoteheadXBlack,
    NoteheadDiamondWhole,
    NoteheadDiamondHalf,
    NoteheadDiamondBlack,
    NoteheadParenthesisLeft,
    NoteheadParenthesisRight,
    // Accidentals
    AccidentalFlat,
    AccidentalNatural,
    AccidentalSharp,
    AccidentalDoubleSharp,
    AccidentalDoubleFlat,
    // Rests
    RestDoubleWhole,
    RestWhole,
    RestHalf,
    RestQuarter,
    Rest8th,
    Rest16th,
    Rest32nd,
    Rest64th,
    Rest128th,
    // Flags
    Flag8thUp,
    Flag8thDown,
    Flag16thUp,
    Flag16thDown,
    Flag32ndUp,
    Flag32ndDown,
    Flag64thUp,
    Flag64thDown,
    Flag128thUp,
    Flag128thDown,
    // Dots and tuplets
    AugmentationDot,
    Tuplet0,
    Tuplet1,
    Tuplet2,
    Tuplet3,
    Tuplet4,
    Tuplet5,
    Tuplet6,
    Tuplet7,
    Tuplet8,
    Tuplet9,
    TupletColon,
    // Articulations
    ArticAccentAbove,
    ArticAccentBelow,
    ArticStaccatoAbove,
    ArticStaccatoBelow,
    ArticMarcatoAbove,
    ArticMarcatoBelow,
    // Dynamics
    DynamicPiano,
    DynamicMezzo,
    DynamicForte,
    DynamicPPP,
    DynamicPP,
    DynamicMP,
    DynamicMF,
    DynamicFF,
    DynamicFFF,
    // Strings and guitar
    StringsDownBow,
    StringsUpBow,
    GuitarVibratoStroke,
    GuitarWideVibratoStroke,
    WiggleTrill,
    ArrowBlackUp,
    ArrowBlackDown,
    Tremolo1,
    Tremolo2,
    Tremolo3,
    // Repeats
    RepeatDot,
    Segno,
    Coda,
    // Metronome marks
    MetNoteWhole,
    MetNoteHalfUp,
    MetNoteQuarterUp,
    MetNote8thUp,
    MetNote16thUp,
    MetNote32ndUp,
    MetAugmentationDot,
};

inline constexpr std::size_t kGlyphCount = static_cast<std::size_t>(Glyph::MetAugmentationDot) + 1;

/// The SMuFL name of a glyph ("noteheadBlack").
[[nodiscard]] std::string_view glyphName(Glyph glyph) noexcept;

/// The code point SMuFL assigns to a glyph (U+E0A4 for noteheadBlack).
[[nodiscard]] char32_t codepoint(Glyph glyph) noexcept;

/// The glyph with that SMuFL name, if Elara draws it.
[[nodiscard]] std::optional<Glyph> glyphNamed(std::string_view name) noexcept;

/// A point in staff spaces from a glyph's origin, y up (as SMuFL measures).
struct Point {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Point&, const Point&) = default;
};

/// The smallest rectangle around a glyph: its lower-left and upper-right corners.
struct BoundingBox {
    Point southWest;
    Point northEast;

    [[nodiscard]] double width() const noexcept { return northEast.x - southWest.x; }
    [[nodiscard]] double height() const noexcept { return northEast.y - southWest.y; }

    friend bool operator==(const BoundingBox&, const BoundingBox&) = default;
};

/// Points a font marks on a glyph for layout (SMuFL anchors).
enum class Anchor : std::uint8_t {
    StemUpSE,   ///< where an up stem attaches to a notehead
    StemDownNW, ///< where a down stem attaches to a notehead
    StemUpNW,   ///< flags: where the flag meets an up stem
    StemDownSW, ///< flags: where the flag meets a down stem
    CutOutNE,   ///< accidentals: corners that may overlap other glyphs
    CutOutSE,
    CutOutSW,
    CutOutNW,
    OpticalCenter, ///< dynamics and other glyphs centred by eye
};

inline constexpr std::size_t kAnchorCount = static_cast<std::size_t>(Anchor::OpticalCenter) + 1;

/// The SMuFL name of an anchor ("stemUpSE").
[[nodiscard]] std::string_view anchorName(Anchor anchor) noexcept;

/// What a font says about one glyph.
struct GlyphMetrics {
    BoundingBox box;
    double advanceWidth = 0.0;
    std::array<std::optional<Point>, kAnchorCount> anchors{};

    [[nodiscard]] std::optional<Point> anchor(Anchor which) const noexcept {
        return anchors[static_cast<std::size_t>(which)];
    }
};

/// Line thicknesses and separations a SMuFL font recommends, in staff spaces.
/// The defaults are Bravura's, used when a font does not give a value.
struct EngravingDefaults {
    double staffLineThickness = 0.13;
    double stemThickness = 0.12;
    double beamThickness = 0.5;
    double beamSpacing = 0.25;
    double legerLineThickness = 0.16;
    double legerLineExtension = 0.4;
    double thinBarlineThickness = 0.16;
    double thickBarlineThickness = 0.5;
    double barlineSeparation = 0.4;
    double thinThickBarlineSeparation = 0.4;
    double repeatBarlineDotSeparation = 0.16;
    double tieEndpointThickness = 0.1;
    double tieMidpointThickness = 0.22;
    double slurEndpointThickness = 0.1;
    double slurMidpointThickness = 0.22;
    double tupletBracketThickness = 0.16;
    double hairpinThickness = 0.16;
    double repeatEndingLineThickness = 0.16;

    friend bool operator==(const EngravingDefaults&, const EngravingDefaults&) = default;
};

struct FontError {
    std::string message;
};

/// A SMuFL font as layout sees it: the metrics of every Glyph and the
/// engraving defaults, read from the font's metadata file.
class EngravingFont {
public:
    /// Reads SMuFL font metadata (the JSON a SMuFL font ships with, such as
    /// Bravura.json). Fails when the text is not JSON or describes no glyphs.
    [[nodiscard]] static std::expected<EngravingFont, FontError>
    fromMetadata(std::string_view json);

    /// Reads a metadata file.
    [[nodiscard]] static std::expected<EngravingFont, FontError>
    fromMetadataFile(const std::filesystem::path& path);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& version() const noexcept { return version_; }
    [[nodiscard]] const EngravingDefaults& defaults() const noexcept { return defaults_; }

    /// The glyph's metrics; an empty box when the font does not describe it.
    [[nodiscard]] const GlyphMetrics& metrics(Glyph glyph) const noexcept {
        return glyphs_[static_cast<std::size_t>(glyph)];
    }

    /// Glyphs the font gives no bounding box for, in enum order.
    [[nodiscard]] std::vector<Glyph> missingGlyphs() const;

private:
    EngravingFont() = default;

    std::string name_;
    std::string version_;
    EngravingDefaults defaults_;
    std::array<GlyphMetrics, kGlyphCount> glyphs_{};
    std::array<bool, kGlyphCount> described_{};
};

} // namespace elara::notation
