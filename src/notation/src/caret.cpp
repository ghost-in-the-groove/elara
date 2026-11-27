// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/instrument.hpp>
#include <elara/notation/caret.hpp>

#include "drum_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace elara::notation {

namespace {

constexpr double kCaretWidth = 2.4;    // room for a two-digit fret number
constexpr double kStaffOverhang = 1.0; // the caret above and below a standard staff
constexpr double kFirstSlot = 1.0;     // a slot with no column before it, from the measure's start
constexpr double kSlotGap = 2.5;       // the free slot after the last column, at least
constexpr double kReach = 3.0;         // how far above or below its system a click picks a measure
constexpr double kBarlineReach = 0.5;  // how far past a measure's ends a click still picks it
constexpr double kTolerance = 1e-9;

const MeasureBox* measureBox(const DisplayList& list, std::size_t measure) {
    const auto it = std::ranges::find(list.measures, measure, &MeasureBox::index);
    return it == list.measures.end() ? nullptr : &*it;
}

// Whether a staff belongs to the system of a measure (the measure box spans
// the system from its first staff to its last).
bool inSystem(const StaffBox& staff, const MeasureBox& measure) {
    return staff.y >= measure.y - kTolerance && staff.y <= measure.y + measure.height + kTolerance;
}

// The staff box showing a staff in the system of `measure`: its tablature when
// the system shows it.
const StaffBox* staffBox(const DisplayList& list, const MeasureBox& measure, std::size_t track,
                         std::size_t staff) {
    const StaffBox* found = nullptr;
    for (const StaffBox& box : list.staves) {
        if (box.track != track || box.staff != staff || !inSystem(box, measure)) {
            continue;
        }
        if (found == nullptr || box.view == StaffView::Tablature) {
            found = &box;
        }
    }
    return found;
}

double lineSpace(const StaffBox& staff) {
    return staff.lines > 1 ? staff.height / static_cast<double>(staff.lines - 1) : 1.0;
}

// Drum entry (ADR-0035): on a percussion staff the caret's string is a lane,
// one per kit piece, which sits where the drum map places that piece.
bool drumStaff(const core::Song& song, std::size_t track, std::size_t staff) {
    return song.track(track).staff(staff).clef() == core::Clef::Percussion;
}

int laneCount() {
    return static_cast<int>(core::drumPieces().size());
}

// A lane's place, in staff spaces below the top line.
double laneDepth(int lane) {
    const auto pieces = core::drumPieces();
    const auto index = static_cast<std::size_t>(std::clamp(lane, 1, laneCount()) - 1);
    return detail::drumNote(pieces[index].key).step / 2.0;
}

// The lane nearest to `depth` staff spaces below the top line. Of lanes that
// share a place, `current` when it is one of them, otherwise the first.
int nearestLane(double depth, int current) {
    int nearest = 1;
    double best = std::numeric_limits<double>::max();
    for (int lane = 1; lane <= laneCount(); ++lane) {
        const double away = std::abs(laneDepth(lane) - depth);
        if (away < best - kTolerance) {
            nearest = lane;
            best = away;
        }
    }
    if (current >= 1 && current <= laneCount() &&
        std::abs(laneDepth(current) - depth) <= best + kTolerance) {
        return current;
    }
    return nearest;
}

// From a column's x to the centre of the notehead the beat draws (where its
// fret numbers are centred).
double headCentre(const core::Beat* beat, const EngravingFont& font) {
    Glyph head = Glyph::NoteheadBlack;
    if (beat != nullptr && !beat->isRest()) {
        switch (beat->duration.value()) {
        case core::NoteValue::DoubleWhole:
            head = Glyph::NoteheadDoubleWhole;
            break;
        case core::NoteValue::Whole:
            head = Glyph::NoteheadWhole;
            break;
        case core::NoteValue::Half:
            head = Glyph::NoteheadHalf;
            break;
        default:
            break;
        }
    }
    const BoundingBox& box = font.metrics(head).box;
    return (box.southWest.x + box.northEast.x) / 2;
}

// Where notes starting `start` into a measure go: the column there, or where
// that time falls between the columns around it (the start of the measure and
// its end when there are none). After the last column it keeps at least a
// note's room from it, as far as the measure allows: a short measure is spaced
// for the notes it has, not for the time it misses.
double slotX(const DisplayList& list, const MeasureBox& box, const core::Fraction& start,
             const core::Fraction& length) {
    core::Fraction before;
    double beforeX = box.x + kFirstSlot;
    bool hasBefore = false;
    core::Fraction after = length;
    double afterX = box.x + box.width;
    bool hasAfter = false;
    for (const ColumnBox& column : list.columns) {
        if (column.measure != box.index) {
            continue;
        }
        if (column.start == start) {
            return column.x;
        }
        if (column.start < start) {
            before = column.start;
            beforeX = column.x;
            hasBefore = true;
        } else if (!hasAfter) {
            after = column.start;
            afterX = column.x;
            hasAfter = true;
        }
    }
    if (after <= before) {
        return beforeX;
    }
    const double share = ((start - before) / (after - before)).toDouble();
    const double x = beforeX + (afterX - beforeX) * std::clamp(share, 0.0, 1.0);
    if (hasBefore && !hasAfter) {
        return std::max(x, std::min(beforeX + kSlotGap, afterX - kCaretWidth));
    }
    return x;
}

struct Slot {
    core::Fraction start;
    const core::Beat* beat = nullptr; // null on the free slot
};

Slot slotOf(const core::Voice& voice, std::size_t beat) {
    if (beat < voice.beats.size()) {
        return {.start = voice.startOf(beat), .beat = &voice.beats[beat]};
    }
    return {.start = voice.length()};
}

double centreX(const DisplayList& list, const MeasureBox& box, const Slot& slot,
               const core::Fraction& length, const EngravingFont& font) {
    return slotX(list, box, slot.start, length) + headCentre(slot.beat, font);
}

bool exists(const core::Song& song, std::size_t track, std::size_t staff, std::size_t measure,
            std::size_t voice) {
    return track < song.trackCount() && staff < song.track(track).staffCount() &&
           measure < song.measureCount() && voice < core::Measure::kMaxVoices;
}

double distance(double value, double from, double to) {
    return value < from ? from - value : (value > to ? value - to : 0.0);
}

} // namespace

