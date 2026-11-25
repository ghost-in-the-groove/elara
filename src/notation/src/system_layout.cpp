// SPDX-License-Identifier: AGPL-3.0-or-later
// A system: staves laid out together, their columns spaced as one (ADR-0012),
// at their natural spacing or stretched to a width (ADR-0014).
#include <elara/core/fraction.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>
#include <elara/notation/staff_layout.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::notation {

namespace detail {

namespace {

using core::Fraction;

constexpr double kStaffDistance = 4.0;     // at least between the lines of neighbouring staves
constexpr double kStaffInkGap = 1.0;       // at least between their ink
constexpr double kMeasureNumberSize = 1.2; // font size of a system's first measure number
constexpr double kMeasureNumberY = -2.0;   // its baseline, above the clef
constexpr double kTrackNameSize = 1.6;     // font size of the track names of the first system
constexpr double kTrackNameGap = 1.0;      // between a track name and its staves
constexpr double kVoltaLowest = -3.0;      // the bracket of an alternate ending at least this high
constexpr double kVoltaHook = 2.0;         // the length of its hooks
constexpr double kVoltaInset = 0.2;        // between its hooks and the barlines
constexpr double kVoltaLabelInset = 0.4;   // between its start hook and its numbers
constexpr double kVoltaSize = 1.4;         // font size of its numbers and of repeat counts
constexpr double kVoltaTextGap = 0.3;      // between the bracket and its numbers
constexpr double kVoltaInkGap = 0.5;       // at least between its numbers and the staff's ink
constexpr double kTempoLowest = -1.5;      // the baseline of a tempo mark at least this high
constexpr double kTempoInkGap = 0.6;       // at least between a tempo mark and the ink below
constexpr double kTempoSize = 1.4;         // font size of its text
constexpr double kTempoNoteScale = 0.65;   // of its note, from the staff's size
constexpr double kTempoNoteLift = 0.35;    // the note's head above the text's baseline
constexpr double kTempoDotGap = 0.15;      // between the note and its dot
constexpr double kTempoTextGap = 0.3;      // between the note (or dot) and "= 120"
constexpr double kRampWordGap = 0.6;       // between "= 120" and "accel."
constexpr double kRampGap = 0.5;           // around the dashed line of a gradual change
constexpr double kRampMinLine = 1.0;       // shorter dashed lines are left out
constexpr double kRampLift = 0.35;         // the dashed line above the baseline
constexpr double kRampThickness = 0.1;
constexpr double kRampDash = 0.6;           // length of a dash and of a gap
constexpr double kNavigationLowest = -1.5;  // the line of signs and jumps at least this high
constexpr double kNavigationInkGap = 0.6;   // at least between them and the ink below
constexpr double kNavigationSize = 1.4;     // font size of a jump's text
constexpr double kNavigationScale = 0.65;   // of a segno or coda, from the staff's size
constexpr double kNavigationInset = 0.4;    // between the barline and the first sign
constexpr double kNavigationGap = 0.25;     // between two signs
constexpr double kNavigationTextGap = 1.0;  // between two texts closing one measure
constexpr double kNavigationLineGap = 0.3;  // between a line of them and the one above
constexpr double kSectionLowest = -2.0;     // the bottom of a section's box at least this high
constexpr double kSectionInkGap = 0.6;      // at least between its box and the ink below
constexpr double kSectionSize = 1.5;        // font size of its name
constexpr double kSectionPad = 0.35;        // between the name and its box
constexpr double kSectionGap = 0.5;         // between two boxes on one line
constexpr double kSectionLineGap = 0.4;     // between a line of boxes and the one above
constexpr double kBraceRoom = 1.8;          // left of a system's staves for a grand staff's brace
constexpr double kBraceGap = 0.3;           // between the brace and the line starting the system
constexpr std::size_t kMeasuresPerJob = 32; // measured together on one thread
constexpr std::size_t kParallelMeasures = 64;

// The columns of every staff merged by start into `merged`: a column holds
// the ink of all.
void merge(const std::vector<std::vector<ColumnInk>>& staves, std::vector<ColumnInk>& all,
           std::vector<ColumnInk>& merged) {
    merged.clear();
    if (staves.size() == 1) {
        merged = staves.front();
        return;
    }
    all.clear();
    for (const std::vector<ColumnInk>& columns : staves) {
        all.insert(all.end(), columns.begin(), columns.end());
    }
    std::ranges::stable_sort(all, {}, &ColumnInk::start);
    for (const ColumnInk& column : all) {
        if (merged.empty() || merged.back().start != column.start) {
            merged.push_back(column);
            continue;
        }
        ColumnInk& into = merged.back();
        into.left = std::min(into.left, column.left);
        into.right = std::max(into.right, column.right);
        into.longest = std::max(into.longest, column.longest);
        if (column.above) {
            into.aboveLeft =
                into.above ? std::min(into.aboveLeft, column.aboveLeft) : column.aboveLeft;
            into.aboveRight =
                into.above ? std::max(into.aboveRight, column.aboveRight) : column.aboveRight;
            into.above = true;
        }
        if (column.below) {
            into.belowLeft =
                into.below ? std::min(into.belowLeft, column.belowLeft) : column.belowLeft;
            into.belowRight =
                into.below ? std::max(into.belowRight, column.belowRight) : column.belowRight;
            into.below = true;
        }
    }
}

// Makes springs [from, to) together at least `need` long at any stretch: each
// takes its share by its ideal space.
void stretchTo(std::vector<Spring>& springs, std::size_t from, std::size_t to, double need) {
    double ideal = 0.0;
    for (std::size_t k = from; k < to; ++k) {
        ideal += springs[k].ideal;
    }
    if (need <= 0.0 || ideal <= 0.0) {
        return;
    }
    for (std::size_t k = from; k < to; ++k) {
        springs[k].ink = std::max(springs[k].ink, need * springs[k].ideal / ideal);
    }
}

// Chord symbols keep apart from each other and from the barlines, not from the
// notes under them (ADR-0080).
void spaceChords(const std::vector<ColumnInk>& columns, MeasureWidth& width) {
    std::optional<std::size_t> previous;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (!columns[i].above) {
            continue;
        }
        if (previous) {
            stretchTo(width.springs, *previous, i,
                      columns[*previous].aboveRight + kChordGap - columns[i].aboveLeft);
        } else {
            width.lead = std::max(width.lead, -columns[i].aboveLeft - kChordBarlineReach);
        }
        previous = i;
    }
    if (previous) {
        stretchTo(width.springs, *previous, width.springs.size(),
                  columns[*previous].aboveRight + kChordEndGap);
    }
}

// Syllables keep apart from each other and from the barlines, not from the
// notes over them (ADR-0084).
void spaceLyrics(const std::vector<ColumnInk>& columns, MeasureWidth& width) {
    std::optional<std::size_t> previous;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (!columns[i].below) {
            continue;
        }
        if (previous) {
            stretchTo(width.springs, *previous, i,
                      columns[*previous].belowRight + kLyricGap - columns[i].belowLeft);
        } else {
            width.lead = std::max(width.lead, -columns[i].belowLeft - kLyricBarlineReach);
        }
        previous = i;
    }
    if (previous) {
        stretchTo(width.springs, *previous, width.springs.size(),
                  columns[*previous].belowRight + kLyricEndGap);
    }
}

