// SPDX-License-Identifier: AGPL-3.0-or-later
// MIDI import (ADR-0009): Standard MIDI Files into the notation model.
#include <elara/io/midi.hpp>

#include "archive.hpp"
#include "general_midi.hpp"
#include "midi_shared.hpp"
#include "parallel.hpp"
#include "smf.hpp"
#include "warnings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>

namespace elara::io {

namespace {

using core::Beat;
using core::Duration;
using core::Fraction;
using core::KeySignature;
using core::Note;
using core::NoteValue;
using core::Pitch;
using core::Song;
using core::TimeSignature;
using core::Tuplet;
using detail::MidiEvent;
using detail::MidiFile;
using detail::Warnings;

// ---- Gathering events -------------------------------------------------------

struct RawNote {
    std::int64_t start = 0;
    std::int64_t end = 0;
    int key = 0;
    int velocity = 0;
};

// The notes of one channel of one MIDI track: one Elara track.
struct Source {
    int channel = 0;
    std::string trackName{};
    std::string instrumentName{};
    std::optional<int> program{};
    std::optional<int> volume{}; // controller 7 before the first note
    std::optional<int> pan{};    // controller 10 before the first note
    std::vector<RawNote> notes{};
};

template <typename T>
struct Timed {
    std::int64_t tick = 0;
    T value{};
};

struct Gathered {
    std::string title{};
    std::string copyright{};
    std::vector<Timed<TimeSignature>> times{};
    std::vector<Timed<KeySignature>> keys{};
    std::vector<Timed<long long>> tempos{}; // microseconds per quarter note
    std::vector<Source> sources{};
    std::int64_t end = 0;       // last tick of any track
    std::int64_t lastStart = 0; // where the last note starts
};

void readMeta(const MidiEvent& event, Gathered& out, std::string& trackName,
              std::string& instrumentName, Warnings& warnings) {
    const std::string& p = event.payload;
    const auto byteAt = [&p](std::size_t i) {
        return static_cast<std::uint8_t>(p[i]);
    };
    switch (event.type) {
    case detail::kMetaTrackName:
        if (trackName.empty()) {
            trackName = p;
        }
        break;
    case detail::kMetaInstrumentName:
        if (instrumentName.empty()) {
            instrumentName = p;
        }
        break;
    case detail::kMetaCopyright:
        if (out.copyright.empty()) {
            out.copyright = p;
        }
        break;
    case detail::kMetaLyric:
        warnings.add("", "lyrics dropped");
        break;
    case detail::kMetaTempo:
        if (p.size() >= 3) {
            const long long micros = (static_cast<long long>(byteAt(0)) << 16) |
                                     (static_cast<long long>(byteAt(1)) << 8) | byteAt(2);
            if (micros > 0) {
                out.tempos.push_back({event.tick, micros});
            }
        }
        break;
    case detail::kMetaTimeSignature:
        if (p.size() >= 2) {
            const int numerator = byteAt(0);
            const int power = byteAt(1);
            if (numerator >= 1 && numerator <= TimeSignature::kMaxNumerator && power <= 6) {
                out.times.push_back({event.tick, TimeSignature(numerator, 1 << power)});
            } else {
                warnings.add("", "unsupported time signatures dropped");
            }
        }
        break;
    case detail::kMetaKeySignature:
        if (p.size() >= 2) {
            const int fifths = static_cast<std::int8_t>(byteAt(0));
            if (fifths >= -KeySignature::kMaxFifths && fifths <= KeySignature::kMaxFifths) {
                out.keys.push_back(
                    {event.tick, KeySignature(fifths, byteAt(1) == 1 ? core::KeyMode::Minor
                                                                     : core::KeyMode::Major)});
            }
        }
        break;
    default:
        break;
    }
}

Gathered gather(const MidiFile& file, Warnings& warnings) {
    Gathered out;
    for (std::size_t t = 0; t < file.tracks.size(); ++t) {
        const detail::MidiTrack& track = file.tracks[t];
        out.end = std::max(out.end, track.end);
        std::string trackName;
        std::string instrumentName;
        std::array<std::optional<std::size_t>, 16> sourceOf{};
        // Attacks waiting for their note-off, per channel and key (oldest first).
        std::vector<std::vector<std::pair<std::int64_t, int>>> sounding(16 * 128);
        bool hasChannelEvents = false;
        // Volume and pan per channel, as set before its first note; they go to
        // the channel's source, if it gets one.
        std::array<std::optional<int>, 16> volumeOf{};
        std::array<std::optional<int>, 16> panOf{};
        std::array<bool, 16> started{};
        const std::size_t firstSource = out.sources.size();
        const auto source = [&](int channel) -> Source& {
            auto& index = sourceOf[static_cast<std::size_t>(channel)];
            if (!index) {
                index = out.sources.size();
                out.sources.push_back(Source{.channel = channel});
            }
            return out.sources[*index];
        };
        for (const MidiEvent& event : track.events) {
            if (event.status == detail::kMetaEvent) {
                readMeta(event, out, trackName, instrumentName, warnings);
                continue;
            }
            if (!event.isChannelMessage()) {
                continue;
            }
            hasChannelEvents = true;
            const int channel = event.channel();
            const int kind = event.kind();
            const int key = event.data[0];
            auto& queue = sounding[static_cast<std::size_t>(channel * 128 + (key & 0x7F))];
            if (kind == detail::kNoteOn && event.data[1] > 0) {
                (void)source(channel);
                started[static_cast<std::size_t>(channel)] = true;
                queue.emplace_back(event.tick, event.data[1]);
            } else if (kind == detail::kNoteOff || kind == detail::kNoteOn) {
                if (!queue.empty()) {
                    source(channel).notes.push_back(RawNote{.start = queue.front().first,
                                                            .end = event.tick,
                                                            .key = key,
                                                            .velocity = queue.front().second});
                    queue.erase(queue.begin());
                }
            } else if (kind == detail::kProgramChange) {
                Source& s = source(channel);
                if (!s.program && s.notes.empty()) {
                    s.program = event.data[0];
                } else if (s.program != event.data[0]) {
                    warnings.add("", "program changes dropped");
                }
            } else if (kind == detail::kController && (event.data[0] == 7 || event.data[0] == 10)) {
                auto& setting =
                    (event.data[0] == 7 ? volumeOf : panOf)[static_cast<std::size_t>(channel)];
                if (!started[static_cast<std::size_t>(channel)]) {
                    setting = event.data[1];
                } else if (setting != event.data[1]) {
                    warnings.add("", "volume and pan changes dropped");
                }
            } else if (kind == detail::kController && event.data[0] == 64 && event.data[1] >= 64) {
                warnings.add("", "sustain pedal dropped");
            } else if (kind == detail::kPitchBend && (event.data[0] != 0 || event.data[1] != 64)) {
                warnings.add("", "pitch bends dropped");
            }
        }
        for (std::size_t channel = 0; channel < sourceOf.size(); ++channel) {
            if (sourceOf[channel]) {
                Source& s = out.sources[*sourceOf[channel]];
                s.volume = volumeOf[channel];
                s.pan = panOf[channel];
            }
        }
        // Notes still sounding at the end of the track end there.
        bool unterminated = false;
        for (int channel = 0; channel < 16; ++channel) {
            for (int key = 0; key < 128; ++key) {
                for (const auto& [start, velocity] :
                     sounding[static_cast<std::size_t>(channel * 128 + key)]) {
                    source(channel).notes.push_back(RawNote{.start = start,
                                                            .end = std::max(track.end, start),
                                                            .key = key,
                                                            .velocity = velocity});
                    unterminated = true;
                }
            }
        }
        if (unterminated) {
            warnings.add("", "notes without a note-off end with their track");
        }
        // The first track of a format 1 file without channel events is the
        // conductor track: its name is the title. In format 0 too.
        const bool conductor =
            (file.format != 0 && t == 0 && !hasChannelEvents) || (file.format == 0 && t == 0);
        if (conductor && out.title.empty()) {
            out.title = trackName;
        }
        for (std::size_t s = firstSource; s < out.sources.size(); ++s) {
            if (file.format != 0) {
                out.sources[s].trackName = trackName;
            }
            out.sources[s].instrumentName = instrumentName;
        }
    }
    for (Source& source : out.sources) {
        std::ranges::stable_sort(source.notes, [](const RawNote& a, const RawNote& b) {
            return std::tie(a.start, a.key) < std::tie(b.start, b.key);
        });
        for (const RawNote& note : source.notes) {
            out.end = std::max(out.end, note.end);
            out.lastStart = std::max(out.lastStart, note.start);
        }
    }
    const auto byTick = [](const auto& a, const auto& b) {
        return a.tick < b.tick;
    };
    std::ranges::stable_sort(out.times, byTick);
    std::ranges::stable_sort(out.keys, byTick);
    std::ranges::stable_sort(out.tempos, byTick);
    return out;
}

// ---- Measures ---------------------------------------------------------------

bool isCompound(const TimeSignature& time) {
    return time.numerator() % 3 == 0 && time.numerator() > 3 && time.denominator() >= 8;
}

struct MeasureInfo {
    Fraction start{};
    TimeSignature time{};
    KeySignature key{};
    std::vector<core::TempoChange> tempos{};
    std::size_t firstCell = 0; // index of its first grid cell in the song

