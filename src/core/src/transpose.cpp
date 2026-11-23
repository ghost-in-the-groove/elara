// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/transpose.hpp>
#include <elara/core/tuning.hpp>

#include "fret_placement.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace elara::core {

namespace {

constexpr int floorDiv(int a, int b) noexcept { // b > 0
    return (a >= 0 ? a : a - b + 1) / b;
}

constexpr int floorMod(int a, int b) noexcept {
    return a - (b * floorDiv(a, b));
}

constexpr std::array<int, 7> kSemitones{0, 2, 4, 5, 7, 9, 11}; // C D E F G A B from C
constexpr std::array<int, 7> kFifths{0, 2, 4, -1, 1, 3, 5};    // their fifths from C

int semitoneOf(Step step) noexcept {
    return kSemitones[static_cast<std::size_t>(step)];
}

// Transposes one measure of a staff.
void transposeMeasure(Measure& content, const Staff& staff, Interval interval,
                      detail::FollowedByVoice& followed, std::size_t measure) {
    for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
        for (Beat& beat : content.voices[v].beats) {
            for (Note& note : beat.notes) {
                const std::optional<Pitch> moved = transposed(note.pitch, interval);
                if (!moved) {
                    throw std::invalid_argument(
                        std::format("measure {}: {} would leave the MIDI range", measure + 1,
                                    note.pitch.toString()));
                }
                note.pitch = *moved;
            }
        }
    }
    if (staff.tuning()) {
        detail::placeOnStrings(content, *staff.tuning(), staff.capo(), followed, measure);
    }
}

} // namespace

Interval Interval::fromSemitones(int semitones, const KeySignature& key) {
    const int octaves = floorDiv(semitones, 12);
    const int rest = semitones - (12 * octaves);
    if (rest == 0) {
        return {.steps = 7 * octaves, .semitones = semitones};
    }
    // A semitone is seven fifths away; of the two keys a tritone from C, the
    // flat one (fifths -6 to 5).
    const KeySignature target(floorMod(key.fifths() + (7 * rest) + 6, 12) - 6, key.mode());
    const int steps = floorMod(static_cast<int>(target.tonic()) - static_cast<int>(key.tonic()), 7);
    return {.steps = steps + (7 * octaves), .semitones = semitones};
}

std::optional<Pitch> transposed(const Pitch& pitch, Interval interval) {
    const int midi = pitch.midiNumber() + interval.semitones;
    if (midi < 0 || midi > 127) {
        return std::nullopt;
    }
    const int index = static_cast<int>(pitch.step()) + interval.steps;
    const auto step = static_cast<Step>(floorMod(index, 7));
    const int octave = pitch.octave() + floorDiv(index, 7);
    const int alter = midi - (((octave + 1) * 12) + semitoneOf(step));
    if (alter < Pitch::kMinAlter || alter > Pitch::kMaxAlter) {
        return Pitch::fromMidi(midi,
                               alter < 0 ? SpellingPreference::Flats : SpellingPreference::Sharps);
    }
    return Pitch(step, alter, octave);
}

KeySignature transposed(const KeySignature& key, Interval interval) {
    const int index = static_cast<int>(key.tonic()) + interval.steps;
    const auto step = static_cast<Step>(floorMod(index, 7));
    const int alter = key.tonicAlter() + interval.semitones -
                      (semitoneOf(step) + (12 * floorDiv(index, 7)) - semitoneOf(key.tonic()));
    int fifths = kFifths[static_cast<std::size_t>(step)] + (7 * alter) -
                 (key.mode() == KeyMode::Minor ? 3 : 0);
    while (fifths > KeySignature::kMaxFifths) {
        fifths -= 12;
    }
    while (fifths < -KeySignature::kMaxFifths) {
        fifths += 12;
    }
    return {fifths, key.mode()};
}

std::unique_ptr<Command> makeTranspose(const Song& song, const Transposition& what) {
    if (what.first >= song.measureCount()) {
        throw std::out_of_range("Transpose: no such measure");
    }
    const std::size_t last = what.first + std::min(what.count, song.measureCount() - what.first);
    std::vector<std::size_t> tracks = what.tracks;
    if (tracks.empty()) {
        tracks.resize(song.trackCount());
        std::iota(tracks.begin(), tracks.end(), std::size_t{0});
    }
    std::ranges::sort(tracks);
    tracks.erase(std::ranges::unique(tracks).begin(), tracks.end());
    if (!tracks.empty() && tracks.back() >= song.trackCount()) {
        throw std::out_of_range("Transpose: no such track");
    }
    const Interval interval =
        Interval::fromSemitones(what.semitones, song.measureHeader(what.first).keySignature);

    std::vector<std::unique_ptr<Command>> commands;
    bool everyPitched = true;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const Track& track = song.track(t);
        if (track.instrument().kind == InstrumentKind::Percussion) {
            continue; // drum keys are drums, not pitches
        }
        if (!std::ranges::binary_search(tracks, t)) {
            everyPitched = false;
            continue;
        }
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            const Staff& staff = track.staff(s);
            detail::FollowedByVoice followed{};
            for (std::size_t m = what.first; m < last; ++m) {
                Measure content = staff.measure(m);
                transposeMeasure(content, staff, interval, followed, m);
                if (content != staff.measure(m)) {
                    commands.push_back(
                        std::make_unique<ReplaceMeasure>(t, s, m, std::move(content)));
                }
            }
        }
    }
    // Key signatures belong to every track: they move when every pitched one does.
    if (everyPitched) {
        for (std::size_t m = what.first; m < last; ++m) {
            MeasureHeader header = song.measureHeader(m);
            const KeySignature key = transposed(header.keySignature, interval);
            if (key != header.keySignature) {
                header.keySignature = key;
                commands.push_back(std::make_unique<SetMeasureHeader>(m, std::move(header)));
            }
        }
    }
    return std::make_unique<CommandGroup>("Transpose", std::move(commands));
}

} // namespace elara::core