// The springs of a measure `length` long with these columns: each column gets
// its ideal space unless the ink of its neighbour needs more.
void springsOf(const std::vector<ColumnInk>& columns, const Fraction& length, double emptyWidth,
               MeasureWidth& width) {
    width.springs.clear();
    if (columns.empty()) {
        width.lead = 0.0;
        width.tail = kEndGap;
        width.springs.push_back(Spring{.ideal = idealSpace(length), .ink = emptyWidth});
        return;
    }
    width.lead = std::max(0.0, -columns.front().left);
    width.tail = 0.0;
    for (std::size_t i = 0; i + 1 < columns.size(); ++i) {
        const ColumnInk& column = columns[i];
        const ColumnInk& next = columns[i + 1];
        width.springs.push_back(Spring{.ideal = idealSpace(next.start - column.start),
                                       .ink = column.right + kMinNoteGap - next.left});
    }
    const ColumnInk& last = columns.back();
    width.springs.push_back(Spring{.ideal = idealSpace(std::max(last.longest, length - last.start)),
                                   .ink = last.right + kEndGap});
    spaceChords(columns, width);
    spaceLyrics(columns, width);
}

// Positions of the columns of a measure starting at `start` at `stretch`;
// returns where the closing barline goes.
double space(const std::vector<ColumnInk>& columns, const MeasureWidth& width, double start,
             double gap, double stretch, std::vector<ColumnPosition>& positions) {
    positions.clear();
    double x = start + gap + width.lead;
    for (std::size_t i = 0; i < width.springs.size(); ++i) {
        if (i < columns.size()) {
            positions.push_back(ColumnPosition{.start = columns[i].start, .x = x});
        }
        x += springLength(width.springs[i], stretch);
    }
    return x + width.tail;
}

// The name of each track right-aligned before its staves (the staff boxes of
// `system`, `indent` from its left), centred on them.
void nameTracks(DisplayList& system, const core::Song& song, std::span<const StaffSelection> staves,
                double indent) {
    for (std::size_t s = 0; s < staves.size();) {
        std::size_t end = s + 1;
        while (end < staves.size() && staves[end].track == staves[s].track) {
            ++end;
        }
        const std::string& name = song.track(staves[s].track).name();
        if (!name.empty()) {
            const StaffBox& top = system.staves[s];
            const StaffBox& bottom = system.staves[end - 1];
            TextItem text;
            text.text = name;
            text.origin = {indent - kTrackNameGap,
                           (top.y + bottom.y + bottom.height) / 2 + capHeight(kTrackNameSize) / 2};
            text.size = kTrackNameSize;
            text.align = TextAlign::Right;
            text.role = Role::TrackName;
            text.measure = system.measures.empty() ? 0 : system.measures.front().index;
            system.texts.push_back(std::move(text));
        }
        s = end;
    }
}

// The grand staves among `staves`: runs [first, last) of two or more staves of
// one track, one after the other, in standard notation.
std::vector<std::pair<std::size_t, std::size_t>>
grandStaves(std::span<const StaffSelection> staves) {
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    for (std::size_t s = 0; s < staves.size();) {
        std::size_t end = s + 1;
        while (end < staves.size() && staves[s].view == StaffView::Standard &&
               staves[end].view == StaffView::Standard && staves[end].track == staves[s].track &&
               staves[end].staff != staves[end - 1].staff) {
            ++end;
        }
        if (end - s > 1) {
            groups.emplace_back(s, end);
        }
        s = end;
    }
    return groups;
}

// Joins each grand staff of `system` (its staff boxes are placed): every
// barline of a staff goes on down to the next staff, and a brace as high as
// them all stands left of the system's start.
void braceStaves(DisplayList& system, std::span<const StaffSelection> staves,
                 const EngravingFont& font) {
    const BoundingBox& box = font.metrics(Glyph::Brace).box;
    const double glyphHeight = box.northEast.y - box.southWest.y;
    for (const auto& [first, last] : grandStaves(staves)) {
        const std::size_t count = system.lines.size();
        for (std::size_t s = first; s + 1 < last; ++s) {
            const StaffBox& upper = system.staves[s];
            const double bottom = upper.y + upper.height;
            const double next = system.staves[s + 1].y;
            for (std::size_t i = 0; i < count; ++i) {
                const LineItem line = system.lines[i];
                if (line.role == Role::Barline && line.from.x == line.to.x &&
                    line.from.y == upper.y && line.to.y == bottom) {
                    system.lines.push_back(LineItem{.from = {line.from.x, bottom},
                                                    .to = {line.from.x, next},
                                                    .thickness = line.thickness,
                                                    .role = Role::Barline,
                                                    .measure = line.measure});
                }
            }
        }
        const double top = system.staves[first].y;
        const double bottom = system.staves[last - 1].y + system.staves[last - 1].height;
        const double scale = glyphHeight > 0.0 ? (bottom - top) / glyphHeight : 1.0;
        system.glyphs.push_back(GlyphItem{
            .glyph = Glyph::Brace,
            .origin = {-kBraceGap - box.northEast.x * scale, bottom + box.southWest.y * scale},
            .role = Role::Brace,
            .measure = system.measures.empty() ? 0 : system.measures.front().index,
            .scale = scale});
    }
}

