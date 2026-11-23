// SPDX-License-Identifier: AGPL-3.0-or-later
// What layout produces and renderers draw: glyphs, lines, beams, text and
// curves at positions (ADR-0012).
#pragma once

#include <elara/core/fraction.hpp>
#include <elara/notation/smufl.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace elara::notation {

/// What an item shows, so renderers can style it and editors can pick it.
enum class Role : std::uint8_t {
    StaffLine,
    LedgerLine,
    Barline,
    Clef,
    KeySignature,
    TimeSignature,
    Notehead,
    Accidental,
    Dot,
    Stem,
    Flag,
    Beam,
    Rest,
    FretNumber, ///< a fret (or "X", "(5)", "<12>") on a tablature line
    Tie,
    Slur,      ///< hammer-on and pull-off arcs, legato slides
    Technique, ///< technique marks: "H", "P", "P.M.", "let ring", pick strokes
    Bend,
    Slide,
    Vibrato,
    Articulation,  ///< staccato, accent and marcato
    MeasureNumber, ///< the number of the first measure of a system
    Credit,        ///< title, subtitle, artist, words, music, copyright and page numbers
    TrackName,     ///< the name of a track left of its staves in the first system
    Repeat,        ///< the dots of repeat barlines and the play count ("x3") above them
    Volta,         ///< the bracket and numbers of an alternate ending
    Tuplet,        ///< the number of a tuplet and its bracket
    Tempo,         ///< a tempo mark: its note and "= 120"
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::Tempo) + 1;

/// The role's name in JSON and SVG output ("staffLine", "fretNumber").
[[nodiscard]] std::string_view roleName(Role role) noexcept;

/// A glyph drawn with its SMuFL origin at `origin`.
struct GlyphItem {
    Glyph glyph{};
    Point origin;
    Role role{};
    std::size_t measure = 0; ///< index of the measure it belongs to in the song
    double scale = 1.0;      ///< of the glyph's size: 1 at the staff's size
    friend bool operator==(const GlyphItem&, const GlyphItem&) = default;
};

/// A straight line with square-cut ends, `thickness` wide across its length;
/// dashed with dashes and gaps `dash` long when dash > 0.
struct LineItem {
    Point from;
    Point to;
    double thickness = 0.0;
    Role role{};
    std::size_t measure = 0;
    double dash = 0.0;
    friend bool operator==(const LineItem&, const LineItem&) = default;
};

/// A beam: a parallelogram with vertical ends at left.x and right.x, `thickness`
/// high, centred on the line from `left` to `right`.
struct BeamItem {
    Point left;
    Point right;
    double thickness = 0.0;
    std::size_t measure = 0;
    friend bool operator==(const BeamItem&, const BeamItem&) = default;
};

enum class TextAlign : std::uint8_t { Left, Centre, Right };

/// Text in the renderer's sans-serif font. `origin` is on the baseline, at the
/// left, centre or right of the text as `align` says; `size` is the font size
/// (an em) in staff spaces. A knocked-out text clears what is under it, as fret
/// numbers interrupt their tablature line.
struct TextItem {
    std::string text;
    Point origin;
    double size = 1.0;
    TextAlign align = TextAlign::Centre;
    bool italic = false;
    bool knockout = false;
    Role role{};
    std::size_t measure = 0;
    friend bool operator==(const TextItem&, const TextItem&) = default;
};

/// A cubic Bézier curve from `from` to `to`, `thickness` thick in its middle and
/// `endThickness` at its ends: a lens for ties and slurs, a plain stroke when
/// both are equal.
struct CurveItem {
    Point from;
    Point control1;
    Point control2;
    Point to;
    double thickness = 0.0;
    double endThickness = 0.0;
    Role role{};
    std::size_t measure = 0;
    friend bool operator==(const CurveItem&, const CurveItem&) = default;
};

/// A filled polygon, such as an arrowhead.
struct PolygonItem {
    std::vector<Point> points;
    Role role{};
    std::size_t measure = 0;
    friend bool operator==(const PolygonItem&, const PolygonItem&) = default;
};

/// Where a column of notes (or rests) starts in a measure: `at` is the time
/// it starts, as a fraction of the measure's length, and `x` where it stands.
struct MeasureOnset {
    double at = 0.0;
    double x = 0.0;
    friend bool operator==(const MeasureOnset&, const MeasureOnset&) = default;
};

/// Where a measure sits: from the end of the barline before it to the start
/// of the barline after it, and from the top line of its system's first staff
/// (`y`) down to the bottom line of the last one. `onsets` are its columns in
/// time order, for the playback cursor.
struct MeasureBox {
    std::size_t index = 0;
    double x = 0.0;
    double width = 0.0;
    double y = 0.0;
    double height = 0.0;
    std::vector<MeasureOnset> onsets{};
    friend bool operator==(const MeasureBox&, const MeasureBox&) = default;
};

/// Where the notes of a measure starting `start` whole notes into it are placed
/// (the left of an unshifted notehead), shared by every staff of the system:
/// how an editor finds a beat on the score.
struct ColumnBox {
    std::size_t measure = 0;
    core::Fraction start;
    double x = 0.0;
    friend bool operator==(const ColumnBox&, const ColumnBox&) = default;
};

/// How a staff is shown: standard notation or tablature (ADR-0004: tablature is
/// a view of a fretted staff).
enum class StaffView : std::uint8_t { Standard, Tablature };

/// Where one staff of a system sits: its lines run from `x` over `width`, the
/// top one at `y`, `lines` lines over `height` staff spaces.
struct StaffBox {
    std::size_t track = 0;
    std::size_t staff = 0;
    StaffView view = StaffView::Standard;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 4.0;
    std::size_t lines = 5;
    friend bool operator==(const StaffBox&, const StaffBox&) = default;
};

/// Where a system sits: measures [firstMeasure, firstMeasure + measureCount)
/// from `x` over `width`, from the top line of its first staff (`y`) down
/// `height` to the bottom line of its last; its ink from `top` to `bottom`.
struct SystemBox {
    std::size_t firstMeasure = 0;
    std::size_t measureCount = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double top = 0.0;
    double bottom = 0.0;
    friend bool operator==(const SystemBox&, const SystemBox&) = default;
};

/// Positioned glyphs, lines, beams, text and curves. Lengths are in staff
/// spaces and y grows downwards. In a single system the top line of the first
/// staff is at y = 0; on a page (0, 0) is the page's top-left corner.
struct DisplayList {
    double width = 0.0;  ///< from x = 0 to the end of the last barline, or of the page
    double top = 0.0;    ///< highest ink, negative above the first staff; 0 on a page
    double bottom = 4.0; ///< lowest ink, or the bottom of the page
    std::vector<SystemBox> systems;
    std::vector<StaffBox> staves;
    std::vector<MeasureBox> measures;
    std::vector<ColumnBox> columns; ///< in measure order, then time order
    std::vector<GlyphItem> glyphs;
    std::vector<LineItem> lines;
    std::vector<BeamItem> beams;
    std::vector<TextItem> texts;
    std::vector<CurveItem> curves;
    std::vector<PolygonItem> polygons;
    friend bool operator==(const DisplayList&, const DisplayList&) = default;
};

/// The width layout assumes for `text` at `size`: the advance widths of a
/// Helvetica-like sans-serif font. Renderers use it to size knockouts.
[[nodiscard]] double textWidth(std::string_view text, double size) noexcept;

/// The height of digits and capitals at `size` (0.72 em), for centring text on
/// a line.
[[nodiscard]] constexpr double capHeight(double size) noexcept {
    return 0.72 * size;
}

} // namespace elara::notation