    [[nodiscard]] Fraction length() const { return time.measureLength(); }
    [[nodiscard]] Fraction end() const { return start + length(); }
    /// What the beat is for writing rhythms: the dotted beat in compound meters.
    [[nodiscard]] Fraction beat() const {
        return isCompound(time) ? Fraction(3, time.denominator()) : time.beatLength();
    }
    /// The span a rhythm grid is chosen for: the beat, but at most a quarter note.
    [[nodiscard]] Fraction cell() const {
        if (isCompound(time)) {
            return Fraction(3, time.denominator());
        }
        return time.denominator() <= 4 ? Fraction(1, 4) : time.beatLength();
    }
    [[nodiscard]] std::size_t cellCount() const {
        return static_cast<std::size_t>((length() / cell()).numerator());
    }
};

// A meter that lasts `length` (a measure cut short by a meter change), or nullopt.
std::optional<TimeSignature> meterFor(const Fraction& length, int denominator) {
    const std::int64_t den = std::max<std::int64_t>(denominator, length.denominator());
    const Fraction count = length * Fraction(den);
    if (count.denominator() != 1 || count.numerator() < 1 ||
        count.numerator() > TimeSignature::kMaxNumerator || den > 64 || (den & (den - 1)) != 0) {
        return std::nullopt;
    }
    return TimeSignature(static_cast<int>(count.numerator()), static_cast<int>(den));
}

std::vector<MeasureInfo> layMeasures(const Gathered& in, int division, Warnings& warnings) {
    const auto at = [division](std::int64_t tick) {
        return Fraction(tick, 4 * std::int64_t{division});
    };
    const Fraction songEnd = at(in.end);
    std::vector<MeasureInfo> measures;
    TimeSignature time;
    std::size_t nextTime = 0;
    const auto applyTimesUpTo = [&](const Fraction& position) {
        while (nextTime < in.times.size() && at(in.times[nextTime].tick) <= position) {
            time = in.times[nextTime++].value;
        }
    };
    Fraction position;
    applyTimesUpTo(position);
    while (position < songEnd) {
        Fraction length = time.measureLength();
        TimeSignature meter = time;
        if (nextTime < in.times.size()) {
            const Fraction change = at(in.times[nextTime].tick);
            if (change < position + length) {
                if (const auto cut = meterFor(change - position, time.denominator())) {
                    meter = *cut;
                    length = change - position;
                } else {
                    // Applied where this measure ends.
                    warnings.add("", "meter changes inside a measure moved to the next measure",
                                 measures.size());
                }
            }
        }
        measures.push_back(MeasureInfo{.start = position, .time = meter});
        position += length;
        applyTimesUpTo(position);
    }
    // Played music often releases its last notes, and ends its tracks, just after
    // the last barline: a final measure that would hold only that (less than a
    // sixteenth note, no note starting in it) is not kept. An end exactly on a
    // barline, as written files have, always is.
    if (measures.size() > 1) {
        const MeasureInfo& last = measures.back();
        if (songEnd - last.start < Fraction(1, 16) && songEnd < last.end() &&
            at(in.lastStart) < last.start) {
            measures.pop_back();
        }
    }

    // Keys at measure starts; tempo changes inside measures.
    std::size_t cells = 0;
    std::size_t nextKey = 0;
    KeySignature key;
    for (std::size_t m = 0; m < measures.size(); ++m) {
        MeasureInfo& measure = measures[m];
        while (nextKey < in.keys.size() && at(in.keys[nextKey].tick) <= measure.start) {
            if (at(in.keys[nextKey].tick) < measure.start) {
                warnings.add("", "key changes inside a measure moved to the next measure", m - 1);
            }
            key = in.keys[nextKey++].value;
        }
        measure.key = key;
        measure.firstCell = cells;
        cells += measure.cellCount();
    }
    for (const auto& tempo : in.tempos) {
        const Fraction when = at(tempo.tick);
        if (when == Fraction(0) || measures.empty() || when >= measures.back().end()) {
            continue; // the start tempo, or after the last note
        }
        const auto after = std::ranges::upper_bound(measures, when, {}, &MeasureInfo::start);
        MeasureInfo& measure = *std::prev(after);
        const double bpm = std::clamp(detail::bpmFromMicroseconds(tempo.value),
                                      core::Tempo::kMinBpm, core::Tempo::kMaxBpm);
        const core::TempoChange change{.offset = when - measure.start, .tempo = core::Tempo(bpm)};
        if (!measure.tempos.empty() && measure.tempos.back().offset == change.offset) {
            measure.tempos.back() = change; // the last of several at one time wins
        } else {
            measure.tempos.push_back(change);
        }
    }
    return measures;
}

// ---- Rhythm grids -------------------------------------------------------------

// A plain written duration (no tuplet, up to two dots) and its length.
struct Plain {
    Duration duration{};
    Fraction length{};
};

// Every plain duration, longest first (lengths computed once: normalising
// fractions is most of the cost of writing rhythms).
const std::vector<Plain>& plainDurations() {
    static const std::vector<Plain> durations = [] {
        std::vector<Plain> all;
        for (int value = 0; value <= static_cast<int>(NoteValue::HundredTwentyEighth); ++value) {
            for (int dots = 0; dots <= 2; ++dots) {
                if (value + dots <= static_cast<int>(NoteValue::HundredTwentyEighth)) {
                    const Duration duration(static_cast<NoteValue>(value), dots);
                    all.push_back(Plain{.duration = duration, .length = duration.length()});
                }
            }
        }
        std::ranges::stable_sort(
            all, [](const Plain& a, const Plain& b) { return a.length > b.length; });
        return all;
    }();
    return durations;
}

bool isPlainLength(const Fraction& length) {
    return std::ranges::any_of(plainDurations(),
                               [&length](const Plain& plain) { return plain.length == length; });
}

// How a grid of `subdivisions` per cell is written: plain values, or a tuplet
// whose written unit is the next longer power-of-two value.
struct GridSpelling {
    Fraction unit{};    // sounding length of one grid step
    Fraction written{}; // written length of one grid step
    Tuplet tuplet{};    // none for plain grids
};

std::optional<GridSpelling> computeGridSpelling(const Fraction& cell, int subdivisions) {
    const Fraction unit = cell / Fraction(subdivisions);
    if (isPlainLength(unit)) {
        return GridSpelling{.unit = unit, .written = unit};
    }
    for (int value = static_cast<int>(NoteValue::HundredTwentyEighth); value >= 0; --value) {
        const Fraction written = core::noteValueLength(static_cast<NoteValue>(value));
        if (written >= unit) {
            const Fraction ratio = unit / written; // normal / actual
            if (ratio.numerator() > Tuplet::kMaxCount || ratio.denominator() > Tuplet::kMaxCount) {
                return std::nullopt;
            }
            return GridSpelling{.unit = unit,
                                .written = written,
                                .tuplet = Tuplet(static_cast<int>(ratio.denominator()),
                                                 static_cast<int>(ratio.numerator()))};
        }
    }
    return std::nullopt;
}

// Memoized per thread: a song has few cell lengths and grids.
const std::optional<GridSpelling>& gridSpelling(const Fraction& cell, int subdivisions) {
    thread_local std::map<std::tuple<std::int64_t, std::int64_t, int>, std::optional<GridSpelling>>
        cache;
    const auto [entry, added] =
        cache.try_emplace({cell.numerator(), cell.denominator(), subdivisions});
    if (added) {
        entry->second = computeGridSpelling(cell, subdivisions);
    }
    return entry->second;
}

struct Candidate {
    int subdivisions;
    double weight; // performance fitting: higher costs more
};

// Grids tried for exact (quantized) music, simplest first.
constexpr std::array<Candidate, 15> kSimpleExact{{{1, 1},
                                                  {2, 1},
                                                  {4, 1},
                                                  {3, 1},
                                                  {8, 1},
                                                  {6, 1},
                                                  {16, 1},
                                                  {12, 1},
                                                  {5, 1},
                                                  {10, 1},
                                                  {7, 1},
                                                  {14, 1},
                                                  {32, 1},
                                                  {24, 1},
                                                  {9, 1}}};
constexpr std::array<Candidate, 11> kCompoundExact{
    {{1, 1}, {3, 1}, {6, 1}, {2, 1}, {12, 1}, {4, 1}, {24, 1}, {8, 1}, {5, 1}, {9, 1}, {7, 1}}};
// Grids for played music: sixteenths and triplets, finer grids only when they
// fit much better.
// Note ends do not count: releases are loose in played music.
constexpr std::array<Candidate, 6> kSimplePlayed{
    {{1, 0.6}, {2, 0.8}, {4, 1.0}, {3, 1.0}, {8, 1.6}, {6, 1.6}}};
constexpr std::array<Candidate, 6> kCompoundPlayed{
    {{1, 0.6}, {3, 0.8}, {6, 1.0}, {2, 1.0}, {12, 1.6}, {4, 1.3}}};

struct Point {
    double tick = 0;     // where it is
    double weight = 1.0; // 0 for note ends: releases do not choose grids for played music
};

// Grid choice for one track: one subdivision per cell of the song.
class Grids {
public:
    Grids(const std::vector<MeasureInfo>& measures, int division)
        : measures_(measures), ticksPerWhole_(4.0 * division) {
        const std::size_t cells =
            measures.empty() ? 0 : measures.back().firstCell + measures.back().cellCount();
        subdivisions_.assign(cells, 1);
        points_.resize(cells);
    }