// How far a start repeat right after the barline before measure m reaches
// back: over that barline's last line, which it replaces.
double overlap(const core::Song& song, std::size_t m, const EngravingFont& font) {
    return song.measureHeader(m - 1).repeatEnd() ? font.defaults().thickBarlineThickness
                                                 : font.defaults().thinBarlineThickness;
}

// The numbers of an alternate ending: "1., 2.".
std::string endingLabel(std::uint8_t endings) {
    std::string label;
    for (int time = 1; time <= core::MeasureHeader::kMaxEnding; ++time) {
        if (((static_cast<unsigned>(endings) >> static_cast<unsigned>(time - 1)) & 1U) != 0) {
            if (!label.empty()) {
                label += ", ";
            }
            label += std::to_string(time) + ".";
        }
    }
    return label;
}

// The brackets of alternate endings and the counts of repeats played more
// than twice above the first staff of `system`, whose measures are
// `measures`, closed by barlines ending at `barlineEnds`.
void markRepeats(DisplayList& system, const core::Song& song, std::span<const MeasureBox> measures,
                 std::span<const double> barlineEnds, const EngravingFont& font) {
    const auto endingsOf = [&song](std::size_t m) {
        return song.measureHeader(m).alternateEndings;
    };
    const bool marked = std::ranges::any_of(measures, [&song](const MeasureBox& box) {
        const core::MeasureHeader& header = song.measureHeader(box.index);
        return header.alternateEndings != 0 || header.repeatTimes > 2;
    });
    if (!marked) {
        return;
    }
    // Every bracket of the system at one height, above the first staff's ink.
    const double y =
        std::min(kVoltaLowest, system.top - kVoltaInkGap -
                                   (kTextAscent + kTextDescent) * kVoltaSize - kVoltaTextGap);
    const double baseline = y + kVoltaTextGap + kTextAscent * kVoltaSize;
    const double thickness = font.defaults().repeatEndingLineThickness;
    const auto line = [&](Point from, Point to, std::size_t m) {
        system.lines.push_back(LineItem{
            .from = from, .to = to, .thickness = thickness, .role = Role::Volta, .measure = m});
    };
    const auto text = [&](std::string content, double x, TextAlign align, Role role,
                          std::size_t m) {
        TextItem item;
        item.text = std::move(content);
        item.origin = {x, baseline};
        item.size = kVoltaSize;
        item.align = align;
        item.role = role;
        item.measure = m;
        system.texts.push_back(std::move(item));
    };
    // A run of measures with the same endings is one bracket. It has no start
    // hook or numbers when it goes on from the system before, no end hook
    // when it goes on into the next one or no end repeat closes it.
    for (std::size_t i = 0; i < measures.size();) {
        const std::size_t m = measures[i].index;
        const std::uint8_t endings = endingsOf(m);
        std::size_t end = i + 1;
        while (end < measures.size() && endingsOf(measures[end].index) == endings) {
            ++end;
        }
        if (endings != 0) {
            const MeasureBox& from = measures[i];
            const MeasureBox& to = measures[end - 1];
            const bool continued = i == 0 && m > 0 && endingsOf(m - 1) == endings;
            const bool goesOn = end == measures.size() && to.index + 1 < song.measureCount() &&
                                endingsOf(to.index + 1) == endings;
            // From the barline before it, over signature changes.
            const double left =
                continued ? from.x : (i > 0 ? barlineEnds[i - 1] : from.x) + kVoltaInset;
            const double right = to.x + to.width - (goesOn ? 0.0 : kVoltaInset);
            line({left, y}, {right, y}, m);
            if (!continued) {
                line({left, y}, {left, y + kVoltaHook}, m);
                text(endingLabel(endings), left + kVoltaLabelInset, TextAlign::Left, Role::Volta,
                     m);
            }
            if (!goesOn && song.measureHeader(to.index).repeatEnd()) {
                line({right, y}, {right, y + kVoltaHook}, to.index);
            }
        }
        i = end;
    }
    // "x3" over the end repeat, inside the end hook of an ending there.
    for (std::size_t i = 0; i < measures.size(); ++i) {
        const std::size_t m = measures[i].index;
        const core::MeasureHeader& header = song.measureHeader(m);
        if (header.repeatTimes <= 2) {
            continue;
        }
        const bool hooked = header.alternateEndings != 0 &&
                            (m + 1 == song.measureCount() || endingsOf(m + 1) != endingsOf(m));
        const double x = hooked ? measures[i].x + measures[i].width - kVoltaInset - kVoltaLabelInset
                                : barlineEnds[i];
        text("x" + std::to_string(header.repeatTimes), x, TextAlign::Right, Role::Repeat, m);
    }
    system.top = std::min(system.top, y - thickness / 2);
}

// "D.S. al Coda": where a jump goes and what ends the music after it.
std::string jumpText(const core::MeasureHeader& header) {
    constexpr std::array<std::string_view, 4> kTo{"", "D.C.", "D.S.", "D.S.S."};
    constexpr std::array<std::string_view, 4> kThen{"", " al Fine", " al Coda", " al Double Coda"};
    return std::string(kTo.at(static_cast<std::size_t>(header.jump))) +
           std::string(kThen.at(static_cast<std::size_t>(header.jumpThen)));
}

