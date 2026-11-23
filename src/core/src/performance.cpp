// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/parallel.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

// Voices times measures from which the voices are read on several threads.
constexpr std::size_t kParallelVoiceMeasures = 4096;

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

// Puts the notes of each chord (notes of one voice starting together) of
// `list`, which is in start order, in key order; equal keys keep their order.
void keyOrderChords(std::vector<SoundingNote>& list) {
    for (std::size_t first = 0; first < list.size();) {
        std::size_t last = first + 1;
        while (last < list.size() && list[last].start == list[first].start) {
            ++last;
        }
        if (last - first > 1) {
            std::stable_sort(
                list.begin() + static_cast<std::ptrdiff_t>(first),
                list.begin() + static_cast<std::ptrdiff_t>(last),
                [](const SoundingNote& a, const SoundingNote& b) { return a.key < b.key; });
        }
        first = last;
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
    // One list per voice of every staff, read on all cores. Each is in start
    // order once the notes of each chord are put in key order.
    struct Part {
        std::size_t track = 0;
        std::size_t staff = 0;
        std::size_t voice = 0;
    };
    std::vector<Part> parts;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                parts.push_back({.track = t, .staff = s, .voice = v});
            }
        }
    }
    std::vector<std::vector<SoundingNote>> lists(parts.size());
    const std::size_t work = parts.size() * song.measureCount();
    forEachIndex(0, parts.size(), threadsFor(0, work, kParallelVoiceMeasures, parts.size()),
                 [&](std::size_t i) {
                     std::vector<SoundingNote>& list = lists[i];
                     addVoice(song, starts, parts[i].track, parts[i].staff, parts[i].voice, list);
                     keyOrderChords(list);
                 });
    // Merged pairwise, neighbours first: the lists are in track, staff and
    // voice order, so the result is ordered by start, then track, staff, voice
    // and key.
    const auto earlier = [](const SoundingNote& a, const SoundingNote& b) {
        return std::tie(a.start, a.track, a.staff, a.voice, a.key) <
               std::tie(b.start, b.track, b.staff, b.voice, b.key);
    };
    std::erase_if(lists, [](const std::vector<SoundingNote>& list) { return list.empty(); });
    while (lists.size() > 1) {
        std::vector<std::vector<SoundingNote>> merged((lists.size() + 1) / 2);
        forEachIndex(0, merged.size(), threadsFor(0, work, kParallelVoiceMeasures, merged.size()),
                     [&](std::size_t i) {
                         if (2 * i + 1 == lists.size()) {
                             merged[i] = std::move(lists[2 * i]);
                             return;
                         }
                         const std::vector<SoundingNote>& a = lists[2 * i];
                         const std::vector<SoundingNote>& b = lists[2 * i + 1];
                         merged[i].resize(a.size() + b.size());
                         std::ranges::merge(a, b, merged[i].begin(), earlier);
                     });
        lists = std::move(merged);
    }
    return lists.empty() ? std::vector<SoundingNote>{} : std::move(lists.front());
}

} // namespace elara::core
