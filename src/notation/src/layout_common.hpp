// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: what the staff writers share and the interface the system layout
// drives them through (ADR-0012).
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/signature.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace elara::core {
class Song;
class Staff;
} // namespace elara::core

namespace elara::notation::detail {

// ---- Spacing, in staff spaces (ADR-0012) ------------------------------------------

inline constexpr double kClefX = 0.6;        // left edge of the clef
inline constexpr double kHeaderGap = 0.9;    // between clef, key and time signatures
inline constexpr double kFirstNoteGap = 1.6; // after the header, before the first note
inline constexpr double kBarlineGap = 1.2;   // after a barline, before the first note
inline constexpr double kRepeatGap =
    0.9; // after a header or signature changes, before a start repeat
inline constexpr double kMinNoteGap = 0.4;   // between the ink of neighbouring columns
inline constexpr double kEndGap = 1.0;       // after the last note, before the barline
inline constexpr double kQuarterSpace = 3.0; // ideal distance after a quarter note
inline constexpr double kEpsilon = 1e-9;

// Text extents as layout assumes them: ascenders (and parentheses) reach
// 0.75 em above the baseline, descenders 0.21 em below.
inline constexpr double kTextAscent = 0.75;
inline constexpr double kTextDescent = 0.21;

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

// ---- Beams and tuplets (ADR-0012, ADR-0040) ---------------------------------------

inline constexpr double kBeamletLength = 1.1;    // a beam that joins no other stem
inline constexpr double kTupletGap = 0.4;        // between a tuplet number or bracket and the notes
inline constexpr double kTupletHook = 0.6;       // the ends of a tuplet bracket, towards the notes
inline constexpr double kTupletNumberGap = 0.25; // either side of the number in its bracket

/// Flags or beams a note value needs (0 for a quarter or longer).
[[nodiscard]] inline int beamCount(core::NoteValue value) {
    return std::max(0, static_cast<int>(value) - static_cast<int>(core::NoteValue::Quarter));
}

/// A note or rest of one voice, as beams and tuplets see it.
struct RhythmItem {
    core::Fraction start; // from the start of the measure
    core::Duration duration;
    bool rest = false;
};

/// The beam groups of one voice: two or more consecutive eighths and shorter
/// inside one beat of the meter (a dotted beat in compound meters), as indices
/// into `items`.
[[nodiscard]] std::vector<std::vector<std::size_t>> beamGroups(std::span<const RhythmItem> items,
                                                               const core::TimeSignature& time);

/// Consecutive items of one voice under one tuplet number, as indices.
struct TupletSpan {
    std::size_t first = 0;
    std::size_t last = 0;
    core::Tuplet tuplet;
};

/// The tuplets of one voice: consecutive items with the same ratio, a group
/// closing once its written length is `actual` times a note value no shorter
/// than any of its items (three eighths, or a quarter and an eighth, under a 3).
[[nodiscard]] std::vector<TupletSpan> tupletSpans(std::span<const RhythmItem> items);

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

/// Moves every item and box of `list` right by `dx` and down by `dy`.
void shift(DisplayList& list, double dx, double dy);

/// Appends the items and boxes of `from` to `to`.
void append(DisplayList& to, DisplayList&& from);

// ---- Technique lanes ----------------------------------------------------------------

/// Bands above a staff for technique marks, from the staff upwards. Only the
/// lanes a line of music uses take room.
enum class Lane : std::uint8_t {
    Bend,
    Vibrato,
    PalmMute,
    LetRing,
    Harmonic,
    PickStroke,
    Strum,
    Tremolo
};
inline constexpr std::size_t kLaneCount = static_cast<std::size_t>(Lane::Tremolo) + 1;

/// The marks of one column of notes that go into lanes.
struct ColumnTechniques {
    bool palmMute = false;
    bool letRing = false;
    core::Vibrato vibrato = core::Vibrato::None;
    core::PickStroke pickStroke = core::PickStroke::None;
    core::Strum strum = core::Strum::None;
    int tremoloStrokes = 0; ///< slashes of tremolo picking, 0 without it
    core::HarmonicKind harmonic = core::HarmonicKind::None;