// Signs and jumps above everything over the first staff of `system`, under its
// tempo marks (ADR-0049): segno and coda signs at the start of their measure
// (two of them for segno segno and double coda), and the texts that close a
// measure ("Fine", "To Coda", "D.S. al Coda") ending at its barline, the jump
// last. They share one line; a mark that would overlap one on the line goes
// on the first line above where it fits.
void markNavigation(DisplayList& system, const core::Song& song,
                    std::span<const MeasureBox> measures, std::span<const double> barlineEnds,
                    const EngravingFont& font) {
    struct Mark {
        double left = 0.0;
        double right = 0.0;
        double height = 0.0;
        std::size_t measure = 0;
        Glyph glyph{};    // a sign, or
        std::string text; // a text when not empty
    };
    std::vector<Mark> marks;
    for (std::size_t i = 0; i < measures.size(); ++i) {
        const std::size_t m = measures[i].index;
        const core::MeasureHeader& header = song.measureHeader(m);
        // The signs, from the barline before the measure.
        double x = (i > 0 ? barlineEnds[i - 1] : measures[i].x) + kNavigationInset;
        const auto sign = [&](Glyph glyph, int count) {
            const BoundingBox& box = font.metrics(glyph).box;
            for (int c = 0; c < count; ++c) {
                const double width = box.width() * kNavigationScale;
                marks.push_back(Mark{.left = x,
                                     .right = x + width,
                                     .height = box.height() * kNavigationScale,
                                     .measure = m,
                                     .glyph = glyph,
                                     .text = {}});
                x += width + kNavigationGap;
            }
        };
        if (header.segno || header.segnoSegno) {
            sign(Glyph::Segno, header.segnoSegno ? 2 : 1);
        }
        if (header.coda || header.doubleCoda) {
            sign(Glyph::Coda, header.doubleCoda ? 2 : 1);
        }
        // The texts, right to left from the barline after it.
        double right = barlineEnds[i];
        const auto text = [&](std::string content) {
            const double width = textWidth(content, kNavigationSize);
            marks.push_back(Mark{.left = right - width,
                                 .right = right,
                                 .height = (kTextAscent + kTextDescent) * kNavigationSize,
                                 .measure = m,
                                 .glyph = {},
                                 .text = std::move(content)});
            right -= width + kNavigationTextGap;
        };
        if (header.jump != core::JumpTo::None) {
            text(jumpText(header));
        }
        if (header.toDoubleCoda) {
            text("To Double Coda");
        }
        if (header.toCoda) {
            text("To Coda");
        }
        if (header.fine) {
            text("Fine");
        }
    }
    if (marks.empty()) {
        return;
    }

    // Lines from the bottom: the signs first, then the texts, each left to
    // right on the lowest line where it overlaps no mark already there.
    std::ranges::stable_sort(
        marks, {}, [](const Mark& mark) { return std::pair(!mark.text.empty(), mark.left); });
    struct Line {
        std::vector<std::pair<double, double>> taken{}; // left and right of its marks
        double height = 0.0;                            // of its tallest mark
    };
    std::vector<Line> lines;
    std::vector<std::size_t> lineOf(marks.size());
    for (std::size_t k = 0; k < marks.size(); ++k) {
        const Mark& mark = marks[k];
        const auto fits = [&mark](const Line& line) {
            return std::ranges::none_of(line.taken, [&mark](const auto& span) {
                return mark.left < span.second + kNavigationGap &&
                       span.first < mark.right + kNavigationGap;
            });
        };
        const auto line =
            static_cast<std::size_t>(std::ranges::find_if(lines, fits) - lines.begin());
        if (line == lines.size()) {
            lines.emplace_back();
        }
        lines[line].taken.emplace_back(mark.left, mark.right);
        lines[line].height = std::max(lines[line].height, mark.height);
        lineOf[k] = line;
    }
    std::vector<double> baselines(lines.size());
    baselines[0] = std::min(kNavigationLowest,
                            system.top - kNavigationInkGap - kTextDescent * kNavigationSize);
    for (std::size_t line = 1; line < baselines.size(); ++line) {
        baselines[line] = baselines[line - 1] - lines[line - 1].height - kNavigationLineGap;
    }
    double highest = baselines.back() - lines.back().height;
    for (std::size_t k = 0; k < marks.size(); ++k) {
        Mark& mark = marks[k];
        const double baseline = baselines[lineOf[k]];
        highest = std::min(highest, baseline - mark.height);
        if (mark.text.empty()) {
            const BoundingBox& box = font.metrics(mark.glyph).box;
            system.glyphs.push_back(
                GlyphItem{.glyph = mark.glyph,
                          .origin = {mark.left - box.southWest.x * kNavigationScale,
                                     baseline + box.southWest.y * kNavigationScale},
                          .role = Role::Navigation,
                          .measure = mark.measure,
                          .scale = kNavigationScale});
        } else {
            TextItem item;
            item.text = std::move(mark.text);
            item.origin = {mark.right, baseline};
            item.size = kNavigationSize;
            item.align = TextAlign::Right;
            item.italic = true;
            item.role = Role::Navigation;
            item.measure = mark.measure;
            system.texts.push_back(std::move(item));
        }
    }
    system.top = std::min(system.top, highest);
}

