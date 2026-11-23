// SPDX-License-Identifier: AGPL-3.0-or-later
// A system: staves laid out together, their columns spaced as one (ADR-0012),
// at their natural spacing or stretched to a width (ADR-0014).
#include <elara/core/fraction.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/staff_layout.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation {

namespace detail {

namespace {

using core::Fraction;

constexpr double kStaffDistance = 4.0;      // at least between the lines of neighbouring staves
constexpr double kStaffInkGap = 1.0;        // at least between their ink
constexpr double kMeasureNumberSize = 1.2;  // font size of a system's first measure number
constexpr double kMeasureNumberY = -2.0;    // its baseline, above the clef
constexpr double kTrackNameSize = 1.6;      // font size of the track names of the first system
constexpr double kTrackNameGap = 1.0;       // between a track name and its staves
constexpr double kVoltaLowest = -3.0;       // the bracket of an alternate ending at least this high
constexpr double kVoltaHook = 2.0;          // the length of its hooks
constexpr double kVoltaInset = 0.2;         // between its hooks and the barlines
constexpr double kVoltaLabelInset = 0.4;    // between its start hook and its numbers
constexpr double kVoltaSize = 1.4;          // font size of its numbers and of repeat counts
constexpr double kVoltaTextGap = 0.3;       // between the bracket and its numbers
constexpr double kVoltaInkGap = 0.5;        // at least between its numbers and the staff's ink
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
        measures.push_back(MeasureBox{.index = m, .x = start, .width = barline - start});
        for (const ColumnPosition& position : positions) {
            columnBoxes.push_back(
                ColumnBox{.measure = m, .start = position.start, .x = position.x});
        }
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
    const double indent = options.named ? nameIndent(song, staves) : 0.0;
    if (indent > 0.0) {
        shift(system, indent, 0.0);
        nameTracks(system, song, staves, indent);
        system.width += indent;
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
