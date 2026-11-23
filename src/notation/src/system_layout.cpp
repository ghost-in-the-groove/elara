// SPDX-License-Identifier: AGPL-3.0-or-later
// A system: staves laid out together, their columns spaced as one (ADR-0012).
#include <elara/core/fraction.hpp>
#include <elara/core/signature.hpp>
#include <elara/notation/staff_layout.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation {

namespace {

using core::Fraction;
using detail::ColumnInk;
using detail::ColumnPosition;
using detail::StaffWriter;

constexpr double kStaffDistance = 4.0; // at least between the lines of neighbouring staves
constexpr double kStaffInkGap = 1.0;   // at least between their ink

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

// Positions of the columns of a measure starting at `start`: each column gets
// its ideal space unless the ink of its neighbour needs more. Returns where the
// closing barline goes.
double space(const std::vector<ColumnInk>& columns, const Fraction& length, double start,
             double gap, double emptyWidth, std::vector<ColumnPosition>& positions) {
    positions.clear();
    if (columns.empty()) {
        return start + gap + std::max(detail::idealSpace(length), emptyWidth) + detail::kEndGap;
    }
    double x = start + gap + std::max(0.0, -columns.front().left);
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const ColumnInk& column = columns[i];
        positions.push_back(ColumnPosition{.start = column.start, .x = x});
        if (i + 1 < columns.size()) {
            const ColumnInk& next = columns[i + 1];
            x += std::max(detail::idealSpace(next.start - column.start),
                          column.right + detail::kMinNoteGap - next.left);
        }
    }
    const ColumnInk& last = columns.back();
    return x + std::max(detail::idealSpace(std::max(last.longest, length - last.start)),
                        last.right + detail::kEndGap);
}

} // namespace

DisplayList layoutSystem(const core::Song& song, std::span<const StaffSelection> staves,
                         const EngravingFont& font, const StaffLayoutOptions& options) {
    if (staves.empty()) {
        throw std::invalid_argument("layoutSystem: no staves");
    }
    if (options.firstMeasure >= song.measureCount()) {
        throw std::out_of_range("layoutSystem: no measure " + std::to_string(options.firstMeasure));
    }
    const auto shows = [&](const StaffSelection& selection, StaffView view) {
        return std::ranges::any_of(staves, [&](const StaffSelection& other) {
            return other.track == selection.track && other.staff == selection.staff &&
                   other.view == view;
        });
    };
    std::vector<std::unique_ptr<StaffWriter>> writers;
    for (const StaffSelection& selection : staves) {
        const core::Staff& staff = song.track(selection.track).staff(selection.staff);
        if (selection.view == StaffView::Standard) {
            writers.push_back(detail::makeStandardWriter(song, selection.track, selection.staff,
                                                         font,
                                                         !shows(selection, StaffView::Tablature)));
            continue;
        }
        if (!staff.isFretted()) {
            throw std::invalid_argument("layoutSystem: tablature of a staff without tuning");
        }
        writers.push_back(detail::makeTabWriter(song, selection.track, selection.staff, font,
                                                !shows(selection, StaffView::Standard)));
    }

    const std::size_t first = options.firstMeasure;
    const std::size_t last = first + std::min(options.measureCount, song.measureCount() - first);
    std::vector<MeasureBox> measures;
    double x = 0.0;
    for (const auto& writer : writers) {
        x = std::max(x, writer->header(first));
    }
    std::vector<std::vector<ColumnInk>> columns(writers.size());
    std::vector<ColumnInk> all;
    std::vector<ColumnInk> merged;
    std::vector<ColumnPosition> positions;
    for (std::size_t m = first; m < last; ++m) {
        if (m > first) {
            double after = x;
            for (const auto& writer : writers) {
                after = std::max(after, writer->changes(m, x));
            }
            x = after;
        }
        double emptyWidth = 0.0;
        for (std::size_t s = 0; s < writers.size(); ++s) {
            writers[s]->collect(m, columns[s]);
            emptyWidth = std::max(emptyWidth, writers[s]->emptyWidth());
        }
        merge(columns, all, merged);
        const double start = x;
        const double barline =
            space(merged, song.measureHeader(m).timeSignature.measureLength(), start,
                  m == first ? detail::kFirstNoteGap : detail::kBarlineGap, emptyWidth, positions);
        for (const auto& writer : writers) {
            writer->draw(positions, start, barline);
        }
        measures.push_back(MeasureBox{.index = m, .x = start, .width = barline - start});
        const bool songEnd = m + 1 == song.measureCount();
        double after = barline;
        for (const auto& writer : writers) {
            after = std::max(after, writer->barline(m, barline, songEnd));
        }
        x = after;
    }

    // Staves one under the other, far enough apart for their ink.
    DisplayList system;
    system.width = x;
    system.measures = std::move(measures);
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
                                        .y = 0.0,
                                        .height = writers[s]->height(),
                                        .lines = writers[s]->lineCount()});
        detail::shift(staff, y);
        previousY = y;
        previousBottom = staff.bottom;
        previousHeight = writers[s]->height();
        system.bottom = staff.bottom;
        detail::append(system, std::move(staff));
    }
    return system;
}

DisplayList layoutStaff(const core::Song& song, std::size_t track, std::size_t staff,
                        const EngravingFont& font, const StaffLayoutOptions& options) {
    const StaffSelection selection{.track = track, .staff = staff, .view = StaffView::Standard};
    return layoutSystem(song, std::span(&selection, 1), font, options);
}

} // namespace elara::notation