// The sections of `system` (ADR-0062), above everything else over its first
// staff: each name in a box from the start of its measure, on the lowest line
// where the box clears the boxes already there.
void markSections(DisplayList& system, const core::Song& song, std::span<const MeasureBox> measures,
                  std::span<const double> barlineEnds, const EngravingFont& font) {
    const double height = (kTextAscent + kTextDescent) * kSectionSize + 2 * kSectionPad;
    const double thickness = font.defaults().thinBarlineThickness;
    const double lowest = std::min(kSectionLowest, system.top - kSectionInkGap - thickness / 2);
    std::vector<double> lineEnds; // the right of the last box on each line
    double highest = 0.0;
    for (std::size_t i = 0; i < measures.size(); ++i) {
        const std::size_t m = measures[i].index;
        const std::string& name = song.measureHeader(m).section;
        if (name.empty()) {
            continue;
        }
        const double left = (i > 0 ? barlineEnds[i - 1] : measures[i].x) + kNavigationInset;
        const double right = left + textWidth(name, kSectionSize) + 2 * kSectionPad;
        const auto line = static_cast<std::size_t>(
            std::ranges::find_if(lineEnds,
                                 [left](double end) { return end + kSectionGap <= left; }) -
            lineEnds.begin());
        if (line == lineEnds.size()) {
            lineEnds.push_back(right);
        } else {
            lineEnds[line] = right;
        }
        const double bottom = lowest - static_cast<double>(line) * (height + kSectionLineGap);
        const double top = bottom - height;
        highest = std::min(highest, top - thickness / 2);

        TextItem item;
        item.text = name;
        item.origin = {left + kSectionPad, bottom - kSectionPad - kTextDescent * kSectionSize};
        item.size = kSectionSize;
        item.align = TextAlign::Left;
        item.role = Role::Section;
        item.measure = m;
        system.texts.push_back(std::move(item));
        const auto side = [&](Point from, Point to) {
            system.lines.push_back(LineItem{.from = from,
                                            .to = to,
                                            .thickness = thickness,
                                            .role = Role::Section,
                                            .measure = m});
        };
        const double half = thickness / 2;
        side({left - half, top}, {right + half, top});
        side({left - half, bottom}, {right + half, bottom});
        side({left, top}, {left, bottom});
        side({right, top}, {right, bottom});
    }
    system.top = std::min(system.top, highest);
}

// The metronome note of a beat unit.
Glyph tempoNote(core::NoteValue value) {
    switch (value) {
    case core::NoteValue::DoubleWhole:
    case core::NoteValue::Whole:
        return Glyph::MetNoteWhole;
    case core::NoteValue::Half:
        return Glyph::MetNoteHalfUp;
    case core::NoteValue::Quarter:
        return Glyph::MetNoteQuarterUp;
    case core::NoteValue::Eighth:
        return Glyph::MetNote8thUp;
    case core::NoteValue::Sixteenth:
        return Glyph::MetNote16thUp;
    default:
        return Glyph::MetNote32ndUp;
    }
}

// "120", "92.5": beats per minute to a tenth.
std::string bpmText(double bpm) {
    const double tenths = std::round(bpm * 10.0);
    if (std::fmod(tenths, 10.0) == 0.0) {
        return std::format("{}", static_cast<long long>(tenths / 10.0));
    }
    return std::format("{:.1f}", tenths / 10.0);
}

// Draws "quarter = 120" from `x` on `baseline` for measure m, raising
// `highest` to its top; returns where its text ends.
double drawTempo(DisplayList& system, const EngravingFont& font, const core::Tempo& tempo,
                 std::size_t m, double x, double baseline, double& highest) {
    const Glyph note = tempoNote(tempo.beatUnit().value());
    const BoundingBox& box = font.metrics(note).box;
    const double y = baseline - kTempoNoteLift;
    const auto place = [&](Glyph which, double at) {
        system.glyphs.push_back(GlyphItem{.glyph = which,
                                          .origin = {at, y},
                                          .role = Role::Tempo,
                                          .measure = m,
                                          .scale = kTempoNoteScale});
    };
    place(note, x - box.southWest.x * kTempoNoteScale);
    highest = std::min(highest, y - box.northEast.y * kTempoNoteScale);
    double end = x + box.width() * kTempoNoteScale;
    for (int d = 0; d < tempo.beatUnit().dots(); ++d) {
        const BoundingBox& dot = font.metrics(Glyph::MetAugmentationDot).box;
        end += kTempoDotGap;
        place(Glyph::MetAugmentationDot, end - dot.southWest.x * kTempoNoteScale);
        end += dot.width() * kTempoNoteScale;
    }
    TextItem text;
    text.text = "= " + bpmText(tempo.bpm());
    text.origin = {end + kTempoTextGap, baseline};
    text.size = kTempoSize;
    text.align = TextAlign::Left;
    text.role = Role::Tempo;
    text.measure = m;
    end = text.origin.x + textWidth(text.text, kTempoSize);
    system.texts.push_back(std::move(text));
    return end;
}

// The tempo change after the `index`th one of measure m, and its measure.
struct NextChange {
    std::size_t measure = 0;
    const core::TempoChange* change = nullptr;
};

std::optional<NextChange> changeAfter(const core::Song& song, std::size_t m, std::size_t index) {
    for (std::size_t n = m; n < song.measureCount(); ++n) {
        const auto& changes = song.measureHeader(n).tempoChanges;
        const std::size_t first = n == m ? index + 1 : 0;
        if (first < changes.size()) {
            return NextChange{.measure = n, .change = &changes[first]};
        }
    }
    return std::nullopt;
}

// "accel." or "rit." for a gradual change from `from` to `to`; empty when the
// speed stays (ADR-0082).
std::string_view rampWord(const core::Tempo& from, const core::Tempo& to) {
    const double before = from.wholesPerSecond();
    const double after = to.wholesPerSecond();
    return after > before ? "accel." : after < before ? "rit." : "";
}

