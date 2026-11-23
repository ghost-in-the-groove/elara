// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer (ADR-0017): positions are exact whole notes along the play
// order until they are turned into seconds, so notes that meet in the score
// meet in time.
#include <elara/audio/sequence.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace elara::audio {

namespace {

using core::Fraction;

constexpr int kKeys = 128;
constexpr std::size_t kParallelNotes = 20'000;  // notes from which work goes on several threads
constexpr std::size_t kParallelEvents = 40'000; // events from which merges go on several threads
constexpr std::size_t kNotesPerBlock = 8192;    // written notes timed together on one thread

// The tempo of every written measure as segments: from an offset (in whole
// notes, as a double) on, seconds pass at a rate per whole note. Exact
// offsets are converted once, so equal positions give equal seconds.
class TempoMap {
public:
    explicit TempoMap(const core::Song& song) {
        first_.reserve(song.measureCount() + 1);
        core::Tempo tempo = song.startTempo();
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            first_.push_back(segments_.size());
            segments_.push_back({.offset = 0.0, .seconds = 0.0, .perWhole = perWhole(tempo)});
            for (const core::TempoChange& change : song.measureHeader(m).tempoChanges) {
                const double offset = change.offset.toDouble();
                const Segment& before = segments_.back();
                tempo = change.tempo;
                segments_.push_back(
                    {.offset = offset,
                     .seconds = before.seconds + (offset - before.offset) * before.perWhole,
                     .perWhole = perWhole(tempo)});
            }
        }
        first_.push_back(segments_.size());
    }

    // Seconds from the start of measure m to `offset` whole notes into it;
    // past its end, its last tempo goes on. A change exactly at `offset`
    // starts after it.
    [[nodiscard]] double secondsInto(std::size_t m, double offset) const noexcept {
        std::size_t s = first_[m];
        while (s + 1 < first_[m + 1] && segments_[s + 1].offset < offset) {
            ++s;
        }
        const Segment& segment = segments_[s];
        return segment.seconds + (offset - segment.offset) * segment.perWhole;
    }

private:
    struct Segment {
        double offset = 0.0;
        double seconds = 0.0; // from the start of the measure
        double perWhole = 0.0;
    };

    static double perWhole(const core::Tempo& tempo) { return tempo.secondsFor(Fraction(1)); }

    std::vector<Segment> segments_;
    std::vector<std::size_t> first_; // first segment of each measure
};

bool earlier(const NoteEvent& a, const NoteEvent& b) {
    if (a.time != b.time) {
        return a.time < b.time;
    }
    if (a.kind != b.kind) {
        return a.kind < b.kind;
    }
    return a.track != b.track ? a.track < b.track : a.key < b.key;
}

// The events of one track from its notes (`mine`, indices into `notes` in
// start order): a key struck while it sounds is released first, unless it was
// struck at that very time, and released when its last note ends. Releases
// at a time come before attacks. Ordered as events() are.
std::vector<NoteEvent> trackEvents(const std::vector<PlayedNote>& notes,
                                   std::span<const std::size_t> mine, std::size_t track) {
    struct Release {
        double time = 0.0;
        int key = 60;
    };
    std::vector<Release> releases;
    releases.reserve(mine.size());
    for (const std::size_t i : mine) {
        if (notes[i].key >= 0 && notes[i].key < kKeys) {
            releases.push_back({.time = notes[i].end, .key = notes[i].key});
        }
    }
    std::ranges::sort(releases, {}, &Release::time);

    std::vector<NoteEvent> events;
    events.reserve(2 * releases.size() + releases.size() / 8);
    std::array<int, kKeys> sounding{};
    std::array<double, kKeys> struck{};
    const auto release = [&](const Release& edge) {
        int& count = sounding[static_cast<std::size_t>(edge.key)];
        if (count > 0 && --count == 0) {
            events.push_back({.time = edge.time,
                              .kind = EventKind::NoteOff,
                              .track = track,
                              .key = edge.key,
                              .velocity = 0});
        }
    };
    std::size_t next = 0;
    for (const std::size_t i : mine) {
        const PlayedNote& note = notes[i];
        if (note.key < 0 || note.key >= kKeys) {
            continue;
        }
        while (next < releases.size() && releases[next].time <= note.start) {
            release(releases[next++]);
        }
        const auto key = static_cast<std::size_t>(note.key);
        int& count = sounding[key];
        if (count > 0 && struck[key] == note.start) {
            ++count;
            continue;
        }
        if (count > 0) {
            events.push_back({.time = note.start,
                              .kind = EventKind::NoteOff,
                              .track = track,
                              .key = note.key,
                              .velocity = 0});
        }
        events.push_back({.time = note.start,
                          .kind = EventKind::NoteOn,
                          .track = track,
                          .key = note.key,
                          .velocity = note.velocity});
        struck[key] = note.start;
        ++count;
    }
    while (next < releases.size()) {
        release(releases[next++]);
    }
    std::ranges::sort(events, earlier);
    return events;
}

// A written note once for every time it is played: seconds from the start of
// its measure to its attack and, when it ends in that measure, to its end;
// otherwise where it ends in whole notes from that measure's start.
struct Timing {
    double start = 0.0;
    double end = 0.0;
    bool crosses = false;
    Fraction endOffset{};
};

} // namespace