    void addPoint(std::int64_t tick, double weight) {
        const auto [cell, offset] = locate(static_cast<double>(tick));
        if (cell >= points_.size() || offset <= 0.0) {
            return; // on a cell boundary: every grid holds it
        }
        points_[cell].push_back(Point{.tick = static_cast<double>(tick), .weight = weight});
    }

    // Picks every cell's grid (see the header comment of importMidi).
    void choose() {
        std::size_t withPoints = 0;
        std::size_t exact = 0;
        std::vector<std::optional<int>> exactGrid(points_.size());
        for (std::size_t c = 0; c < points_.size(); ++c) {
            if (points_[c].empty()) {
                continue;
            }
            ++withPoints;
            exactGrid[c] = exactFor(c);
            if (exactGrid[c]) {
                ++exact;
            }
        }
        const bool quantized = withPoints == 0 || exact * 10 >= withPoints * 9;
        for (std::size_t c = 0; c < points_.size(); ++c) {
            if (points_[c].empty()) {
                continue;
            }
            subdivisions_[c] = quantized && exactGrid[c] ? *exactGrid[c] : bestFor(c);
        }
    }

    // The grid point nearest to `tick`.
    [[nodiscard]] Fraction snap(std::int64_t tick) const {
        const auto [cell, offset] = locate(static_cast<double>(tick));
        if (cell >= subdivisions_.size()) {
            return Fraction(tick, static_cast<std::int64_t>(ticksPerWhole_));
        }
        const MeasureInfo& measure = measureOf(cell);
        const Fraction cellStart =
            measure.start +
            measure.cell() * Fraction(static_cast<std::int64_t>(cell - measure.firstCell));
        const int s = subdivisions_[cell];
        const double unitTicks = measure.cell().toDouble() * ticksPerWhole_ / s;
        const auto k = std::clamp<std::int64_t>(std::llround(offset / unitTicks), 0, s);
        return cellStart + measure.cell() * Fraction(k, s);
    }