// Tempo marks ("quarter = 120") above everything over the first staff of
// `system`: the tempo the song starts with, and every change to another tempo
// over the column where it happens (ADR-0042). A gradual change adds "accel."
// or "rit." and a dashed line to the next change, or to the end of the system,
// where the next system goes on with the line (ADR-0082).
void markTempo(DisplayList& system, const core::Song& song, std::span<const MeasureBox> measures,
               const EngravingFont& font) {
    struct Mark {
        std::size_t measure = 0;
        double x = 0.0;
        std::optional<core::Tempo> tempo{}; // none: only a gradual change goes on
        std::string_view word{};            // "accel.", "rit." or none
        double rampEnd = 0.0;               // where its dashed line ends, if beyond x
    };
    std::vector<Mark> marks;
    if (measures.empty()) {
        return;
    }
    const auto boxX = [](const MeasureBox& box, const core::Fraction& offset,
                         const core::Fraction& length) {
        const double at = (offset / length).toDouble();
        const auto onset = std::ranges::find_if(
            box.onsets, [at](const MeasureOnset& o) { return std::abs(o.at - at) < kEpsilon; });
        return onset != box.onsets.end() ? onset->x : box.x + at * box.width;
    };
    const double systemEnd = measures.back().x + measures.back().width;
    // Where a gradual change's line ends: at the next change in this system,
    // or at its end.
    const auto rampEnd = [&](const NextChange& next) {
        const auto box = std::ranges::find(measures, next.measure, &MeasureBox::index);
        if (box == measures.end()) {
            return systemEnd;
        }
        return boxX(*box, next.change->offset,
                    song.measureHeader(next.measure).timeSignature.measureLength());
    };
    // A gradual change from before the system that goes on in it.
    for (std::size_t m = measures.front().index; m-- > 0;) {
        const auto& changes = song.measureHeader(m).tempoChanges;
        if (changes.empty()) {
            continue;
        }
        const core::TempoChange& last = changes.back();
        const auto next = changeAfter(song, m, changes.size() - 1);
        const bool goesOn = next && (next->measure > measures.front().index ||
                                     next->change->offset > core::Fraction());
        if (last.gradual && goesOn && !rampWord(last.tempo, next->change->tempo).empty()) {
            const MeasureBox& first = measures.front();
            marks.push_back(Mark{.measure = first.index,
                                 .x = boxX(first, {}, core::Fraction(1)),
                                 .rampEnd = rampEnd(*next)});
        }
        break;
    }
    // The tempo in force before each measure, carried along the system's
    // consecutive measures: looking it up from the start of the song for each
    // made long lines quadratic.
    core::Tempo current = song.startTempo();
    std::size_t next = 0; // the measure `current` is in force before
    for (const MeasureBox& box : measures) {
        const std::size_t m = box.index;
        const core::MeasureHeader& header = song.measureHeader(m);
        const core::Fraction length = header.timeSignature.measureLength();
        const auto xAt = [&box, &length](const core::Fraction& offset) {
            const double at = (offset / length).toDouble();
            const auto onset = std::ranges::find_if(
                box.onsets, [at](const MeasureOnset& o) { return std::abs(o.at - at) < kEpsilon; });
            return onset != box.onsets.end() ? onset->x : box.x + at * box.width;
        };
        if (m != next) {
            current =
                m == 0
                    ? song.startTempo()
                    : song.tempoAt(m - 1, song.measureHeader(m - 1).timeSignature.measureLength());
        }
        next = m + 1;
        const bool changedAtStart =
            !header.tempoChanges.empty() && header.tempoChanges.front().offset == core::Fraction();
        if (m == 0 && !changedAtStart) {
            marks.push_back(Mark{.measure = m, .x = xAt({}), .tempo = current});
        }
        for (std::size_t i = 0; i < header.tempoChanges.size(); ++i) {
            const core::TempoChange& change = header.tempoChanges[i];
            Mark mark{.measure = m, .x = xAt(change.offset)};
            if (change.tempo != current || (m == 0 && change.offset == core::Fraction())) {
                mark.tempo = change.tempo;
            }
            if (change.gradual) {
                if (const auto after = changeAfter(song, m, i)) {
                    mark.word = rampWord(change.tempo, after->change->tempo);
                    mark.rampEnd = mark.word.empty() ? 0.0 : rampEnd(*after);
                }
            }
            if (mark.tempo || !mark.word.empty()) {
                marks.push_back(mark);
            }
            current = change.tempo;
        }
    }
    if (marks.empty()) {
        return;
    }
    // One baseline for the system, above the ink of everything over the staff.
    const double baseline =
        std::min(kTempoLowest, system.top - kTempoInkGap - kTextDescent * kTempoSize);
    double highest = baseline - kTextAscent * kTempoSize;
    for (const Mark& mark : marks) {
        double after = mark.x; // where what follows the mark goes
        if (mark.tempo) {
            after = drawTempo(system, font, *mark.tempo, mark.measure, mark.x, baseline, highest);
        }
        if (!mark.word.empty()) {
            if (mark.tempo) {
                after += kRampWordGap;
            }
            system.texts.push_back(TextItem{.text = std::string(mark.word),
                                            .origin = {after, baseline},
                                            .size = kTempoSize,
                                            .align = TextAlign::Left,
                                            .italic = true,
                                            .role = Role::Tempo,
                                            .measure = mark.measure});
            after += textWidth(mark.word, kTempoSize);
        }
        // A line going on from an earlier system starts at the measure.
        const double from = mark.tempo || !mark.word.empty() ? after + kRampGap : after;
        const double to = mark.rampEnd - kRampGap;
        if (to - from >= kRampMinLine) {
            const double y = baseline - kRampLift;
            system.lines.push_back(LineItem{.from = {from, y},
                                            .to = {to, y},
                                            .thickness = kRampThickness,
                                            .role = Role::Tempo,
                                            .measure = mark.measure,
                                            .dash = kRampDash});
        }
    }
    system.top = std::min(system.top, highest);
}

