// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: what the staff writers share and the interface the system layout
// drives them through (ADR-0012).
#pragma once

#include <elara/core/effects.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/signature.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace elara::core {
class Song;
}

namespace elara::notation::detail {

// ---- Spacing, in staff spaces (ADR-0012) ------------------------------------------

inline constexpr double kClefX = 0.6;        // left edge of the clef
inline constexpr double kHeaderGap = 0.9;    // between clef, key and time signatures
inline constexpr double kFirstNoteGap = 1.6; // after the header, before the first note
inline constexpr double kBarlineGap = 1.2;   // after a barline, before the first note
inline constexpr double kMinNoteGap = 0.4;   // between the ink of neighbouring columns
inline constexpr double kEndGap = 1.0;       // after the last note, before the barline
inline constexpr double kQuarterSpace = 3.0; // ideal distance after a quarter note
inline constexpr double kEpsilon = 1e-9;

/// Ideal distance after a note that lasts `length` whole notes: each doubling of
/// the length multiplies the space by the square root of two.
[[nodiscard]] double idealSpace(const core::Fraction& length);

[[nodiscard]] inline Glyph offsetGlyph(Glyph first, int by) {
    return static_cast<Glyph>(static_cast<int>(first) + by);
}

/// The ink of the notes starting together in one staff, from the column's x
/// (the left of an unshifted notehead).
struct ColumnInk {
    core::Fraction start;   // from the start of the measure, in whole notes
    core::Fraction longest; // longest note starting here
    double left = 0.0;
    double right = 0.0;
};

struct ColumnPosition {
    core::Fraction start;
    double x = 0.0;
};

/// The x of the column starting at `start`; `positions` are in time order and
/// hold that start.
[[nodiscard]] double columnX(std::span<const ColumnPosition> positions,
                             const core::Fraction& start);

/// Where the column after the one at `start` begins, or `barline` for the last.
[[nodiscard]] double nextColumnX(std::span<const ColumnPosition> positions,
                                 const core::Fraction& start, double barline);

struct Extents {
    double top = 0.0;
    double bottom = 0.0;
};

/// The highest and lowest ink of the items of `list` and a staff `staffHeight`
/// high.
[[nodiscard]] Extents inkExtents(const DisplayList& list, const EngravingFont& font,
                                 double staffHeight);

/// Sets `list.top` and `list.bottom` from the ink of its items and a staff
/// `staffHeight` high.
void measureExtents(DisplayList& list, const EngravingFont& font, double staffHeight);

/// Moves every item of `list` down by `dy`.
void shift(DisplayList& list, double dy);

/// Appends the items of `from` to `to`.
void append(DisplayList& to, DisplayList&& from);

// ---- Technique lanes ----------------------------------------------------------------

/// Bands above a staff for technique marks, from the staff upwards. Only the
/// lanes a line of music uses take room.
enum class Lane : std::uint8_t { Bend, Vibrato, PalmMute, LetRing, Harmonic, PickStroke };
inline constexpr std::size_t kLaneCount = static_cast<std::size_t>(Lane::PickStroke) + 1;

/// The marks of one column of notes that go into lanes.
struct ColumnTechniques {
    bool palmMute = false;
    bool letRing = false;
    core::Vibrato vibrato = core::Vibrato::None;
    core::PickStroke pickStroke = core::PickStroke::None;
    core::HarmonicKind harmonic = core::HarmonicKind::None;

    [[nodiscard]] bool any() const noexcept {
        return palmMute || letRing || vibrato != core::Vibrato::None ||
               pickStroke != core::PickStroke::None || harmonic != core::HarmonicKind::None;
    }
};

/// Adds the marks of the notes of `beat` to `marks`.
void addTechniques(ColumnTechniques& marks, const core::Beat& beat);

/// Collects technique marks column by column: "P.M.", "let ring" and vibrato
/// run on while consecutive columns have them, across barlines.
class TechniqueLanes {
public:
    explicit TechniqueLanes(const EngravingFont& font);

    /// The marks of the column centred at `centre` whose notes last until `end`;
    /// columns come in time order, rests included.
    void column(const ColumnTechniques& marks, double centre, double end, std::size_t measure);

    /// Gives `lane` room even without marks (bends draw their arrows into it).
    void use(Lane lane) { used_[static_cast<std::size_t>(lane)] = true; }

    /// Whether any lane has marks or open runs.
    [[nodiscard]] bool empty() const noexcept;

    /// Whether column() has anything to do with `marks`: marks to add or runs
    /// to end.
    [[nodiscard]] bool wants(const ColumnTechniques& marks) const noexcept {
        return marks.any() || std::ranges::any_of(runs_, [](const Run& run) { return run.open; });
    }

    /// Ends open runs, places the used lanes above `top` (the highest ink of
    /// the staff, negative above it) and moves their marks into `list`.
    void place(DisplayList& list, double top);

    /// The bottom of `lane` once placed.
    [[nodiscard]] double bottom(Lane lane) const { return bottom_[static_cast<std::size_t>(lane)]; }

private:
    struct Run {
        bool open = false;
        double from = 0.0;
        double to = 0.0;
        std::size_t measure = 0;
        core::Vibrato vibrato = core::Vibrato::None;
    };

