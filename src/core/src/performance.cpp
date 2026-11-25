// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/parallel.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

// Voices times measures from which the voices are read on several threads.
constexpr std::size_t kParallelVoiceMeasures = 4096;

// Velocity between two neighbouring dynamics (mf 80, f 96).
constexpr int kDynamicStep = 16;

// Beats of one voice under one hairpin, in whole notes from the start of the
// song.
struct HairpinSpan {
    Fraction start{};
    Fraction end{};
    Hairpin hairpin = Hairpin::None;
};

// The loudest velocity struck at one start.
struct Strike {
    Fraction start{};
    int velocity = 0;
};

// A note of the previous beat that a tie can continue.
struct OpenNote {
    int key = 0;
    int string = 0; // 0 when the note has no fret position
    std::size_t index = 0;
};

// Appends the notes of one voice of one staff, joining ties, and the spans of
// its hairpins.
void addVoice(const Song& song, const std::vector<Fraction>& starts, std::size_t t, std::size_t s,
              std::size_t v, std::vector<SoundingNote>& out, std::vector<HairpinSpan>& spans) {
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
        for (std::size_t b = 0; b < voice.beats.size(); ++b) {
            const Beat& beat = voice.beats[b];
            const Fraction length = beat.duration.length();
            if (const Hairpin hairpin = beat.effects.hairpin; hairpin != Hairpin::None) {
                if (!spans.empty() && spans.back().hairpin == hairpin &&
                    spans.back().end == position) {
                    spans.back().end = position + length;
                } else {
                    spans.push_back(
                        {.start = position, .end = position + length, .hairpin = hairpin});
                }
            }
            next.clear();
            for (std::size_t n = 0; n < beat.notes.size(); ++n) {
                const Note& note = beat.notes[n];
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
                               .beat = static_cast<std::uint32_t>(b),
                               .note = static_cast<std::uint32_t>(n),
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

// Adds the notes of `notes`, in start order, to `strikes`, in start order,
// keeping the loudest velocity of each start.
void addStrikes(const std::vector<SoundingNote>& notes, std::vector<Strike>& strikes,
                std::vector<Strike>& scratch) {
    scratch.clear();
    for (const SoundingNote& note : notes) {
        if (!scratch.empty() && scratch.back().start == note.start) {
            scratch.back().velocity = std::max(scratch.back().velocity, note.velocity);
        } else {
            scratch.push_back({.start = note.start, .velocity = note.velocity});
        }
    }
    const auto middle = static_cast<std::ptrdiff_t>(strikes.size());
    strikes.insert(strikes.end(), scratch.begin(), scratch.end());
    std::ranges::inplace_merge(strikes, strikes.begin() + middle, std::less<>{}, &Strike::start);
    std::vector<Strike>::iterator out = strikes.begin();
    for (auto in = strikes.begin(); in != strikes.end(); ++in) {
        if (in != strikes.begin() && std::prev(out)->start == in->start) {
            std::prev(out)->velocity = std::max(std::prev(out)->velocity, in->velocity);
        } else {
            *out++ = *in;
        }
    }
    strikes.erase(out, strikes.end());
}

// Ramps the velocities of the notes of one staff (`voices`, one list per
// voice) under its hairpins (ADR-0078). A wedge leads from the loudest notes
// struck at or before its start to the loudest struck at or after its end
// when they are louder (crescendo) or softer (diminuendo), or else one
// dynamic further; each note under it moves by the share of that change its
// start has reached.
void rampHairpins(std::span<std::vector<SoundingNote>> voices,
                  std::span<const std::vector<HairpinSpan>> spanLists) {
    std::vector<HairpinSpan> wedges;
    for (const std::vector<HairpinSpan>& list : spanLists) {
        wedges.insert(wedges.end(), list.begin(), list.end());
    }
    if (wedges.empty()) {
        return;
    }
    // One wedge per run of the same hairpin, whichever voices hold it.
    std::ranges::stable_sort(wedges, std::less<>{}, &HairpinSpan::start);
    std::size_t kept = 0;
    for (std::size_t i = 1; i < wedges.size(); ++i) {
        HairpinSpan& last = wedges[kept];
        if (wedges[i].hairpin == last.hairpin && wedges[i].start <= last.end) {
            last.end = std::max(last.end, wedges[i].end);
        } else {
            wedges[++kept] = wedges[i];
        }
    }
    wedges.resize(kept + 1);

    std::vector<Strike> strikes;
    std::vector<Strike> scratch;
    for (const std::vector<SoundingNote>& notes : voices) {
        addStrikes(notes, strikes, scratch);
    }
    std::vector<int> changes;
    changes.reserve(wedges.size());
    for (const HairpinSpan& wedge : wedges) {
        const auto upTo =
            std::ranges::upper_bound(strikes, wedge.start, std::less<>{}, &Strike::start);
        const auto after =
            std::ranges::lower_bound(strikes, wedge.end, std::less<>{}, &Strike::start);
        const int from =
            upTo == strikes.begin() ? dynamicVelocity(Dynamic::MF) : std::prev(upTo)->velocity;
        const int direction = wedge.hairpin == Hairpin::Crescendo ? 1 : -1;
        int to = from + direction * kDynamicStep;
        if (after != strikes.end() && (after->velocity - from) * direction > 0) {
            to = after->velocity;
        }
        changes.push_back(std::clamp(to, 1, 127) - from);
    }
    for (std::vector<SoundingNote>& notes : voices) {
        std::size_t w = 0;
        for (SoundingNote& note : notes) {
            while (w < wedges.size() && wedges[w].end <= note.start) {
                ++w;
            }
            if (w == wedges.size()) {
                break;
            }
            const HairpinSpan& wedge = wedges[w];
            if (note.start < wedge.start) {
                continue;
            }
            const double share =
                ((note.start - wedge.start) / (wedge.end - wedge.start)).toDouble();
            note.velocity = std::clamp(
                note.velocity + static_cast<int>(std::lround(changes[w] * share)), 1, 127);
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
    std::vector<std::vector<HairpinSpan>> spans(parts.size());
    const std::size_t work = parts.size() * song.measureCount();
    forEachIndex(0, parts.size(), threadsFor(0, work, kParallelVoiceMeasures, parts.size()),
                 [&](std::size_t i) {
                     std::vector<SoundingNote>& list = lists[i];
                     addVoice(song, starts, parts[i].track, parts[i].staff, parts[i].voice, list,
                              spans[i]);
                     keyOrderChords(list);
                 });
    // The voices of a staff are consecutive parts.
    for (std::size_t first = 0; first < parts.size(); first += Measure::kMaxVoices) {
        rampHairpins(
            std::span(lists).subspan(first, Measure::kMaxVoices),
            std::span<const std::vector<HairpinSpan>>(spans).subspan(first, Measure::kMaxVoices));
    }
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