Sequence::Sequence(const core::Song& song) {
    const std::size_t count = song.measureCount();
    const TempoMap tempo(song);
    std::vector<Fraction> lengths;
    std::vector<Fraction> written; // where each written measure starts
    lengths.reserve(count);
    written.reserve(count);
    Fraction position;
    for (std::size_t m = 0; m < count; ++m) {
        lengths.push_back(song.measureHeader(m).timeSignature.measureLength());
        written.push_back(position);
        position += lengths.back();
    }

    // The measures as played.
    const std::vector<std::size_t> order = song.playOrder();
    measures_.reserve(order.size());
    double seconds = 0.0;
    for (const std::size_t m : order) {
        const double end = seconds + tempo.secondsInto(m, lengths[m].toDouble());
        measures_.push_back(PlayedMeasure{.measure = m, .start = seconds, .end = end});
        seconds = end;
    }
    duration_ = seconds;
    if (measures_.empty()) {
        return;
    }

    // Every written note timed once in its measure.
    const std::vector<core::SoundingNote> sounding = core::soundingNotes(song);
    std::vector<Timing> timings(sounding.size());
    const std::size_t blocks = (sounding.size() + kNotesPerBlock - 1) / kNotesPerBlock;
    core::forEachIndex(0, blocks, core::threadsFor(0, sounding.size(), kParallelNotes, blocks),
                       [&](std::size_t block) {
                           const std::size_t last =
                               std::min(sounding.size(), (block + 1) * kNotesPerBlock);
                           for (std::size_t i = block * kNotesPerBlock; i < last; ++i) {
                               const core::SoundingNote& note = sounding[i];
                               const Fraction offset = note.start - written[note.measure];
                               const Fraction end = offset + note.length;
                               Timing& timing = timings[i];
                               timing.start = tempo.secondsInto(note.measure, offset.toDouble());
                               timing.crosses = end > lengths[note.measure];
                               if (timing.crosses) {
                                   timing.endOffset = end;
                               } else {
                                   timing.end = tempo.secondsInto(note.measure, end.toDouble());
                               }
                           }
                       });
    // Where a note crossing out of played measure k ends: over the measures
    // played after it, the last one going on past its end.
    const auto endAfter = [&](std::size_t k, Fraction into) {
        while (k + 1 < measures_.size() && into > lengths[measures_[k].measure]) {
            into -= lengths[measures_[k].measure];
            ++k;
        }
        return measures_[k].start + tempo.secondsInto(measures_[k].measure, into.toDouble());
    };

    // The notes of every played measure.
    std::vector<std::size_t> firstOf(count + 1);
    for (std::size_t m = 0; m <= count; ++m) {
        firstOf[m] = static_cast<std::size_t>(
            std::ranges::lower_bound(sounding, m, {}, &core::SoundingNote::measure) -
            sounding.begin());
    }
    std::size_t played = 0;
    for (const PlayedMeasure& measure : measures_) {
        played += firstOf[measure.measure + 1] - firstOf[measure.measure];
    }
    notes_.reserve(played);
    for (std::size_t k = 0; k < measures_.size(); ++k) {
        const std::size_t m = measures_[k].measure;
        const double base = measures_[k].start;
        for (std::size_t i = firstOf[m]; i < firstOf[m + 1]; ++i) {
            const core::SoundingNote& note = sounding[i];
            const Timing& timing = timings[i];
            notes_.push_back(PlayedNote{.track = note.track,
                                        .staff = note.staff,
                                        .voice = note.voice,
                                        .pass = k,
                                        .start = base + timing.start,
                                        .end = timing.crosses ? endAfter(k, timing.endOffset)
                                                              : base + timing.end,
                                        .key = note.key,
                                        .velocity = note.velocity});
        }
    }

    // The notes of each track, in order (a counting sort by track).
    const std::size_t trackCount = song.trackCount();
    std::vector<std::size_t> trackStart(trackCount + 1);
    for (const PlayedNote& note : notes_) {
        ++trackStart[note.track + 1];
    }
    for (std::size_t t = 0; t < trackCount; ++t) {
        trackStart[t + 1] += trackStart[t];
    }
    std::vector<std::size_t> byTrack(notes_.size());
    {
        std::vector<std::size_t> cursor(trackStart.begin(), trackStart.end() - 1);
        for (std::size_t i = 0; i < notes_.size(); ++i) {
            byTrack[cursor[notes_[i].track]++] = i;
        }
    }

    // Note events, track by track on all cores, then merged in time.
    std::vector<std::vector<NoteEvent>> tracks(trackCount);
    core::forEachIndex(
        0, trackCount, core::threadsFor(0, notes_.size(), kParallelNotes, trackCount),
        [&](std::size_t t) {
            tracks[t] = trackEvents(
                notes_,
                std::span(byTrack).subspan(trackStart[t], trackStart[t + 1] - trackStart[t]), t);
        });
    // Every list is ordered, so is every merge of two: neighbours merged
    // pairwise, each round on all cores, leave one ordered list.
    std::size_t total = 0;
    for (const std::vector<NoteEvent>& track : tracks) {
        total += track.size();
    }
    while (tracks.size() > 1) {
        std::vector<std::vector<NoteEvent>> merged((tracks.size() + 1) / 2);
        core::forEachIndex(0, merged.size(),
                           core::threadsFor(0, total, kParallelEvents, merged.size()),
                           [&](std::size_t i) {
                               if (2 * i + 1 == tracks.size()) {
                                   merged[i] = std::move(tracks[2 * i]);
                                   return;
                               }
                               const std::vector<NoteEvent>& a = tracks[2 * i];
                               const std::vector<NoteEvent>& b = tracks[2 * i + 1];
                               merged[i].resize(a.size() + b.size());
                               std::ranges::merge(a, b, merged[i].begin(), earlier);
                           });
        tracks = std::move(merged);
    }
    if (!tracks.empty()) {
        events_ = std::move(tracks.front());
    }
}

std::size_t Sequence::eventAt(double seconds) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::lower_bound(events_, seconds, {}, &NoteEvent::time) - events_.begin());
}

} // namespace elara::audio