// Measures [first, last) with one set of writers.
void measureRange(const core::Song& song, std::span<const StaffSelection> staves,
                  const EngravingFont& font, std::size_t first, std::size_t last,
                  std::span<MeasureWidth> widths) {
    const std::vector<std::unique_ptr<StaffWriter>> writers = makeWriters(song, staves, font);
    std::vector<std::vector<ColumnInk>> columns(writers.size());
    std::vector<ColumnInk> all;
    std::vector<ColumnInk> merged;
    for (std::size_t m = first; m < last; ++m) {
        MeasureWidth& width = widths[m - first];
        const core::MeasureHeader& header = song.measureHeader(m);
        double emptyWidth = 0.0;
        double opening = 0.0;
        for (std::size_t s = 0; s < writers.size(); ++s) {
            StaffWriter& writer = *writers[s];
            width.header = std::max(width.header, writer.header(m));
            if (m > 0) {
                width.changes = std::max(width.changes, writer.changes(m, 0.0));
            }
            if (header.repeatStart) {
                opening = std::max(opening, writer.opening(m, 0.0));
            }
            writer.collect(m, columns[s]);
            emptyWidth = std::max(emptyWidth, writer.emptyWidth());
            width.barline =
                std::max(width.barline,
                         writer.barline(m, 0.0, m + 1 == song.measureCount(), header.repeatEnd()));
            writer.clearItems();
        }
        if (header.repeatStart) {
            width.opening = kRepeatGap + opening;
            width.openingInline = m > 0 ? opening - overlap(song, m, font) : width.opening;
        }
        merge(columns, all, merged);
        springsOf(merged, song.measureHeader(m).timeSignature.measureLength(), emptyWidth, width);
    }
}

} // namespace

std::vector<std::unique_ptr<StaffWriter>> makeWriters(const core::Song& song,
                                                      std::span<const StaffSelection> staves,
                                                      const EngravingFont& font) {
    if (staves.empty()) {
        throw std::invalid_argument("layout: no staves");
    }
    const auto shows = [&](const StaffSelection& selection, StaffView view) {
        return std::ranges::any_of(staves, [&](const StaffSelection& other) {
            return other.track == selection.track && other.staff == selection.staff &&
                   other.view == view;
        });
    };
    std::vector<std::unique_ptr<StaffWriter>> writers;
    writers.reserve(staves.size());
    for (const StaffSelection& selection : staves) {
        const core::Staff& staff = song.track(selection.track).staff(selection.staff);
        if (selection.view == StaffView::Standard) {
            writers.push_back(makeStandardWriter(song, selection.track, selection.staff, font,
                                                 !shows(selection, StaffView::Tablature)));
            continue;
        }
        if (!staff.isFretted()) {
            throw std::invalid_argument("layout: tablature of a staff without tuning");
        }
        writers.push_back(makeTabWriter(song, selection.track, selection.staff, font,
                                        !shows(selection, StaffView::Standard)));
    }
    return writers;
}

std::vector<MeasureWidth> measureWidths(const core::Song& song,
                                        std::span<const StaffSelection> staves,
                                        const EngravingFont& font, std::size_t first,
                                        std::size_t last, unsigned threads) {
    std::vector<MeasureWidth> widths(last - first);
    const std::size_t jobs = (widths.size() + kMeasuresPerJob - 1) / kMeasuresPerJob;
    core::forEachIndex(0, jobs, core::threadsFor(threads, widths.size(), kParallelMeasures, jobs),
                       [&](std::size_t job) {
                           const std::size_t from = first + job * kMeasuresPerJob;
                           const std::size_t to = std::min(last, from + kMeasuresPerJob);
                           measureRange(song, staves, font, from, to,
                                        std::span(widths).subspan(from - first, to - from));
                       });
    return widths;
}

double nameIndent(const core::Song& song, std::span<const StaffSelection> staves) {
    const bool several = std::ranges::any_of(staves, [&](const StaffSelection& selection) {
        return selection.track != staves.front().track;
    });
    if (!several) {
        return 0.0;
    }
    double widest = 0.0;
    for (const StaffSelection& selection : staves) {
        widest = std::max(widest, textWidth(song.track(selection.track).name(), kTrackNameSize));
    }
    return widest > 0.0 ? widest + kTrackNameGap : 0.0;
}

double braceRoom(std::span<const StaffSelection> staves) {
    return grandStaves(staves).empty() ? 0.0 : kBraceRoom;
}

