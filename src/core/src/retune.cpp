// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>
#include <elara/core/retune.hpp>
#include <elara/core/signature.hpp>

#include "fret_placement.hpp"

#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

// Gives every note with a fret position in `content` the pitch its fret plays
// on `tuning` above `capo`, spelled in `key` when it changes.
void soundFrets(Measure& content, const Tuning& tuning, int capo, const KeySignature& key,
                std::size_t measure) {
    for (Voice& voice : content.voices) {
        for (Beat& beat : voice.beats) {
            for (Note& note : beat.notes) {
                if (!note.position) {
                    continue;
                }
                const int string = note.position->string;
                const int fret = note.position->fret;
                if (string < 1 || string > tuning.stringCount()) {
                    throw std::invalid_argument(std::format(
                        "measure {}: the new tuning has no string {}", measure + 1, string));
                }
                if (fret < 0 || capo + fret > Tuning::kMaxFret) {
                    throw std::invalid_argument(std::format(
                        "measure {}: fret {} is past the neck above the capo", measure + 1, fret));
                }
                const int midi = tuning.openString(string).midiNumber() + capo + fret;
                if (midi > 127) {
                    throw std::invalid_argument(
                        std::format("measure {}: fret {} on string {} is above the MIDI range",
                                    measure + 1, fret, string));
                }
                if (midi != note.pitch.midiNumber()) {
                    note.pitch = key.spell(midi);
                }
            }
        }
    }
}

} // namespace

std::unique_ptr<Command> makeRetune(const Song& song, const Retuning& what) {
    if (what.track >= song.trackCount()) {
        throw std::out_of_range("Retune: no such track");
    }
    const Track& track = song.track(what.track);
    if (what.staff >= track.staffCount()) {
        throw std::out_of_range("Retune: no such staff");
    }
    const Staff& staff = track.staff(what.staff);
    if (!staff.tuning()) {
        throw std::invalid_argument("Retune: the staff has no strings");
    }
    if (what.capo < 0 || what.capo > Tuning::kMaxFret) {
        throw std::invalid_argument("Retune: capo outside the neck");
    }
    if (*staff.tuning() == what.tuning && staff.capo() == what.capo) {
        return nullptr;
    }

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(
        std::make_unique<SetStaffTuning>(what.track, what.staff, what.tuning, what.capo));
    detail::FollowedByVoice followed{};
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        Measure content = staff.measure(m);
        if (what.keep == Retune::Frets) {
            soundFrets(content, what.tuning, what.capo, song.measureHeader(m).keySignature, m);
        } else {
            detail::placeOnStrings(content, what.tuning, what.capo, followed, m);
        }
        if (content != staff.measure(m)) {
            commands.push_back(
                std::make_unique<ReplaceMeasure>(what.track, what.staff, m, std::move(content)));
        }
    }
    return std::make_unique<CommandGroup>("Change tuning", std::move(commands));
}

} // namespace elara::core