    // One grid step of the cell at `position`.
    [[nodiscard]] Fraction unitAt(const Fraction& position) const {
        const std::size_t cell = cellAt(position);
        if (cell >= subdivisions_.size()) {
            return Fraction(1, 16);
        }
        return measureOf(cell).cell() / Fraction(subdivisions_[cell]);
    }

    [[nodiscard]] int subdivisionsOf(std::size_t cell) const { return subdivisions_[cell]; }

private:
    [[nodiscard]] const MeasureInfo& measureOf(std::size_t cell) const {
        const auto after = std::ranges::upper_bound(measures_, cell, {}, &MeasureInfo::firstCell);
        return *std::prev(after);
    }

    [[nodiscard]] std::size_t cellAt(const Fraction& position) const {
        const auto after = std::ranges::upper_bound(measures_, position, {}, &MeasureInfo::start);
        if (after == measures_.begin()) {
            return 0;
        }
        const MeasureInfo& measure = *std::prev(after);
        const Fraction offset = (position - measure.start) / measure.cell();
        return measure.firstCell +
               static_cast<std::size_t>(offset.numerator() / offset.denominator());
    }

    // Cell index and ticks from the cell's start.
    [[nodiscard]] std::pair<std::size_t, double> locate(double tick) const {
        const double whole = tick / ticksPerWhole_;
        auto after = std::ranges::upper_bound(
            measures_, whole, {}, [](const MeasureInfo& m) { return m.start.toDouble(); });
        if (after == measures_.begin() || whole >= measures_.back().end().toDouble()) {
            return {points_.size(), 0.0}; // outside the song (its end is on the grid)
        }
        const MeasureInfo& measure = *std::prev(after);
        const double cellTicks = measure.cell().toDouble() * ticksPerWhole_;
        const double fromStart = tick - measure.start.toDouble() * ticksPerWhole_;
        auto index = static_cast<std::size_t>(std::floor(fromStart / cellTicks + 1e-9));
        index = std::min(index, measure.cellCount() == 0 ? 0 : measure.cellCount() - 1);
        const double offset = fromStart - static_cast<double>(index) * cellTicks;
        return {measure.firstCell + index, offset < 1e-6 ? 0.0 : offset};
    }

    [[nodiscard]] bool usable(const MeasureInfo& measure, int subdivisions) const {
        const Fraction cell = measure.cell();
        const auto& spelling = gridSpelling(cell, subdivisions);
        const double unitTicks = cell.toDouble() * ticksPerWhole_ / subdivisions;
        return spelling && spelling->written >= Fraction(1, 128) && unitTicks >= 2.0;
    }

    // Largest distance from a point to the grid, and the weighted sum.
    [[nodiscard]] std::pair<double, double> error(std::size_t cell, int subdivisions) const {
        const MeasureInfo& measure = measureOf(cell);
        const double cellStart =
            (measure.start +
             measure.cell() * Fraction(static_cast<std::int64_t>(cell - measure.firstCell)))
                .toDouble() *
            ticksPerWhole_;
        const double unitTicks = measure.cell().toDouble() * ticksPerWhole_ / subdivisions;
        double largest = 0;
        double sum = 0;
        for (const Point& point : points_[cell]) {
            const double steps = (point.tick - cellStart) / unitTicks;
            const double distance = std::abs(steps - std::round(steps)) * unitTicks;
            largest = std::max(largest, distance);
            sum += distance * point.weight;
        }
        return {largest, sum};
    }

