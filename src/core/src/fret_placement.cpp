// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fret_placement.hpp"

#include <elara/core/effects.hpp>
#include <elara/core/pitch.hpp>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>

namespace elara::core::detail {

namespace {

void placeBeat(std::vector<Note>& notes, const Tuning& tuning, int capo, Followed& followed,
               std::size_t measure) {
    const auto fretFor = [&](int string, int midi) {
        const int fret = midi - tuning.openString(string).midiNumber() - capo;
        return fret >= 0 && capo + fret <= Tuning::kMaxFret ? fret : -1;
    };
    const auto validString = [&](int string) {
        return string >= 1 && string <= tuning.stringCount();
    };
    std::array<bool, Tuning::kMaxStrings + 1> taken{};
    std::vector<std::size_t> homeless;
    std::vector<int> before(notes.size(), 0); // each note's string as given

    // Each note keeps its string when its fret there is on the neck (a
    // natural harmonic: when it is the same fret), unless it is tied or
    // hammered from a note that moved.
    for (std::size_t i = 0; i < notes.size(); ++i) {
        Note& note = notes[i];
        if (!note.position) {
            continue;
        }
        const int string = note.position->string;
        before[i] = string;
        const bool joined = note.effects.tiedFromPrevious || note.effects.legatoFromPrevious;
        const bool follows = joined && validString(string) &&
                             followed[static_cast<std::size_t>(string)] != 0 &&
                             followed[static_cast<std::size_t>(string)] != string;
        const bool natural = note.effects.harmonic.kind == HarmonicKind::Natural;
        const int fret = validString(string) ? fretFor(string, note.pitch.midiNumber()) : -1;
        if (!follows && fret >= 0 && (!natural || fret == note.position->fret) &&
            !taken[static_cast<std::size_t>(string)]) {
            note.position->fret = static_cast<std::int8_t>(fret);
            taken[static_cast<std::size_t>(string)] = true;
        } else {
            homeless.push_back(i);
        }
    }

    // The others take the nearest free string that can play them, of two as
    // near the higher; a tied or hammered note first tries its note's string.
    for (const std::size_t i : homeless) {
        Note& note = notes[i];
        const int string = note.position->string;
        const int node = note.position->fret;
        const bool joined = note.effects.tiedFromPrevious || note.effects.legatoFromPrevious;
        const int preferred =
            joined && validString(string) ? followed[static_cast<std::size_t>(string)] : 0;
        const auto place = [&](bool keepFret) {
            const auto fits = [&](int candidate) {
                if (!validString(candidate) || taken[static_cast<std::size_t>(candidate)]) {
                    return false;
                }
                const int fret = fretFor(candidate, note.pitch.midiNumber());
                if (fret < 0 || (keepFret && fret != node)) {
                    return false;
                }
                note.position = FretPosition{.string = static_cast<std::int8_t>(candidate),
                                             .fret = static_cast<std::int8_t>(fret)};
                taken[static_cast<std::size_t>(candidate)] = true;
                return true;
            };
            if (preferred != 0 && fits(preferred)) {
                return true;
            }
            for (int distance = 0; distance <= Tuning::kMaxStrings; ++distance) {
                if (fits(string - distance) || fits(string + distance)) {
                    return true;
                }
            }
            return false;
        };
        if (note.effects.harmonic.kind == HarmonicKind::Natural) {
            // The same node on a string tuned that much higher or lower rings
            // the same harmonic; otherwise the node goes over a fretted note.
            if (place(true)) {
                continue;
            }
            note.pitch = Pitch::fromMidi(note.pitch.midiNumber() - node,
                                         note.pitch.alter() < 0 ? SpellingPreference::Flats
                                                                : SpellingPreference::Sharps);
            note.effects.harmonic.kind = HarmonicKind::Artificial;
        }
        if (!place(false)) {
            throw std::invalid_argument(std::format("measure {}: no free string can play {}",
                                                    measure + 1, note.pitch.toString()));
        }
    }

    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (validString(before[i])) {
            followed[static_cast<std::size_t>(before[i])] = notes[i].position->string;
        }
    }
}

} // namespace

void placeOnStrings(Measure& content, const Tuning& tuning, int capo, FollowedByVoice& followed,
                    std::size_t measure) {
    for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
        for (Beat& beat : content.voices[v].beats) {
            placeBeat(beat.notes, tuning, capo, followed[v], measure);
        }
    }
}

} // namespace elara::core::detail
