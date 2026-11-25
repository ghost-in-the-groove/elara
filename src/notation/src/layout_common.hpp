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
#include <utility>
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
inline constexpr double kGraceScale = 0.65;  // of grace notes and their fret numbers (ADR-0079)
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
    // A chord symbol above the column (ADR-0080): what it reaches, when `above`.
    // Chord symbols keep apart from each other, not from the notes under them.
    double aboveLeft = 0.0;
    double aboveRight = 0.0;
    bool above = false;
    // Syllables under the column (ADR-0084): what they reach with the hyphens
    // after them, when `below`. They keep apart from each other, not from the
    // notes over them.
    double belowLeft = 0.0;
    double belowRight = 0.0;
    bool below = false;
    // The sustain pedal under the column (ADR-0092): what "Ped.", a change's
    // notch or the end of its line reaches, when `pedal`. They keep apart from
    // each other and the barline, not from the notes over them.
    double pedalLeft = 0.0;
    double pedalRight = 0.0;
    bool pedal = false;
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

/// The same for the ink between x = `left` and `right` only: of the items
/// that reach into that span, what lies over it (a slanted beam or line where
/// it crosses it).
[[nodiscard]] Extents inkExtents(const DisplayList& list, const EngravingFont& font,
                                 double staffHeight, double left, double right);

/// Sets `list.top` and `list.bottom` from the ink of its items and a staff
/// `staffHeight` high.
void measureExtents(DisplayList& list, const EngravingFont& font, double staffHeight);

/// Moves every item and box of `list` right by `dx` and down by `dy`.
void shift(DisplayList& list, double dx, double dy);

/// Appends the items and boxes of `from` to `to`.
void append(DisplayList& to, DisplayList&& from);

/// Appends copies of the items and boxes of `from` to `to`, moved right by `dx`
/// and down by `dy`: what shift() and append() give, in one pass and leaving
/// `from` as it is.
void appendShifted(DisplayList& to, const DisplayList& from, double dx, double dy);

/// Makes room in `to` for the items and boxes of every list of `from` besides
/// its own.
void reserveFor(DisplayList& to, std::span<const DisplayList* const> from);

// ---- Technique lanes ----------------------------------------------------------------

/// Bands above a staff for technique marks, from the staff upwards. Only the
/// lanes a line of music uses take room.
enum class Lane : std::uint8_t {
    Bend,
    Whammy,
    Vibrato,
    Ornament, ///< "tr" and the other note's fret (ADR-0090), a mordent or a turn (ADR-0091)
    PalmMute,
    LetRing,
    Harmonic,
    Bass,
    PickStroke,
    Strum,
    Tremolo
};
inline constexpr std::size_t kLaneCount = static_cast<std::size_t>(Lane::Tremolo) + 1;

/// The glyph of a mordent or a turn (ADR-0091).
[[nodiscard]] constexpr Glyph ornamentGlyph(core::Ornament ornament) noexcept {
    switch (ornament) {
    case core::Ornament::Mordent:
        return Glyph::OrnamentMordent;
    case core::Ornament::UpperMordent:
        return Glyph::OrnamentShortTrill;
    case core::Ornament::Turn:
        return Glyph::OrnamentTurn;
    case core::Ornament::InvertedTurn:
        return Glyph::OrnamentTurnInverted;
    case core::Ornament::None:
        break;
    }
    return Glyph::OrnamentTrill;
}

/// The marks of one column of notes that go into lanes.
struct ColumnTechniques {
    bool palmMute = false;
    bool letRing = false;
    core::Vibrato vibrato = core::Vibrato::None;
    core::PickStroke pickStroke = core::PickStroke::None;
    core::Strum strum = core::Strum::None;
    int tremoloStrokes = 0; ///< slashes of tremolo picking, 0 without it
    core::HarmonicKind harmonic = core::HarmonicKind::None;
    core::BendCurve whammy{}; ///< the whammy bar, empty without it (ADR-0061)
    core::BassTechnique bass = core::BassTechnique::None;
    int trillFret = -1; ///< the fret a trill goes to (ADR-0090), -1 without one
    core::Ornament ornament = core::Ornament::None; ///< a mordent or a turn (ADR-0091)