    [[nodiscard]] std::optional<int> exactFor(std::size_t cell) const {
        const MeasureInfo& measure = measureOf(cell);
        const auto exactList = [&]() -> std::span<const Candidate> {
            return isCompound(measure.time) ? std::span<const Candidate>(kCompoundExact)
                                            : std::span<const Candidate>(kSimpleExact);
        }();
        for (const Candidate& candidate : exactList) {
            if (usable(measure, candidate.subdivisions) &&
                error(cell, candidate.subdivisions).first <= 1.0) {
                return candidate.subdivisions;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] int bestFor(std::size_t cell) const {
        const MeasureInfo& measure = measureOf(cell);
        const auto played = isCompound(measure.time) ? std::span<const Candidate>(kCompoundPlayed)
                                                     : std::span<const Candidate>(kSimplePlayed);
        int best = 1;
        double bestScore = 0;
        bool first = true;
        for (const Candidate& candidate : played) {
            if (!usable(measure, candidate.subdivisions)) {
                continue;
            }
            const double score = error(cell, candidate.subdivisions).second * candidate.weight;
            if (first || score < bestScore - 1e-9) {
                best = candidate.subdivisions;
                bestScore = score;
                first = false;
            }
        }
        return best;
    }

    const std::vector<MeasureInfo>& measures_;
    double ticksPerWhole_;
    std::vector<int> subdivisions_;
    std::vector<std::vector<Point>> points_;
};

// ---- Writing rhythms ----------------------------------------------------------

bool isMultiple(const Fraction& value, const Fraction& unit) {
    return (value / unit).denominator() == 1;
}

Fraction floorTo(const Fraction& value, const Fraction& unit) {
    const Fraction count = value / unit;
    const std::int64_t whole =
        count.numerator() >= 0
            ? count.numerator() / count.denominator()
            : -((-count.numerator() + count.denominator() - 1) / count.denominator());
    return unit * Fraction(whole);
}

// The longest plain duration of at most `length` whose start `at` is a multiple
// of its length when `aligned` (rests). nullptr when none fits.
const Plain* longestPlain(const Fraction& length, const Fraction& at, bool aligned) {
    for (const Plain& plain : plainDurations()) {
        if (plain.length <= length && (!aligned || isMultiple(at, plain.length))) {
            return &plain;
        }
    }
    return nullptr;
}

// The simplest grid of each cell that holds a voice's note and rest boundaries
// (offsets from the measure start): what that voice's rhythm is written on, so
// a triplet in one voice does not turn the other voices into tuplets.
std::vector<int> voiceSubdivisions(const MeasureInfo& measure, const std::vector<Fraction>& points,
                                   const Grids& staffGrids) {
    const Fraction cell = measure.cell();
    const std::size_t count = measure.cellCount();
    std::vector<std::vector<Fraction>> inside(count);
    for (const Fraction& point : points) {
        const Fraction steps = point / cell;
        const auto index = static_cast<std::size_t>(steps.numerator() / steps.denominator());
        const Fraction offset = point - cell * Fraction(static_cast<std::int64_t>(index));
        if (index < count && offset != Fraction(0)) {
            inside[index].push_back(offset);
        }
    }
    const auto candidates = isCompound(measure.time) ? std::span<const Candidate>(kCompoundExact)
                                                     : std::span<const Candidate>(kSimpleExact);
    std::vector<int> result(count, 1);
    for (std::size_t c = 0; c < count; ++c) {
        if (inside[c].empty()) {
            continue;
        }
        result[c] = staffGrids.subdivisionsOf(measure.firstCell + c); // holds every point
        for (const Candidate& candidate : candidates) {
            const auto& spelling = gridSpelling(cell, candidate.subdivisions);
            if (spelling && spelling->written >= Fraction(1, 128) &&
                std::ranges::all_of(inside[c], [&](const Fraction& offset) {
                    return isMultiple(offset, spelling->unit);
                })) {
                result[c] = candidate.subdivisions;
                break;
            }
        }
    }
    return result;
}

// Writes [from, to) of one measure (offsets from its start) as durations:
// tuplet cells are written by themselves; a note that starts off the beat is
// cut at the next beat; otherwise the longest value that fits is taken (rests
// also aligned to their own length).
std::vector<Duration> spellRhythm(const MeasureInfo& measure, std::span<const int> subdivisions,
                                  Fraction from, const Fraction& to, bool rest) {
    std::vector<Duration> out;
    const Fraction cell = measure.cell();
    const Fraction beat = measure.beat();
    const auto spellingOf = [&](std::size_t index) -> const std::optional<GridSpelling>& {
        return gridSpelling(cell, subdivisions[index]);
    };
    while (from < to) {
        const auto index =
            static_cast<std::size_t>((from / cell).numerator() / (from / cell).denominator());
        const Fraction cellStart = cell * Fraction(static_cast<std::int64_t>(index));
        const Fraction cellEnd = cellStart + cell;
        const auto& spelling = spellingOf(index);
        if (spelling && !spelling->tuplet.isNone()) {
            // Inside a tuplet cell: written units, each scaled by the tuplet.
            const Fraction end = std::min(to, cellEnd);
            Fraction written = (end - from) / spelling->unit * spelling->written;
            Fraction at = (from - cellStart) / spelling->unit * spelling->written;
            while (written > Fraction(0)) {
                const Plain* d = longestPlain(written, at, rest);
                if (d == nullptr) {
                    d = longestPlain(written, at, false);
                }
                if (d == nullptr) {
                    break; // finer than a 128th: cannot happen on the grids used
                }
                out.emplace_back(d->duration.value(), d->duration.dots(), spelling->tuplet);
                written -= d->length;
                at += d->length;
            }
            from = end;
            continue;
        }
        // Straight: stop at the next tuplet cell, and at the next beat when off the beat.
        Fraction limit = to;
        for (std::size_t next = index + 1; cell * Fraction(static_cast<std::int64_t>(next)) < limit;
             ++next) {
            if (const auto s = spellingOf(next); s && !s->tuplet.isNone()) {
                limit = cell * Fraction(static_cast<std::int64_t>(next));
                break;
            }
        }
        if (!isMultiple(from, beat)) {
            limit = std::min(limit, floorTo(from, beat) + beat);
        }
        const Plain* d = longestPlain(limit - from, from, rest);
        if (d == nullptr) {
            d = longestPlain(limit - from, from, false);
        }
        if (d == nullptr) {
            break; // finer than a 128th: cannot happen on the grids used
        }
        out.push_back(d->duration);
        from += d->length;
    }
    return out;
}

// ---- Voices -------------------------------------------------------------------

struct PlacedNote {
    Fraction start{};
    Fraction end{};
    int key = 0;
    int velocity = 0;
};

struct Chord {
    Fraction start{};
    Fraction end{};
    std::vector<std::pair<int, int>> notes{}; // key, velocity; highest first
};

using Voices = std::array<std::vector<Chord>, core::Measure::kMaxVoices>;

// Notes that start and end together form chords; each chord goes to the first
// voice that is free when it starts (higher chords first). With all voices busy,
// the voice that frees first is cut short.
Voices assignVoices(std::vector<PlacedNote> notes, bool& shortened) {
    std::ranges::stable_sort(notes, [](const PlacedNote& a, const PlacedNote& b) {
        return std::tie(a.start, b.key) < std::tie(b.start, a.key);
    });
    std::vector<Chord> chords;
    for (const PlacedNote& note : notes) {
        // Chords starting with this note are at the back.
        bool joined = false;
        for (auto c = chords.rbegin(); c != chords.rend() && c->start == note.start; ++c) {
            if (c->end == note.end) {
                c->notes.emplace_back(note.key, note.velocity);
                joined = true;
                break;
            }
        }
        if (!joined) {
            chords.push_back(
                Chord{.start = note.start, .end = note.end, .notes = {{note.key, note.velocity}}});
        }
    }
    Voices voices;
    for (Chord& chord : chords) {
        std::size_t target = voices.size();
        for (std::size_t v = 0; v < voices.size(); ++v) {
            if (voices[v].empty() || voices[v].back().end <= chord.start) {
                target = v;
                break;
            }
        }
        if (target == voices.size()) {
            target = 0;
            for (std::size_t v = 1; v < voices.size(); ++v) {
                if (voices[v].back().end < voices[target].back().end) {
                    target = v;
                }
            }
            Chord& last = voices[target].back();
            shortened = true;
            if (last.start < chord.start) {
                last.end = chord.start;
            } else { // same start: join the chord with its length
                for (const auto& note : chord.notes) {
                    last.notes.push_back(note);
                }
                continue;
            }
        }
        voices[target].push_back(std::move(chord));
    }
    return voices;
}

core::Dynamic dynamicFor(int velocity) {
    core::Dynamic best = core::Dynamic::MF;
    int bestDistance = 1000;
    for (int d = 0; d <= static_cast<int>(core::Dynamic::FFF); ++d) {
        const auto dynamic = static_cast<core::Dynamic>(d);
        const int distance = std::abs(core::dynamicVelocity(dynamic) - velocity);
        if (distance < bestDistance) {
            best = dynamic;
            bestDistance = distance;
        }
    }
    return best;
}

// ---- Loudness (ADR-0045) ----------------------------------------------------------

constexpr int kOnDynamic = 2;    // a velocity this close to a dynamic's was written from it
constexpr int kHoldDynamic = 12; // a measure keeps the dynamic before within this of it
constexpr int kAccentFrom = 20;  // above the measure's dynamic: an accent from here...
constexpr int kMarcatoFrom = 36; // ...a marcato from here
constexpr int kGhostFrom = 28;   // below it: a ghost note from here

// A note struck at `start` with `velocity`.
struct Strike {
    Fraction start{};
    int velocity = 0;
};

// How a staff's velocities are written.
struct StaffLoudness {
    bool written = true; // every velocity on a dynamic's: each note its nearest dynamic
    std::vector<std::optional<core::Dynamic>> measures{}; // played: the dynamic of each measure
};

struct Loudness {
    core::Dynamic dynamic = core::Dynamic::MF;
    core::Accent accent = core::Accent::None;
    bool ghost = false;
};

// The measure that `at` falls in.
std::size_t measureAt(const std::vector<MeasureInfo>& measures, const Fraction& at) {
    const auto after = std::ranges::upper_bound(measures, at, {}, &MeasureInfo::start);
    return after == measures.begin() ? 0 : static_cast<std::size_t>(after - measures.begin()) - 1;
}

// Velocities all within kOnDynamic of a dynamic's were written from dynamics
// (by Elara or another notation program) and stay one per note. Others were
// played: each measure takes the dynamic nearest its median velocity, or keeps
// the dynamic before when that is within kHoldDynamic of the median.
StaffLoudness staffLoudness(const std::vector<Strike>& strikes,
                            const std::vector<MeasureInfo>& measures) {
    StaffLoudness result;
    result.written = std::ranges::all_of(strikes, [](const Strike& strike) {
        return std::abs(core::dynamicVelocity(dynamicFor(strike.velocity)) - strike.velocity) <=
               kOnDynamic;
    });
    if (result.written) {
        return result;
    }
    std::vector<std::vector<int>> velocities(measures.size());
    for (const Strike& strike : strikes) {
        velocities[measureAt(measures, strike.start)].push_back(strike.velocity);
    }
    result.measures.resize(measures.size());
    std::optional<core::Dynamic> previous;
    for (std::size_t m = 0; m < measures.size(); ++m) {
        std::vector<int>& heard = velocities[m];
        if (heard.empty()) {
            continue;
        }
        std::ranges::sort(heard);
        const std::size_t middle = heard.size() / 2;
        const double median = heard.size() % 2 == 1
                                  ? static_cast<double>(heard[middle])
                                  : static_cast<double>(heard[middle - 1] + heard[middle]) / 2.0;
        core::Dynamic dynamic = dynamicFor(static_cast<int>(std::lround(median)));
        if (previous && std::abs(median - static_cast<double>(core::dynamicVelocity(*previous))) <=
                            static_cast<double>(kHoldDynamic)) {
            dynamic = *previous;
        }
        result.measures[m] = dynamic;
        previous = dynamic;
    }
    return result;
}

// How a note struck in measure m with `velocity` is written: played notes well
// above their measure's dynamic are accented (marcato when far above), well
// below it ghost notes.
Loudness loudnessOf(int velocity, const StaffLoudness& staff, std::size_t m) {
    if (staff.written || !staff.measures[m]) {
        return Loudness{.dynamic = dynamicFor(velocity)};
    }
    Loudness result{.dynamic = *staff.measures[m]};
    const int above = velocity - core::dynamicVelocity(result.dynamic);
    if (above >= kMarcatoFrom) {
        result.accent = core::Accent::Marcato;
    } else if (above >= kAccentFrom) {
        result.accent = core::Accent::Accent;
    } else if (above <= -kGhostFrom) {
        result.ghost = true;
    }
    return result;
}

// Writes one voice's chords into the staff's measures, with rests between them
// and ties where a chord is cut by a barline or a beat.
void writeVoice(const std::vector<Chord>& chords, std::size_t voiceIndex, bool percussion,
                const std::vector<MeasureInfo>& measures, const Grids& grids,
                const StaffLoudness& loudness, core::Staff& staff) {
    std::size_t next = 0;
    for (std::size_t m = 0; m < measures.size(); ++m) {
        const MeasureInfo& measure = measures[m];
        while (next < chords.size() && chords[next].end <= measure.start) {
            ++next;
        }
        if (next >= chords.size() || chords[next].start >= measure.end()) {
            continue; // nothing here: an empty voice (a measure rest)
        }
        std::vector<Fraction> points;
        for (std::size_t c = next; c < chords.size() && chords[c].start < measure.end(); ++c) {
            points.push_back(std::max(chords[c].start, measure.start) - measure.start);
            points.push_back(std::min(chords[c].end, measure.end()) - measure.start);
        }
        const std::vector<int> subdivisions = voiceSubdivisions(measure, points, grids);
        auto& beats = staff.measure(m).voices[voiceIndex].beats;
        Fraction position = measure.start;
        const auto rests = [&](const Fraction& until) {
            for (const Duration& d : spellRhythm(measure, subdivisions, position - measure.start,
                                                 until - measure.start, true)) {
                beats.push_back(Beat{.duration = d});
            }
            position = until;
        };
        for (std::size_t c = next; c < chords.size() && chords[c].start < measure.end(); ++c) {
            const Chord& chord = chords[c];
            const Fraction start = std::max(chord.start, measure.start);
            const Fraction end = std::min(chord.end, measure.end());
            if (start > position) {
                rests(start);
            }
            bool tied = chord.start < measure.start;
            const std::size_t struck = measureAt(measures, chord.start);
            for (const Duration& d : spellRhythm(measure, subdivisions, start - measure.start,
                                                 end - measure.start, false)) {
                Beat beat{.duration = d};
                for (const auto& [key, velocity] : chord.notes) {
                    Note note{.pitch = percussion ? Pitch::fromMidi(key) : measure.key.spell(key)};
                    note.effects.tiedFromPrevious = tied;
                    const Loudness heard = loudnessOf(velocity, loudness, struck);
                    note.effects.dynamic = heard.dynamic;
                    if (!tied) { // a tied note only sustains
                        note.effects.accent = heard.accent;
                        note.effects.ghost = heard.ghost;
                    }
                    beat.notes.push_back(note);
                }
                beats.push_back(std::move(beat));
                tied = true;
            }
            position = end;
        }
        if (position < measure.end()) {
            rests(measure.end());
        }
    }
}

// ---- Tracks -------------------------------------------------------------------

const core::InstrumentPreset& presetFor(core::InstrumentKind kind, int program) {
    using core::InstrumentKind;
    for (const core::InstrumentPreset& preset : core::instrumentPresets()) {
        if (preset.kind == kind && preset.midiProgram == program &&
            kind != InstrumentKind::Percussion) {
            return preset;
        }
    }
    switch (kind) {
    case InstrumentKind::Percussion:
        return *core::findInstrumentPreset("drum-kit");
    case InstrumentKind::Keyboard:
        return *core::findInstrumentPreset("piano");
    case InstrumentKind::Fretted:
        return *core::findInstrumentPreset(program >= 32 ? "electric-bass" : "electric-guitar");
    case InstrumentKind::Pitched:
        break;
    }
    return *core::findInstrumentPreset("voice");
}

bool isPedal(int key) {
    return key == 35 || key == 36 || key == 44; // bass drums and pedal hi-hat
}

// Drum hits last until the next hit of their voice, within their beat.
std::vector<PlacedNote> drumHits(std::vector<PlacedNote> hits,
                                 const std::vector<MeasureInfo>& measures) {
    std::ranges::stable_sort(hits, [](const PlacedNote& a, const PlacedNote& b) {
        return std::tie(a.start, a.key) < std::tie(b.start, b.key);
    });
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const auto measure =
            std::prev(std::ranges::upper_bound(measures, hits[i].start, {}, &MeasureInfo::start));
        const Fraction beat = measure->beat();
        const Fraction beatEnd =
            measure->start + floorTo(hits[i].start - measure->start, beat) + beat;
        Fraction end = std::min(beatEnd, measure->end());
        for (std::size_t j = i + 1; j < hits.size(); ++j) {
            if (hits[j].start > hits[i].start) {
                end = std::min(end, hits[j].start);
                break;
            }
        }
        hits[i].end = end;
    }
    return hits;
}

// The track a source becomes, without notes: name, instrument and staves.
core::Track trackFor(const Source& source) {
    const bool percussion = source.channel == detail::kPercussionChannel;
    const int program = source.program.value_or(0);
    const core::InstrumentKind kind = detail::kindForProgram(program, percussion);
    const core::InstrumentPreset& preset = presetFor(kind, program);

    std::string instrumentName = source.instrumentName;
    if (instrumentName.empty()) {
        instrumentName =
            percussion ? std::string(preset.name) : std::string(detail::programName(program));
    }
    const std::string name = source.trackName.empty() ? instrumentName : source.trackName;

    const core::Track shape = core::Track::fromPreset(preset);
    const bool matchingPreset = preset.kind == kind && preset.midiProgram == program;
    core::Instrument instrument{
        .name = instrumentName,
        .kind = kind,
        .midiProgram = program,
        .writtenOffset =
            matchingPreset || kind != core::InstrumentKind::Pitched ? preset.writtenOffset : 0};
    std::vector<core::Staff> staves;
    for (std::size_t s = 0; s < shape.staffCount(); ++s) {
        staves.push_back(shape.staff(s));
    }
    if (kind == core::InstrumentKind::Pitched && !matchingPreset) {
        // No preset: the clef that suits the notes.
        long long sum = 0;
        for (const RawNote& note : source.notes) {
            sum += note.key;
        }
        const bool low =
            !source.notes.empty() && sum < 60LL * static_cast<long long>(source.notes.size());
        staves = {core::Staff(low ? core::Clef::Bass : core::Clef::Treble)};
    }
    core::Track track(name, instrument, std::move(staves));
    core::TrackMix mix;
    if (source.volume) {
        mix.volume = static_cast<double>(*source.volume) / 127.0;
    }
    if (source.pan) {
        // 64 is the centre, 1 (or 0) hard left and 127 hard right.
        mix.pan = std::clamp(static_cast<double>(*source.pan - 64) / 63.0, -1.0, 1.0);
    }
    track.setMix(mix);
    return track;
}

// Writes a source's notes into its track, whose staves have the song's measures.
// Touches nothing else, so tracks can be written on several threads.
void writeNotes(core::Track& result, const Source& source, const std::vector<MeasureInfo>& measures,
                int division, Warnings& warnings) {
    if (measures.empty()) {
        return;
    }
    const bool percussion = source.channel == detail::kPercussionChannel;
    const std::string& name = result.name();

    const Fraction songEnd = measures.back().end();
    const std::size_t staffCount = result.staffCount();
    // Keyboards are split at middle C; each staff gets its own grids.
    std::vector<std::vector<RawNote>> raws(staffCount);
    for (const RawNote& raw : source.notes) {
        raws[staffCount == 2 && raw.key < 60 ? 1 : 0].push_back(raw);
    }
    bool shortened = false;
    for (std::size_t s = 0; s < staffCount; ++s) {
        Grids grids(measures, division);
        for (const RawNote& note : raws[s]) {
            grids.addPoint(note.start, 1.0);
            if (!percussion) {
                grids.addPoint(note.end, 0.0); // exact grids hold it; played grids ignore it
            }
        }
        grids.choose();
        if (percussion) {
            std::array<std::vector<PlacedNote>, 2> hands{}; // hands and feet
            for (const RawNote& raw : raws[s]) {
                const Fraction start = grids.snap(raw.start);
                if (start < songEnd) {
                    hands[isPedal(raw.key) ? 1 : 0].push_back(
                        {.start = start, .key = raw.key, .velocity = raw.velocity});
                }
            }
            std::vector<Strike> strikes;
            for (const auto& hand : hands) {
                for (const PlacedNote& hit : hand) {
                    strikes.push_back({.start = hit.start, .velocity = hit.velocity});
                }
            }
            const StaffLoudness loudness = staffLoudness(strikes, measures);
            for (std::size_t v = 0; v < hands.size(); ++v) {
                const Voices placed =
                    assignVoices(drumHits(std::move(hands[v]), measures), shortened);
                writeVoice(placed[0], v, true, measures, grids, loudness, result.staff(s));
            }
            continue;
        }
        std::vector<PlacedNote> notes;
        notes.reserve(raws[s].size());
        for (const RawNote& raw : raws[s]) {
            PlacedNote note{
                .start = grids.snap(raw.start), .key = raw.key, .velocity = raw.velocity};
            if (note.start >= songEnd) {
                continue;
            }
            note.end = std::min(grids.snap(raw.end), songEnd);
            if (note.end <= note.start) {
                note.end = std::min(note.start + grids.unitAt(note.start), songEnd);
            }
            notes.push_back(note);
        }
        std::vector<Strike> strikes;
        strikes.reserve(notes.size());
        for (const PlacedNote& note : notes) {
            strikes.push_back({.start = note.start, .velocity = note.velocity});
        }
        const StaffLoudness loudness = staffLoudness(strikes, measures);
        const Voices placed = assignVoices(std::move(notes), shortened);
        for (std::size_t v = 0; v < placed.size(); ++v) {
            writeVoice(placed[v], v, false, measures, grids, loudness, result.staff(s));
        }
    }
    if (shortened) {
        warnings.add(name, "notes overlapping more than four voices shortened");
    }
}

std::expected<ImportResult, FormatError> importParsed(const MidiFile& file, Warnings& warnings,
                                                      const MidiOptions& options) {
    const Gathered gathered = gather(file, warnings);
    const std::vector<MeasureInfo> measures = layMeasures(gathered, file.division, warnings);

    ImportResult result;
    Song& song = result.song;
    song.info().title = gathered.title;
    song.info().copyright = gathered.copyright;
    if (!gathered.tempos.empty() && gathered.tempos.front().tick == 0) {
        long long micros = gathered.tempos.front().value;
        for (const auto& tempo : gathered.tempos) {
            if (tempo.tick == 0) {
                micros = tempo.value; // the last at the start wins
            }
        }
        song.setStartTempo(core::Tempo(std::clamp(detail::bpmFromMicroseconds(micros),
                                                  core::Tempo::kMinBpm, core::Tempo::kMaxBpm)));
    }
    song.appendMeasures(measures.size());
    for (std::size_t m = 0; m < measures.size(); ++m) {
        core::MeasureHeader& header = song.measureHeader(m);
        header.timeSignature = measures[m].time;
        header.keySignature = measures[m].key;
        header.tempoChanges = measures[m].tempos;
    }
    std::size_t notes = 0;
    for (const Source& source : gathered.sources) {
        song.addTrack(trackFor(source));
        notes += source.notes.size();
    }

    // Each track's notes depend only on its source and the measures: written on
    // several threads into their own tracks, warnings merged in track order.
    const std::size_t sources = gathered.sources.size();
    std::vector<Warnings> trackWarnings(sources);
    const auto write = [&](std::size_t i) {
        writeNotes(song.track(i), gathered.sources[i], measures, file.division, trackWarnings[i]);
    };
    constexpr std::size_t kParallelNotes = 20'000; // measured on the benchmark song
    detail::forEachIndex(
        0, sources, detail::threadsFor(options.threads, notes, kParallelNotes, sources), write);
    for (const Warnings& track : trackWarnings) {
        warnings.append(track);
    }
    result.warnings = warnings.lines();
    return result;
}

} // namespace

std::expected<ImportResult, FormatError> importMidi(std::span<const std::uint8_t> bytes,
                                                    const MidiOptions& options) {
    Warnings warnings;
    const auto file = detail::readSmf(bytes, warnings);
    if (!file) {
        return std::unexpected(file.error());
    }
    return importParsed(*file, warnings, options);
}

std::expected<ImportResult, FormatError> importMidiFile(const std::filesystem::path& path,
                                                        const MidiOptions& options) {
    const auto bytes = detail::readFile(path);
    if (!bytes) {
        return std::unexpected(FormatError{std::format("cannot open {}", path.string())});
    }
    return importMidi(*bytes, options);
}

} // namespace elara::io