std::optional<Rect> caretBox(const DisplayList& list, const core::Song& song,
                             const core::Caret& caret, const EngravingFont& font) {
    if (!exists(song, caret.track, caret.staff, caret.measure, caret.voice)) {
        return std::nullopt;
    }
    const MeasureBox* box = measureBox(list, caret.measure);
    if (box == nullptr) {
        return std::nullopt;
    }
    const StaffBox* staff = staffBox(list, *box, caret.track, caret.staff);
    if (staff == nullptr) {
        return std::nullopt;
    }
    const core::Voice& voice =
        song.track(caret.track).staff(caret.staff).measure(caret.measure).voices[caret.voice];
    const core::Fraction length = song.measureHeader(caret.measure).timeSignature.measureLength();
    const double centre = centreX(list, *box, slotOf(voice, caret.beat), length, font);
    if (staff->view == StaffView::Tablature) {
        const double space = lineSpace(*staff);
        const int string = std::clamp(caret.string, 1, static_cast<int>(staff->lines));
        const double y = staff->y + space * (string - 1);
        return Rect{.x = centre - kCaretWidth / 2,
                    .y = y - space / 2,
                    .width = kCaretWidth,
                    .height = space};
    }
    if (drumStaff(song, caret.track, caret.staff)) {
        const double space = lineSpace(*staff);
        const double y = staff->y + space * laneDepth(caret.string);
        return Rect{.x = centre - kCaretWidth / 2,
                    .y = y - space / 2,
                    .width = kCaretWidth,
                    .height = space};
    }
    return Rect{.x = centre - kCaretWidth / 2,
                .y = staff->y - kStaffOverhang,
                .width = kCaretWidth,
                .height = staff->height + 2 * kStaffOverhang};
}