    [[nodiscard]] bool any() const noexcept {
        return palmMute || letRing || vibrato != core::Vibrato::None || trillFret >= 0 ||
               ornament != core::Ornament::None || pickStroke != core::PickStroke::None ||
               strum != core::Strum::None || tremoloStrokes > 0 ||
               harmonic != core::HarmonicKind::None || !whammy.isEmpty() ||
               bass != core::BassTechnique::None;
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
        return marks.any() || whammyRun_ ||
               std::ranges::any_of(runs_, [](const Run& run) { return run.open; });
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
    // The whammy bar of the column from `centre` to `end` (ADR-0061).
    void whammy(const core::BendCurve& curve, double centre, double end, std::size_t measure);
    DisplayList& items(Lane lane) { return items_[static_cast<std::size_t>(lane)]; }

    const EngravingFont& font_;
    std::array<DisplayList, kLaneCount> items_{}; // y from the bottom of the lane
    std::array<Run, kLaneCount> runs_{};
    std::array<bool, kLaneCount> used_{};
    std::array<double, kLaneCount> bottom_{};
    bool whammyRun_ = false; // the last column had a whammy bar
};

/// "full", "1/2", "1 1/2": a bend of `cents` in tones, to the nearest quarter.
[[nodiscard]] std::string bendLabel(int cents);

/// "-1 1/2", "+1/2": how far a whammy bar goes from the written pitch, in
/// tones to the nearest quarter, with its direction; empty under an eighth.
[[nodiscard]] std::string whammyLabel(int cents);

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
        wedges_.clear();
    }

    /// Notes struck together at `dynamic`, centred at `centre`; columns come in
    /// time order.
    void strike(core::Dynamic dynamic, double centre, std::size_t measure);

    /// The hairpins of `measure`, measure m of the staff: wedges from the
    /// column of their first beat to the column after their last, or to
    /// `barline`; measures come in order (ADR-0078).
    void hairpins(const core::Measure& measure, std::size_t m,
                  std::span<const ColumnPosition> positions, double barline);

    /// Whether the line has no marks and no hairpins.
    [[nodiscard]] bool empty() const noexcept { return marks_.empty() && wedges_.empty(); }

    /// Places the marks and hairpins under `bottom` (the lowest ink of the
    /// staff) and `staffHeight`, left of `end` (the end of the line), and draws
    /// them into `list`.
    void place(DisplayList& list, double bottom, double staffHeight, double end);

private:
    struct Mark {
        core::Dynamic dynamic = core::Dynamic::MF;
        double centre = 0.0;
        std::size_t measure = 0;
    };

    struct Span {
        core::Fraction start;
        core::Fraction end;
        core::Hairpin hairpin = core::Hairpin::None;
    };

    struct Wedge {
        core::Hairpin hairpin = core::Hairpin::None;
        double left = 0.0;
        double right = 0.0;
        std::size_t measure = 0; // where it starts
        std::size_t last = 0;    // where it ends
        bool toBarline = false;  // ends at the barline of `last`
        bool continued = false;  // from the line before
    };

    void placeWedges(DisplayList& list, double middle, double end);

    const EngravingFont& font_;
    const core::Staff* staff_ = nullptr;
    std::size_t first_ = 0;
    bool known_ = false;
    core::Dynamic current_ = core::Dynamic::MF;
    std::vector<Mark> marks_;
    std::vector<double> origins_; // of the marks being placed
    std::vector<Span> spans_;     // of the measure being read
    std::vector<Wedge> wedges_;
};

// ---- Chord symbols and beat text (ADR-0080, ADR-0081) ------------------------------------

inline constexpr double kChordGap = 0.8; // between neighbouring chord symbols
inline constexpr double kChordBarlineReach =
    0.6;                                    // how far one reaches into the gap after a barline
inline constexpr double kChordEndGap = 0.3; // between one and the barline after it