    [[nodiscard]] bool any() const noexcept {
        return palmMute || letRing || vibrato != core::Vibrato::None ||
               pickStroke != core::PickStroke::None || strum != core::Strum::None ||
               tremoloStrokes > 0 || harmonic != core::HarmonicKind::None;
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

    // A glyph centred at `centre`, its ink just above the bottom of `lane`.
    void mark(Lane lane, Glyph glyph, double centre, std::size_t measure, double scale = 1.0);
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

// ---- Dynamics -----------------------------------------------------------------------

/// The mark of a dynamic ("mf": dynamicMF).
[[nodiscard]] Glyph dynamicGlyph(core::Dynamic dynamic) noexcept;

/// Makes `loudest` the loudest of itself and the notes of `beat` that are
/// struck: tied notes only sustain.
void addStruck(std::optional<core::Dynamic>& loudest, const core::Beat& beat);

/// The dynamic in force at the start of measure m of `staff`: the loudest of
/// the last notes struck together before it, mf before the first.
[[nodiscard]] core::Dynamic dynamicBefore(const core::Staff& staff, std::size_t m);

/// Dynamics marks under a staff (ADR-0044): a mark under the notes whose
/// dynamic differs from the one in force, all of a line on one baseline.
class DynamicMarks {
public:
    explicit DynamicMarks(const EngravingFont& font) : font_(font) {}

    /// Starts a line at measure m of `staff`: forgets the marks; the dynamic in
    /// force is looked up when notes are first struck (measuring strikes none).
    void start(const core::Staff& staff, std::size_t m) {
        staff_ = &staff;
        first_ = m;
        known_ = false;
        marks_.clear();
    }

    /// Notes struck together at `dynamic`, centred at `centre`; columns come in
    /// time order.
    void strike(core::Dynamic dynamic, double centre, std::size_t measure);

    /// Whether the line has no marks.
    [[nodiscard]] bool empty() const noexcept { return marks_.empty(); }

    /// Places the marks under `bottom` (the lowest ink of the staff) and
    /// `staffHeight`, left of `end` (the end of the line), and draws them into
    /// `list`.
    void place(DisplayList& list, double bottom, double staffHeight, double end);

private:
    struct Mark {
        core::Dynamic dynamic = core::Dynamic::MF;
        double centre = 0.0;
        std::size_t measure = 0;
    };

    const EngravingFont& font_;
    const core::Staff* staff_ = nullptr;
    std::size_t first_ = 0;
    bool known_ = false;
    core::Dynamic current_ = core::Dynamic::MF;
    std::vector<Mark> marks_;
    std::vector<double> origins_; // of the marks being placed
};

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

    /// The barline closing measure m at `x`: thin, thin-thick at the song's
    /// end, dots and thin-thick for an end repeat. Returns its right.
    double barline(std::size_t m, double x, bool songEnd, bool repeatEnd);

    /// The start repeat of measure m, thick-thin and dots, from `x`; returns
    /// its right.
    double opening(std::size_t m, double x);

    /// Staff lines over `width`, the marks still open, the ink extents. Ends the
    /// line: the writer can start another with header().
    DisplayList finish(double width);

    /// Forgets what was drawn, keeping the buffers: measuring calls header(),
    /// changes(), collect() and barline() for their widths only.
    void clearItems();

protected:
    /// Draws what waits for the end of the line ending at `width` (technique
    /// lanes, ties into the next line).
    virtual void close(double /*width*/) {}

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

    /// The lowest ink drawn so far, never above the bottom line.
    [[nodiscard]] double inkBottom() const;

    /// The digits of a tuplet number with their width and extents (SMuFL y, up).
    struct TupletNumber {
        std::string digits;
        double width = 0.0;
        double top = 0.0;
        double bottom = 0.0;
    };
    [[nodiscard]] TupletNumber tupletNumber(int count) const;

    /// Draws `number` centred on x with its baseline at `origin`.
    void drawTupletNumber(const TupletNumber& number, double x, double origin);

    /// How far from the nearest ink of its notes a tuplet bracket lies.
    [[nodiscard]] static double bracketReach(const TupletNumber& number) {
        return kTupletGap + std::max(kTupletHook, (number.top - number.bottom) / 2);
    }

    /// A tuplet bracket from `left` to `right` at y with hooks towards the notes
    /// (down when `above`) and `number` in a gap in its middle.
    void tupletBracket(const TupletNumber& number, double left, double right, double y, bool above);

    const EngravingFont& font_;
    const EngravingDefaults& defaults_;
    DisplayList list_;
    std::size_t measure_ = 0;

private:
    /// The two dots of a repeat barline from `x`, either side of the middle of
    /// the staff in a space; returns their right.
    double repeatDots(double x);

    double height_;
    std::size_t lines_;
};

/// Standard notation of a staff; with `techniques`, palm mutes, vibrato and the
/// other technique marks go above it (when no tablature shows them).
[[nodiscard]] std::unique_ptr<StaffWriter> makeStandardWriter(const core::Song& song,
                                                              std::size_t track, std::size_t staff,
                                                              const EngravingFont& font,
                                                              bool techniques);

/// Tablature of a fretted staff; `alone` (when no standard notation of the
/// staff shows them), its rests and rhythm show in it (ADR-0041).
[[nodiscard]] std::unique_ptr<StaffWriter> makeTabWriter(const core::Song& song, std::size_t track,
                                                         std::size_t staff,
                                                         const EngravingFont& font, bool alone);

// ---- Lines of music --------------------------------------------------------------------

/// The writers of the selected staves, top to bottom. Throws
/// std::invalid_argument for no staves or tablature of a staff without tuning,
/// std::out_of_range for a missing track or staff.
[[nodiscard]] std::vector<std::unique_ptr<StaffWriter>>
makeWriters(const core::Song& song, std::span<const StaffSelection> staves,
            const EngravingFont& font);

/// Systems tighten their spacing at most this share of the ideal spaces.
inline constexpr double kMaxShrink = 0.2;

/// A space between columns: `ideal` for the rhythm, at least `ink` for the
/// notes on either side.
struct Spring {
    double ideal = 0.0;
    double ink = 0.0;
};

/// The spring's length at `stretch`: from 0 (natural: the ideal space unless
/// the ink needs more) up, ideal spaces grow by `stretch` of themselves; down
/// to -kMaxShrink they shrink, never below the ink.
[[nodiscard]] inline double springLength(const Spring& spring, double stretch) {
    return stretch >= 0.0 ? std::max(spring.ideal, spring.ink) + stretch * spring.ideal
                          : std::max(spring.ideal * (1.0 + stretch), spring.ink);
}

/// How wide a measure is at any stretch, and what goes before and after it.
struct MeasureWidth {
    double header = 0.0;         // clef and signatures, when a system starts with it
    double changes = 0.0;        // signature changes after the barline before it, otherwise
    double lead = 0.0;           // after the gap at its start, before the first column
    double tail = 0.0;           // after the last spring, before the barline
    std::vector<Spring> springs; // after each column, the last one up to the barline
    double barline = 0.0;        // the barline closing it
    double opening = 0.0;        // its start repeat after a header or changes, gap included
    double openingInline = 0.0;  // its start repeat over the barline before it