std::optional<Rect> caretColumn(const DisplayList& list, const core::Song& song,
                                const core::Caret& caret, const EngravingFont& font) {
    const std::optional<Rect> box = caretBox(list, song, caret, font);
    const MeasureBox* measure = measureBox(list, caret.measure);
    if (!box || measure == nullptr) {
        return std::nullopt;
    }
    double top = box->y;
    double bottom = box->y + box->height;
    for (const StaffBox& staff : list.staves) {
        if (staff.track == caret.track && staff.staff == caret.staff && inSystem(staff, *measure)) {
            top = std::min(top, staff.y - kStaffOverhang);
            bottom = std::max(bottom, staff.y + staff.height + kStaffOverhang);
        }
    }
    return Rect{.x = box->x, .y = top, .width = box->width, .height = bottom - top};
}

std::optional<core::Caret> caretAt(const DisplayList& list, const core::Song& song, Point point,
                                   const core::Caret& current, const EngravingFont& font) {
    // The measure under the point: nearest across, then up and down.
    const MeasureBox* box = nullptr;
    double bestAcross = std::numeric_limits<double>::max();
    double bestDown = std::numeric_limits<double>::max();
    for (const MeasureBox& measure : list.measures) {
        const double across = distance(point.x, measure.x, measure.x + measure.width);
        const double down = distance(point.y, measure.y, measure.y + measure.height);
        if (across > kBarlineReach || down > kReach) {
            continue;
        }
        if (across < bestAcross || (across == bestAcross && down < bestDown)) {
            box = &measure;
            bestAcross = across;
            bestDown = down;
        }
    }
    if (box == nullptr) {
        return std::nullopt;
    }
    const StaffBox* staff = nullptr;
    double nearest = std::numeric_limits<double>::max();
    for (const StaffBox& candidate : list.staves) {
        if (!inSystem(candidate, *box)) {
            continue;
        }
        const double away = distance(point.y, candidate.y, candidate.y + candidate.height);
        if (away < nearest) {
            staff = &candidate;
            nearest = away;
        }
    }
    if (staff == nullptr || !exists(song, staff->track, staff->staff, box->index, current.voice)) {
        return std::nullopt;
    }

    core::Caret caret = current;
    caret.track = staff->track;
    caret.staff = staff->staff;
    caret.measure = box->index;
    if (staff->view == StaffView::Tablature) {
        const double lines = static_cast<double>(staff->lines);
        const double string = std::round((point.y - staff->y) / lineSpace(*staff)) + 1.0;
        caret.string = static_cast<int>(std::clamp(string, 1.0, lines));
    } else if (drumStaff(song, caret.track, caret.staff)) {
        const bool sameStaff = current.track == caret.track && current.staff == caret.staff;
        caret.string =
            nearestLane((point.y - staff->y) / lineSpace(*staff), sameStaff ? current.string : 0);
    }
    const core::Voice& voice =
        song.track(caret.track).staff(caret.staff).measure(caret.measure).voices[caret.voice];
    const core::Fraction length = song.measureHeader(caret.measure).timeSignature.measureLength();
    const std::size_t slots = voice.beats.size() + (voice.length() < length ? 1 : 0);
    double closest = std::numeric_limits<double>::max();
    for (std::size_t beat = 0; beat < slots; ++beat) {
        const double away =
            std::abs(centreX(list, *box, slotOf(voice, beat), length, font) - point.x);
        if (away < closest) {
            closest = away;
            caret.beat = beat;
        }
    }
    return core::clampCaret(song, caret);
}

} // namespace elara::notation