/// What is written above a staff over its beats. Chord symbols: each name
/// centred over its column with its diagram under it on staves with frets, all
/// of a line on one baseline above the technique lanes. Beat texts above them,
/// each starting at its column; one that would run into the text before it
/// goes on a row above. Texts take no room between the columns.
class AboveRow {
public:
    /// `diagrams`: the staff has frets, so diagrams show.
    AboveRow(const EngravingFont& font, bool diagrams) : font_(font), diagrams_(diagrams) {}

    /// Reads the chord symbols and texts of `measure`, one of each per start
    /// (the first voice's), and gives the columns of `ink` (in time order) the
    /// chord symbols' reach; a column with no ink is added where one starts and
    /// no notes do. `centre` is where a symbol's middle lies from its column's x.
    void collect(const core::Measure& measure, std::vector<ColumnInk>& ink, double centre);

    /// What was read last, with its columns at `positions`.
    void draw(std::span<const ColumnPosition> positions, double centre, std::size_t measure);

    [[nodiscard]] bool empty() const noexcept { return marks_.empty() && texts_.empty(); }

    /// Places the line's chord symbols above `top` (the highest ink of the
    /// staff, negative above it) and its texts above them, draws them into
    /// `list` and forgets them.
    void place(DisplayList& list, double top);

private:
    struct Read {
        core::Fraction start;
        core::Fraction length; // of its beat
        const core::ChordSymbol* symbol = nullptr;
        const std::string* text = nullptr;
    };

    struct Mark {
        const core::ChordSymbol* symbol = nullptr;
        double centre = 0.0;
        std::size_t measure = 0;
    };

    struct TextMark {
        const std::string* text = nullptr;
        double left = 0.0;
        std::size_t measure = 0;
    };

    // Places the line's texts above `top`, rows upwards.
    void placeTexts(DisplayList& list, double top);

    [[nodiscard]] bool showsDiagram(const core::ChordSymbol& symbol) const noexcept {
        return diagrams_ && symbol.diagram && !symbol.diagram->frets.empty();
    }

    // How far `symbol` reaches left and right of its middle (both positive).
    [[nodiscard]] std::pair<double, double> reach(const core::ChordSymbol& symbol) const;

    // The diagram centred at `centre` from `top` down.
    void diagram(DisplayList& list, const core::ChordDiagram& diagram, double centre, double top,
                 std::size_t measure) const;

    const EngravingFont& font_;
    bool diagrams_;
    std::vector<Read> read_;      // of the measure read last
    std::vector<Mark> marks_;     // of the line
    std::vector<TextMark> texts_; // of the line
};

// ---- Lyrics (ADR-0084) -----------------------------------------------------------------

// The sustain pedal under a staff (ADR-0092).
inline constexpr double kPedalLineGap = 0.3; // between "Ped." and its line
inline constexpr double kPedalEndGap = 0.3;  // the line ends before the column where it comes up
inline constexpr double kPedalNotch = 0.4;   // half the width of a change's notch
inline constexpr double kPedalRoom = 0.5;    // of line at least between two of these

inline constexpr double kLyricGap = 0.6;    // between neighbouring syllables
inline constexpr double kLyricEndGap = 0.3; // between a syllable and the barline after it
inline constexpr double kLyricBarlineReach =
    0.6; // how far one reaches into the gap after a barline

/// The lyrics under a staff. Each verse has its own line under the staff's
/// ink, in order; a syllable is centred under its note, or starts at the
/// notehead when it is held. A hyphen goes between the syllables of a word,
/// and after the last syllable of a line when its word goes on in the next. A
/// held syllable has a line under the notes it is held over, to the end of the
/// line when they go on, and from the first note of the next.
class LyricsRow {
public:
    /// The lyrics of `staff`.
    explicit LyricsRow(const core::Staff& staff) : staff_(staff) {}

    /// Starts a line at measure m: forgets the syllables. The held syllables of
    /// the measures before, whose notes go on into it, are found when the
    /// line's first measure is drawn (measuring draws none).
    void start(std::size_t m);