    /// What comes before the gap at its start: the header when a system starts
    /// with it (`first`), otherwise the signature changes; then its start repeat.
    [[nodiscard]] double before(bool first) const {
        if (first) {
            return header + opening;
        }
        return changes > 0.0 ? changes + opening : openingInline;
    }

    /// From the end of the gap at its start to its barline.
    [[nodiscard]] double width(double stretch) const {
        double total = lead + tail;
        for (const Spring& spring : springs) {
            total += springLength(spring, stretch);
        }
        return total;
    }

    /// The sum of its ideal spaces: how much it grows per unit of stretch.
    [[nodiscard]] double ideal() const {
        double total = 0.0;
        for (const Spring& spring : springs) {
            total += spring.ideal;
        }
        return total;
    }
};

/// The widths of measures [first, last) of the selected staves, on up to
/// `threads` threads.
[[nodiscard]] std::vector<MeasureWidth> measureWidths(const core::Song& song,
                                                      std::span<const StaffSelection> staves,
                                                      const EngravingFont& font, std::size_t first,
                                                      std::size_t last, unsigned threads);

/// How a system is drawn besides its measures.
struct LineOptions {
    double stretch = 0.0;  ///< 0: natural spacing
    bool numbered = false; ///< the number of its first measure above it
    bool named = false;    ///< track names left of the staves (see nameIndent())
};

/// Room for the names of the tracks left of the first system's staves: 0 for
/// staves of a single track.
[[nodiscard]] double nameIndent(const core::Song& song, std::span<const StaffSelection> staves);

/// Lays out measures [first, last) of the selected staves as one system.
[[nodiscard]] DisplayList layoutLine(const core::Song& song, std::span<const StaffSelection> staves,
                                     const EngravingFont& font, std::size_t first, std::size_t last,
                                     const LineOptions& options);

} // namespace elara::notation::detail
