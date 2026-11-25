// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/clipboard.hpp>

#include "fret_placement.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace elara::core {

MeasureClip copyMeasures(const Song& song, std::size_t first, std::size_t count, std::size_t track,
                         std::size_t trackCount) {
    if (count == 0 || first >= song.measureCount() || count > song.measureCount() - first) {
        throw std::out_of_range("Copy: no such measures");
    }
    if (track >= song.trackCount()) {
        throw std::out_of_range("Copy: no such track");
    }
    const std::size_t tracks = trackCount == 0 ? song.trackCount() - track : trackCount;
    if (tracks > song.trackCount() - track) {
        throw std::out_of_range("Copy: no such track");
    }
    MeasureClip clip;
    for (std::size_t m = first; m < first + count; ++m) {
        clip.headers.push_back(song.measureHeader(m));
    }
    for (std::size_t t = track; t < track + tracks; ++t) {
        const Track& from = song.track(t);
        ClipTrack part{.kind = from.instrument().kind};
        for (std::size_t s = 0; s < from.staffCount(); ++s) {
            const Staff& staff = from.staff(s);
            ClipStaff copied{.tuning = staff.tuning(), .capo = staff.capo()};
            for (std::size_t m = first; m < first + count; ++m) {
                copied.measures.push_back(staff.measure(m));
            }
            part.staves.push_back(std::move(copied));
        }
        clip.tracks.push_back(std::move(part));
    }
    return clip;
}

std::unique_ptr<Command> makePaste(const Song& song, const MeasureClip& clip, std::size_t at,
                                   std::size_t track, PasteMode mode) {
    if (clip.isEmpty()) {
        throw std::out_of_range("Paste: nothing to paste");
    }
    if (at > song.measureCount()) {
        throw std::out_of_range("Paste: past the end of the song");
    }
    if (track >= song.trackCount() || clip.tracks.size() > song.trackCount() - track) {
        throw std::out_of_range("Paste: not a track for every copied track");
    }
    const std::size_t count = clip.measureCount();
    std::vector<std::unique_ptr<Command>> commands;
    if (mode == PasteMode::Insert) {
        commands.push_back(std::make_unique<InsertMeasures>(at, count));
        for (std::size_t i = 0; i < count; ++i) {
            commands.push_back(std::make_unique<SetMeasureHeader>(at + i, clip.headers[i]));
        }
    } else if (at + count > song.measureCount()) {
        commands.push_back(std::make_unique<InsertMeasures>(song.measureCount(),
                                                            at + count - song.measureCount()));
    }

    for (std::size_t i = 0; i < clip.tracks.size(); ++i) {
        const ClipTrack& part = clip.tracks[i];
        const Track& target = song.track(track + i);
        const bool drums = target.instrument().kind == InstrumentKind::Percussion;
        if (drums != (part.kind == InstrumentKind::Percussion)) {
            throw std::invalid_argument("Paste: drums go to drum tracks only");
        }
        for (std::size_t s = 0; s < std::min(part.staves.size(), target.staffCount()); ++s) {
            const ClipStaff& from = part.staves[s];
            const Staff& to = target.staff(s);
            // Frets mean nothing without a tuning, and something else on
            // another tuning or capo: there the notes find strings again.
            const bool place =
                to.tuning() && (to.tuning() != from.tuning || to.capo() != from.capo);
            detail::FollowedByVoice followed{};
            for (std::size_t m = 0; m < count; ++m) {
                Measure content = from.measures[m];
                if (!to.tuning()) {
                    for (Voice& voice : content.voices) {
                        for (Beat& beat : voice.beats) {
                            for (Note& note : beat.notes) {
                                note.position.reset();
                            }
                            for (Grace& grace : beat.graces) {
                                for (Note& note : grace.notes) {
                                    note.position.reset();
                                }
                            }
                        }
                    }
                } else if (place) {
                    detail::placeOnStrings(content, *to.tuning(), to.capo(), followed, at + m);
                }
                commands.push_back(
                    std::make_unique<ReplaceMeasure>(track + i, s, at + m, std::move(content)));
            }
        }
    }
    return std::make_unique<CommandGroup>("Paste", std::move(commands));
}

} // namespace elara::core