    /// Reads the syllables of measure m, one per verse and start, and gives
    /// the columns of `ink` (in time order) their reach; a column with no ink is
    /// added where a syllable starts and no notes do. `centre` is the middle of
    /// a notehead from its column's x.
    void collect(std::size_t m, std::vector<ColumnInk>& ink, double centre);

    /// What was read last, with its columns at `positions`.
    void draw(std::span<const ColumnPosition> positions, double centre);

    [[nodiscard]] bool empty() const noexcept { return syllables_.empty() && held_.empty(); }

    /// Places the verses under `bottom` (the lowest ink of the staff, at least
    /// `staffHeight`), the line ending at `end`, draws them into `list` and
    /// forgets them.
    void place(DisplayList& list, double bottom, double staffHeight, double end);

private:
    /// Where a held syllable's notes end: the start of the last of them.
    struct Spot {
        std::size_t measure = 0;
        core::Fraction start;
    };

    struct Read {
        core::Fraction start;
        core::Fraction length; // of its beat
        std::size_t verse = 0;
        std::size_t voice = 0;
        std::size_t beat = 0; // in its voice
        const core::Syllable* syllable = nullptr;
    };

    struct Sung {
        const core::Syllable* syllable = nullptr;
        std::size_t verse = 0;
        double left = 0.0;
        double width = 0.0;
        std::size_t measure = 0;
        std::optional<Spot> heldTo{}; // when held over notes
    };

    /// A syllable held over from the line before: its line goes from the first
    /// note of the line to the end of the notes at `to`.
    struct Held {
        std::size_t verse = 0;
        Spot to{};
        std::size_t measure = 0;
    };

    /// The columns of a measure drawn on the line.
    struct Drawn {
        std::size_t measure = 0;
        std::vector<ColumnPosition> positions{};
    };

    /// The last note a syllable on beat `beat` of `voice` in measure m is held
    /// over, before the next syllable of `verse`, a rest or the end; nullopt
    /// when none follows it.
    [[nodiscard]] std::optional<Spot> heldTo(std::size_t m, std::size_t voice, std::size_t beat,
                                             std::size_t verse) const;

    /// The x of the end of the notehead at `spot`, or `end` past the line.
    [[nodiscard]] double noteEnd(const Spot& spot, double end) const;

    /// Finds the held syllables of the measures before the line's first.
    void findHeld();

    const core::Staff& staff_;
    std::size_t first_ = 0;  // the line's first measure
    bool heldKnown_ = false; // findHeld() ran for the line
    double headWidth_ = 0.0;
    std::vector<Read> read_{};
    std::size_t readMeasure_ = 0;
    std::vector<Sung> syllables_{}; // of the line
    std::vector<Held> held_{};      // of the line
    std::vector<Drawn> drawn_{};    // of the line
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

    /// What closes measure m before its barline, from `x`: the clef the next
    /// measure changes to (ADR-0087). Returns where it ends (`x` for nothing).
    virtual double ending(std::size_t /*m*/, double x) { return x; }

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
    void glyph(Glyph which, double x, double y, Role role, double scale = 1.0) {
        list_.glyphs.push_back(GlyphItem{
            .glyph = which, .origin = {x, y}, .role = role, .measure = measure_, .scale = scale});
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

    /// The highest and lowest ink drawn so far between x = left and right.
    [[nodiscard]] Extents inkBetween(double left, double right) const;

    /// A fermata centred on x = `centre` (ADR-0085): above the staff and the
    /// ink drawn over it there, or under them when `below`.
    void fermata(core::Fermata fermata, double centre, bool below);

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

/// Room for a brace left of every system's staves: 0 unless two or more
/// staves of one track show standard notation one after the other (a grand
/// staff, such as a piano's).
[[nodiscard]] double braceRoom(std::span<const StaffSelection> staves);

/// Lays out measures [first, last) of the selected staves as one system.
[[nodiscard]] DisplayList layoutLine(const core::Song& song, std::span<const StaffSelection> staves,
                                     const EngravingFont& font, std::size_t first, std::size_t last,
                                     const LineOptions& options);

} // namespace elara::notation::detail