DisplayList layoutLine(const core::Song& song, std::span<const StaffSelection> staves,
                       const EngravingFont& font, std::size_t first, std::size_t last,
                       const LineOptions& options) {
    const double stretch = options.stretch;
    const std::vector<std::unique_ptr<StaffWriter>> writers = makeWriters(song, staves, font);
    std::vector<MeasureBox> measures;
    measures.reserve(last - first);
    std::vector<ColumnBox> columnBoxes;
    double x = 0.0;
    for (const auto& writer : writers) {
        x = std::max(x, writer->header(first));
    }
    std::vector<std::vector<ColumnInk>> columns(writers.size());
    std::vector<ColumnInk> all;
    std::vector<ColumnInk> merged;
    std::vector<ColumnPosition> positions;
    std::vector<double> barlineEnds;
    barlineEnds.reserve(last - first);
    MeasureWidth width;
    for (std::size_t m = first; m < last; ++m) {
        const core::MeasureHeader& header = song.measureHeader(m);
        bool changed = false;
        if (m > first) {
            double after = x;
            for (const auto& writer : writers) {
                after = std::max(after, writer->changes(m, x));
            }
            changed = after > x;
            x = after;
        }
        if (header.repeatStart) {
            // After the header or the changes, or in place of the barline before.
            const double from = m == first || changed ? x + kRepeatGap : x - overlap(song, m, font);
            double after = from;
            for (const auto& writer : writers) {
                after = std::max(after, writer->opening(m, from));
            }
            x = after;
        }
        double emptyWidth = 0.0;
        for (std::size_t s = 0; s < writers.size(); ++s) {
            writers[s]->collect(m, columns[s]);
            emptyWidth = std::max(emptyWidth, writers[s]->emptyWidth());
        }
        merge(columns, all, merged);
        springsOf(merged, song.measureHeader(m).timeSignature.measureLength(), emptyWidth, width);
        const double start = x;
        const double barline = space(merged, width, start, m == first ? kFirstNoteGap : kBarlineGap,
                                     stretch, positions);
        for (const auto& writer : writers) {
            writer->draw(positions, start, barline);
        }
        MeasureBox box{.index = m, .x = start, .width = barline - start};
        const core::Fraction length = song.measureHeader(m).timeSignature.measureLength();
        box.onsets.reserve(positions.size());
        for (const ColumnPosition& position : positions) {
            box.onsets.push_back(
                MeasureOnset{.at = (position.start / length).toDouble(), .x = position.x});
            columnBoxes.push_back(
                ColumnBox{.measure = m, .start = position.start, .x = position.x});
        }
        measures.push_back(std::move(box));
        const bool songEnd = m + 1 == song.measureCount();
        double after = barline;
        for (const auto& writer : writers) {
            after = std::max(after, writer->barline(m, barline, songEnd, header.repeatEnd()));
        }
        barlineEnds.push_back(after);
        x = after;
    }

    // Staves one under the other, far enough apart for their ink.
    DisplayList system;
    system.width = x;
    double previousY = 0.0;
    double previousBottom = 0.0;
    double previousHeight = 0.0;
    for (std::size_t s = 0; s < writers.size(); ++s) {
        DisplayList staff = writers[s]->finish(x);
        double y = 0.0;
        if (s == 0) {
            system.top = staff.top;
        } else {
            y = std::max(previousY + previousHeight + kStaffDistance,
                         previousBottom + kStaffInkGap - staff.top);
        }
        staff.staves.push_back(StaffBox{.track = staves[s].track,
                                        .staff = staves[s].staff,
                                        .view = staves[s].view,
                                        .x = 0.0,
                                        .y = 0.0,
                                        .width = x,
                                        .height = writers[s]->height(),
                                        .lines = writers[s]->lineCount()});
        shift(staff, 0.0, y);
        previousY = y;
        previousBottom = staff.bottom;
        previousHeight = writers[s]->height();
        system.bottom = staff.bottom;
        append(system, std::move(staff));
    }
    markRepeats(system, song, measures, barlineEnds, font);
    markNavigation(system, song, measures, barlineEnds, font);
    markTempo(system, song, measures, font);
    markSections(system, song, measures, barlineEnds, font);
    const double height = previousY + previousHeight;
    if (writers.size() > 1) {
        // The line joining the staves at the start of the system.
        const double thin = font.defaults().thinBarlineThickness;
        system.lines.push_back(LineItem{.from = {thin / 2, 0.0},
                                        .to = {thin / 2, height},
                                        .thickness = thin,
                                        .role = Role::Barline,
                                        .measure = first});
    }
    if (options.numbered) {
        TextItem number;
        number.text = std::to_string(first + 1);
        number.origin = {0.0, kMeasureNumberY};
        number.size = kMeasureNumberSize;
        number.align = TextAlign::Left;
        number.italic = true;
        number.role = Role::MeasureNumber;
        number.measure = first;
        system.top = std::min(system.top, number.origin.y - kTextAscent * kMeasureNumberSize);
        system.texts.push_back(std::move(number));
    }
    for (MeasureBox& measure : measures) {
        measure.height = height;
    }
    system.measures = std::move(measures);
    system.columns = std::move(columnBoxes);
    braceStaves(system, staves, font);
    const double names = options.named ? nameIndent(song, staves) : 0.0;
    const double indent = names + braceRoom(staves);
    if (indent > 0.0) {
        shift(system, indent, 0.0);
        system.width += indent;
    }
    if (names > 0.0) {
        nameTracks(system, song, staves, names);
    }
    system.systems.push_back(SystemBox{.firstMeasure = first,
                                       .measureCount = last - first,
                                       .x = 0.0,
                                       .y = 0.0,
                                       .width = system.width,
                                       .height = height,
                                       .top = system.top,
                                       .bottom = system.bottom});
    return system;
}

} // namespace detail

DisplayList layoutSystem(const core::Song& song, std::span<const StaffSelection> staves,
                         const EngravingFont& font, const StaffLayoutOptions& options) {
    if (staves.empty()) {
        throw std::invalid_argument("layoutSystem: no staves");
    }
    if (options.firstMeasure >= song.measureCount()) {
        throw std::out_of_range("layoutSystem: no measure " + std::to_string(options.firstMeasure));
    }
    const std::size_t first = options.firstMeasure;
    const std::size_t last = first + std::min(options.measureCount, song.measureCount() - first);
    return detail::layoutLine(song, staves, font, first, last, {});
}

std::vector<StaffSelection> selectTracks(const core::Song& song,
                                         std::span<const std::size_t> tracks, TrackView view) {
    const bool standard = view != TrackView::Tablature;
    const bool tablature = view != TrackView::Standard;
    std::vector<StaffSelection> selection;
    for (const std::size_t t : tracks) {
        if (t >= song.trackCount()) {
            throw std::out_of_range("selectTracks: no such track");
        }
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            const bool fretted = song.track(t).staff(s).isFretted();
            if (standard || !fretted) {
                selection.push_back({.track = t, .staff = s, .view = StaffView::Standard});
            }
            if (tablature && fretted) {
                selection.push_back({.track = t, .staff = s, .view = StaffView::Tablature});
            }
        }
    }
    return selection;
}

bool hasTablature(const core::Song& song, std::span<const std::size_t> tracks) {
    return std::ranges::any_of(tracks, [&song](std::size_t t) {
        if (t >= song.trackCount()) {
            throw std::out_of_range("hasTablature: no such track");
        }
        const core::Track& track = song.track(t);
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            if (track.staff(s).isFretted()) {
                return true;
            }
        }
        return false;
    });
}

DisplayList layoutStaff(const core::Song& song, std::size_t track, std::size_t staff,
                        const EngravingFont& font, const StaffLayoutOptions& options) {
    const StaffSelection selection{.track = track, .staff = staff, .view = StaffView::Standard};
    return layoutSystem(song, std::span(&selection, 1), font, options);
}

} // namespace elara::notation
