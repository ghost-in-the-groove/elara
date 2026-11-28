// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/staff_size.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace elara::notation {

namespace {

constexpr double kStandardHeight = 4.0; // five lines
constexpr double kTabLineSpace = 1.5;   // between string lines, as tablature draws them
constexpr double kUsualDistance = 5.0;  // between the lines of neighbouring staves
constexpr double kInkRoom = 8.0;        // above the first staff and below the last
constexpr double kSystemGap = 2.0;      // between the ink of two systems
constexpr int kSystemsPerPage = 2;

} // namespace

double typicalSystemHeight(const core::Song& song, std::span<const StaffSelection> staves) {
    if (staves.empty()) {
        return 0.0;
    }
    double height = kInkRoom + kUsualDistance * static_cast<double>(staves.size() - 1);
    for (const StaffSelection& selection : staves) {
        const core::Staff& staff = song.track(selection.track).staff(selection.staff);
        if (selection.view == StaffView::Tablature && staff.tuning()) {
            height += kTabLineSpace * static_cast<double>(staff.tuning()->stringCount() - 1);
        } else {
            height += kStandardHeight;
        }
    }
    return height;
}

double fittedStaffSpace(const core::Song& song, std::span<const StaffSelection> staves,
                        const Paper& paper, double staffSpace, double margin) {
    if (!(staffSpace > 0.0)) {
        throw std::invalid_argument("the staff space must be positive");
    }
    const double room = paper.height - 2.0 * margin; // mm between the margins
    if (!(room > 0.0)) {
        throw std::invalid_argument("the margins leave no room on the page");
    }
    const double system = typicalSystemHeight(song, staves);
    if (system <= 0.0) {
        return staffSpace;
    }
    const double perPage = static_cast<double>(kSystemsPerPage);
    double space = room / (perPage * system + (perPage - 1.0) * kSystemGap);
    if (space >= staffSpace) {
        return staffSpace;
    }
    space = std::max(space, kLeastStaffShare * staffSpace);
    if (system * space > room) {
        space = room / system; // one system a page
    }
    space = std::max(space, kLeastStaffSpace);
    return std::min(staffSpace, std::floor(space * 100.0) / 100.0);
}

} // namespace elara::notation
