// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <tuple>

namespace elara::core {

namespace {

// A note of the previous beat that a tie can continue.
struct OpenNote {
    int key = 0;
    int string = 0; // 0 when the note has no fret position
    std::size_t index = 0;
};

// Appends the notes of one voice of one staff, joining ties.
void addVoice(const Song& song, const std::vector<Fraction>& starts, std::size_t t, std::size_t s,
              std::size_t v, std::vector<SoundingNote>& out) {
    const Staff& staff = song.track(t).staff(s);
    std::vector<OpenNote> open;
    std::vector<OpenNote> next;
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        const Voice& voice = staff.measure(m).voices[v];
        if (voice.isEmpty()) {
            open.clear(); // a measure rest
            continue;
        }
        Fraction position = starts[m];
        for (const Beat& beat : voice.beats) {
            const Fraction length = beat.duration.length();
            next.clear();
            for (const Note& note : beat.notes) {
                const int key = note.pitch.midiNumber();
                const int string = note.position ? note.position->string : 0;
                const auto joined = std::ranges::find_if(open, [&](const OpenNote& candidate) {
                    return candidate.key == key &&
                           (string == 0 || candidate.string == 0 || candidate.string == string);
                });
                if (note.effects.tiedFromPrevious && joined != open.end()) {
                    out[joined->index].length += length;
                    next.push_back({.key = key, .string = string, .index = joined->index});
                    continue;
                }
                next.push_back({.key = key, .string = string, .index = out.size()});
                out.push_back({.track = t,
                               .staff = s,
                               .voice = v,
                               .start = position,
                               .length = length,
                               .key = key,
                               .velocity = dynamicVelocity(note.effects.dynamic),
                               .measure = m});
            }
            std::swap(open, next);
            position += length;
        }
    }
}

} // namespace

std::vector<SoundingNote> soundingNotes(const Song& song) {
    std::vector<Fraction> starts(song.measureCount());
    Fraction start;
    for (std::size_t m = 0; m < starts.size(); ++m) {
        starts[m] = start;
        start += song.measureHeader(m).timeSignature.measureLength();
    }
    std::vector<SoundingNote> notes;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                addVoice(song, starts, t, s, v, notes);
            }
        }
    }
    std::ranges::stable_sort(notes, [](const SoundingNote& a, const SoundingNote& b) {
        return std::tie(a.start, a.track, a.staff, a.voice, a.key) <
               std::tie(b.start, b.track, b.staff, b.voice, b.key);
    });
    return notes;
}

} // namespace elara::core