    void extend(Lane lane, bool active, double centre, double end, std::size_t measure,
                core::Vibrato vibrato = core::Vibrato::None);
    void close(Lane lane);
    DisplayList& items(Lane lane) { return items_[static_cast<std::size_t>(lane)]; }

    const EngravingFont& font_;
    std::array<DisplayList, kLaneCount> items_{}; // y from the bottom of the lane
    std::array<Run, kLaneCount> runs_{};
    std::array<bool, kLaneCount> used_{};
    std::array<double, kLaneCount> bottom_{};
};

/// "full", "1/2", "1 1/2": a bend of `cents` in tones, to the nearest quarter.
[[nodiscard]] std::string bendLabel(int cents);

// ---- Staff writers --------------------------------------------------------------------

/// Lays out one staff measure by measure while the system layout spaces the
/// columns of all its staves together. The y of the staff's top line is 0.
class StaffWriter {
public:
    StaffWriter(const EngravingFont& font, double height, std::size_t lines);
    virtual ~StaffWriter() = default;
    StaffWriter(const StaffWriter&) = delete;
    StaffWriter& operator=(const StaffWriter&) = delete;
    StaffWriter(StaffWriter&&) = delete;
    StaffWriter& operator=(StaffWriter&&) = delete;

    [[nodiscard]] double height() const noexcept { return height_; }
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_; }

    /// Clef and signatures at the start of the line; returns where they end.
    virtual double header(std::size_t first) = 0;

    /// Signature changes at the start of measure m after `x`; returns where
    /// they end (`x` when there are none).
    virtual double changes(std::size_t m, double x) = 0;

    /// Reads measure m; puts the ink of its columns into `ink` in time order,
    /// none for a measure without notes.
    virtual void collect(std::size_t m, std::vector<ColumnInk>& ink) = 0;

    /// What a measure without notes needs between its gaps.
    [[nodiscard]] virtual double emptyWidth() const = 0;

    /// Draws the measure read last with its columns at `positions`, the
    /// measure running from `start` to its barline.
    virtual void draw(std::span<const ColumnPosition> positions, double start, double barline) = 0;

    /// The barline closing measure m at `x` (thin-thick at the song's end);
    /// returns its right.
    double barline(std::size_t m, double x, bool songEnd);

    /// Staff lines over `width`, the marks still open, the ink extents.
    DisplayList finish(double width);

protected:
    /// Draws what waits for the end of the line (technique lanes, open runs).
    virtual void close() {}

    // Inline: layout draws tens of thousands of glyphs and lines.
    void glyph(Glyph which, double x, double y, Role role) {
        list_.glyphs.push_back(
            GlyphItem{.glyph = which, .origin = {x, y}, .role = role, .measure = measure_});
    }

    /// Draws `which` with the left of its box at `x`; returns the right of the box.
    double glyphFrom(Glyph which, double x, double y, Role role) {
        const BoundingBox& box = metrics(which).box;
        glyph(which, x - box.southWest.x, y, role);
        return x + box.width();
    }

    /// Draws `which` with its box centred on (x, y).
    void glyphCentred(Glyph which, double x, double y, Role role);
    void line(Point from, Point to, double thickness, Role role, double dash = 0.0) {
        list_.lines.push_back(LineItem{.from = from,
                                       .to = to,
                                       .thickness = thickness,
                                       .role = role,
                                       .measure = measure_,
                                       .dash = dash});
    }

    void text(std::string content, Point origin, double size, Role role,
              TextAlign align = TextAlign::Centre, bool knockout = false);
    void curve(Point from, Point to, double height, double thickness, double endThickness,
               Role role);
    void polygon(std::vector<Point> points, Role role);

    [[nodiscard]] const GlyphMetrics& metrics(Glyph which) const { return font_.metrics(which); }
    [[nodiscard]] double widthOf(Glyph which) const { return metrics(which).box.width(); }

    /// A time signature after `x` with its digits centred on `middle`; returns
    /// its right.
    double timeSignature(const core::TimeSignature& time, double x, double middle);

    /// The highest ink drawn so far, never below the top line.
    [[nodiscard]] double inkTop() const;

    const EngravingFont& font_;
    const EngravingDefaults& defaults_;
    DisplayList list_;
    std::size_t measure_ = 0;

private:
    double height_;
    std::size_t lines_;
};

/// Standard notation of a staff; with `techniques`, palm mutes, vibrato and the
/// other technique marks go above it (when no tablature shows them).
[[nodiscard]] std::unique_ptr<StaffWriter> makeStandardWriter(const core::Song& song,
                                                              std::size_t track, std::size_t staff,
                                                              const EngravingFont& font,
                                                              bool techniques);

/// Tablature of a fretted staff; with `rests`, rests show in it (when no
/// standard notation of the staff shows them).
[[nodiscard]] std::unique_ptr<StaffWriter> makeTabWriter(const core::Song& song, std::size_t track,
                                                         std::size_t staff,
                                                         const EngravingFont& font, bool rests);

} // namespace elara::notation::detail
