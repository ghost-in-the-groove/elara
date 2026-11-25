// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/chord_name.hpp>
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

/// A chord pitch moved by `interval`, as the interval spells it.
Pitch moved(ChordPitch pitch, Interval interval) {
    // Octave 4 keeps every spelling inside the MIDI range.
    const Pitch from(pitch.step, pitch.alter, 4);
    return transposed(from, interval).value_or(from);
}

/// A chord pitch moved by `interval`, with one sharp or flat at most.
ChordPitch transposed(ChordPitch pitch, Interval interval) {
    Pitch spelled = moved(pitch, interval);
    if (spelled.alter() < -1 || spelled.alter() > 1) {
        spelled =
            Pitch::fromMidi(spelled.midiNumber(), spelled.alter() < 0 ? SpellingPreference::Flats
                                                                      : SpellingPreference::Sharps);
    }
    return {.step = spelled.step(), .alter = spelled.alter()};
}

/// The chord symbols already moved, so that beats which shared one still do.
using MovedChords =
    std::vector<std::pair<SharedOptional<ChordSymbol>, SharedOptional<ChordSymbol>>>;

void transposeChord(Beat& beat, Interval interval, int maxFret, MovedChords& moved) {
    if (!beat.chord) {
        return;
    }
    if (const auto done = std::ranges::find(moved, beat.chord, &MovedChords::value_type::first);
        done != moved.end()) {
        beat.chord = done->second;
        return;
    }
    ChordSymbol symbol = *beat.chord;
    symbol.name = transposedChordName(symbol.name, interval);
    if (symbol.diagram) {
        symbol.diagram = transposedDiagram(*symbol.diagram, interval.semitones, maxFret);
    }
    SharedOptional<ChordSymbol> result(std::move(symbol));
    moved.emplace_back(beat.chord, result);
    beat.chord = std::move(result);
}

// Transposes one measure of a staff.
void transposeMeasure(Measure& content, const Staff& staff, Interval interval,
                      detail::FollowedByVoice& followed, std::size_t measure, MovedChords& chords) {
    const int maxFret = Tuning::kMaxFret - staff.capo();
    for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
        for (Beat& beat : content.voices[v].beats) {
            transposeChord(beat, interval, maxFret, chords);
            const auto move = [&](std::vector<Note>& notes) {
                for (Note& note : notes) {
                    const std::optional<Pitch> moved = transposed(note.pitch, interval);
                    if (!moved) {
                        throw std::invalid_argument(
                            std::format("measure {}: {} would leave the MIDI range", measure + 1,
                                        note.pitch.toString()));
                    }
                    note.pitch = *moved;
                }
            };
            for (Grace& grace : beat.graces) {
                move(grace.notes);
            }
            move(beat.notes);
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

std::string transposedChordName(std::string_view name, Interval interval) {
    const std::optional<ChordNameParts> parts = splitChordName(name);
    if (!parts) {
        return std::string(name);
    }
    // A root that would need a double sharp or flat takes the step next to
    // it, and the bass moves by that same interval: Gb/Bb up a diminished
    // fifth is C/E, an augmented fourth, rather than C/Fb.
    Interval spelling = interval;
    if (const int alter = moved(parts->root, interval).alter(); alter < -1) {
        --spelling.steps;
    } else if (alter > 1) {
        ++spelling.steps;
    }
    std::string result = transposed(parts->root, spelling).toString();
    result += parts->suffix;
    if (parts->bass) {
        result += '/';
        result += transposed(*parts->bass, spelling).toString();
    }
    return result;
}

std::optional<ChordDiagram> transposedDiagram(const ChordDiagram& diagram, int semitones,
                                              int maxFret) {
    const int rest = floorMod(semitones, 12);
    for (const int shift : {semitones, rest, rest - 12}) {
        const bool fits = std::ranges::all_of(diagram.frets, [&](std::int8_t fret) {
            return fret == ChordDiagram::kMuted || (fret + shift >= 0 && fret + shift <= maxFret);
        });
        if (!fits) {
            continue;
        }
        std::vector<std::int8_t> frets = diagram.frets;
        for (std::int8_t& fret : frets) {
            if (fret != ChordDiagram::kMuted) {
                fret = static_cast<std::int8_t>(fret + shift);
            }
        }
        return ChordDiagram::ofFrets(std::move(frets));
    }
    return std::nullopt;
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
            MovedChords chords;
            for (std::size_t m = what.first; m < last; ++m) {
                Measure content = staff.measure(m);
                transposeMeasure(content, staff, interval, followed, m, chords);
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
