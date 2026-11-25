// SPDX-License-Identifier: AGPL-3.0-or-later
// The sequencer (ADR-0017): positions are exact whole notes along the play
// order until they are turned into seconds, so notes that meet in the score
// meet in time.
#include <elara/audio/sequence.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elara::audio {

namespace {

using core::Fraction;

constexpr int kKeys = 128;
constexpr std::size_t kParallelNotes = 20'000;  // notes from which work goes on several threads
constexpr std::size_t kParallelEvents = 40'000; // events from which merges go on several threads
constexpr std::size_t kNotesPerBlock = 8192;    // written notes timed together on one thread
constexpr int kDynamicStep = 16;                // velocity between two dynamics (mf 80, f 96)
constexpr double kVibratoRate = 5.5;            // Hz
constexpr double kSlightVibrato = 20.0;         // cents either side
constexpr double kWideVibrato = 50.0;
constexpr double kSlideCents = 500.0; // slides in and off the string span five frets
constexpr double kSlideIn = 0.1;      // seconds a slide into a note takes at most
constexpr double kSlideOut = 0.2;     // seconds a slide out of a note takes at most
constexpr double kClick = 0.05;       // seconds a metronome click is held at most
constexpr double kStrumStep = 0.01;   // seconds between the strings of a strum
constexpr int kAccentClick = 100;     // velocity of the first click of a measure
constexpr int kClickVelocity = 80;

// The tempo of every written measure, times `speed`, as segments: from an
// offset (in whole notes, as a double) on, the speed in whole notes a second
// holds, or moves by a slope per whole note after a gradual change (ADR-0082).
// What a fermata holds runs its stretch times slower (ADR-0085). Exact offsets
// are converted once, so equal positions give equal seconds.
class TempoMap {
public:
    TempoMap(const core::Song& song, double speed) : speed_(speed) {
        // Every change along the song, with its slope: towards the next
        // change's speed when it is gradual.
        struct Change {
            std::size_t measure = 0;
            double offset = 0.0; // in the measure
            double at = 0.0;     // in the song
            double speed = 0.0;
            bool gradual = false;
            double slope = 0.0;
        };
        std::vector<Change> changes;
        std::vector<double> starts; // of the measures, in the song
        starts.reserve(song.measureCount());
        Fraction start;
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            starts.push_back(start.toDouble());
            for (const core::TempoChange& change : song.measureHeader(m).tempoChanges) {
                changes.push_back({.measure = m,
                                   .offset = change.offset.toDouble(),
                                   .at = (start + change.offset).toDouble(),
                                   .speed = speedOf(change.tempo),
                                   .gradual = change.gradual});
            }
            start += song.measureHeader(m).timeSignature.measureLength();
        }
        for (std::size_t c = 0; c < changes.size(); ++c) {
            Change& change = changes[c];
            if (change.gradual && c + 1 < changes.size() && changes[c + 1].at > change.at) {
                change.slope =
                    (changes[c + 1].speed - change.speed) / (changes[c + 1].at - change.at);
            }
        }

        first_.reserve(song.measureCount() + 1);
        double current = speedOf(song.startTempo()); // at `from`
        double slope = 0.0;
        double from = 0.0;
        std::size_t c = 0;
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            first_.push_back(segments_.size());
            segments_.push_back({.offset = 0.0,
                                 .seconds = 0.0,
                                 .speed = current + slope * (starts[m] - from),
                                 .slope = slope});
            for (; c < changes.size() && changes[c].measure == m; ++c) {
                const Segment& before = segments_.back();
                segments_.push_back(
                    {.offset = changes[c].offset,
                     .seconds =
                         before.seconds + core::rampSeconds(before.speed, before.slope,
                                                            changes[c].offset - before.offset),
                     .speed = changes[c].speed,
                     .slope = changes[c].slope});
                current = changes[c].speed;
                slope = changes[c].slope;
                from = changes[c].at;
            }
            hold(m, song.fermataHolds(m));
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
        return segment.seconds +
               core::rampSeconds(segment.speed, segment.slope, offset - segment.offset);
    }

    struct Segment {
        double offset = 0.0;
        double seconds = 0.0; // from the start of the measure
        double speed = 0.0;   // whole notes a second at `offset`
        double slope = 0.0;   // change of speed per whole note
        double stretch = 1.0; // of the fermatas holding it: the written speed is speed * stretch
    };

    // The tempo segments of measure m, in order.
    [[nodiscard]] std::span<const Segment> segments(std::size_t m) const noexcept {
        return std::span(segments_).subspan(first_[m], first_[m + 1] - first_[m]);
    }

private:
    [[nodiscard]] double speedOf(const core::Tempo& tempo) const {
        return tempo.wholesPerSecond() * speed_;
    }

    // Cuts the segments of measure m (the last ones) where its holds start and
    // end; a held stretch runs slower: speed and slope divided by its stretch.
    void hold(std::size_t m, const std::vector<core::FermataHold>& holds) {
        if (holds.empty()) {
            return;
        }
        const std::vector<Segment> written(
            segments_.begin() + static_cast<std::ptrdiff_t>(first_[m]), segments_.end());
        std::vector<double> cuts;
        for (const Segment& segment : written) {
            cuts.push_back(segment.offset);
        }
        for (const core::FermataHold& held : holds) {
            cuts.push_back(held.start.toDouble());
            cuts.push_back(held.end.toDouble());
        }
        std::ranges::sort(cuts);
        cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
        segments_.resize(first_[m]);
        std::size_t s = 0; // the written segment in force
        std::size_t h = 0; // the first hold not ended
        for (const double cut : cuts) {
            while (s + 1 < written.size() && written[s + 1].offset <= cut) {
                ++s;
            }
            while (h < holds.size() && holds[h].end.toDouble() <= cut) {
                ++h;
            }
            const double stretch =
                h < holds.size() && holds[h].start.toDouble() <= cut ? holds[h].stretch : 1.0;
            const Segment& at = written[s];
            Segment next{.offset = cut,
                         .seconds = 0.0,
                         .speed = (at.speed + at.slope * (cut - at.offset)) / stretch,
                         .slope = at.slope / stretch,
                         .stretch = stretch};
            if (segments_.size() > first_[m]) {
                const Segment& before = segments_.back();
                next.seconds = before.seconds +
                               core::rampSeconds(before.speed, before.slope, cut - before.offset);
            }
            segments_.push_back(next);
        }
    }

    double speed_ = 1.0;
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

// The note-on and note-off events of one track from its notes (`mine`,
// indices into `notes` in start order): a key struck while it sounds is
// released first, unless it was struck at that very time, and released when
// its last note ends (`ends`: a struck note's end, or that of the last note
// joined to it; empty when no note is joined). Notes that are not struck have
// no events. Releases at a time come before attacks. Ordered as events() are.
std::vector<NoteEvent> trackEvents(const std::vector<PlayedNote>& notes,
                                   std::span<const double> ends, std::span<const std::size_t> mine,
                                   std::size_t track) {
    struct Release {
        double time = 0.0;
        int key = 60;
    };
    std::vector<Release> releases;
    releases.reserve(mine.size());
    for (const std::size_t i : mine) {
        if (notes[i].struck && notes[i].key >= 0 && notes[i].key < kKeys) {
            releases.push_back(
                {.time = ends.empty() ? notes[i].end : ends[i], .key = notes[i].key});
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
        if (!note.struck || note.key < 0 || note.key >= kKeys) {
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
                          .touch = note.touch,
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

// How a written note is played (ADR-0022).
struct Articulation {
    int key = 60;
    int velocity = 80;
    Touch touch = Touch::Normal;
    bool staccato = false;
    bool staccatissimo = false; // ADR-0094
    bool tenuto = false;        // with staccato, portato (ADR-0088)
    bool letRing = false;
    bool moves = false; // a bend, whammy bar, slide, hammer-on or vibrato (ADR-0023)
    int string = 0;     // 0 when the note has no fret position
    int strum = 0;      // place in the order its chord is strummed (ADR-0025)
    Fraction tremolo{}; // whole notes between tremolo-picked attacks; 0 for none
    int trill = 0;      // semitones to a trill's other note, struck every `tremolo`
    // The semitones from the note of a mordent's or a turn's quick notes (ADR-0091).
    std::array<int, 3> ornament{};
    std::size_t ornamentNotes = 0;
    const core::NoteEffects* effects = nullptr;
    const core::BeatEffects* beat = nullptr;
};

// How long a note of written length `length` sounds: a quarter of it
// staccatissimo (ADR-0094), half of it staccato, three quarters portato
// (ADR-0088), all of it otherwise.
Fraction articulatedLength(const Fraction& length, const Articulation& articulation) {
    if (articulation.staccatissimo) {
        return length / Fraction(4);
    }
    if (!articulation.staccato) {
        return length;
    }
    return articulation.tenuto ? length * Fraction(3, 4) : length / Fraction(2);
}

// The same, less what a breath mark (a sixteenth) or a caesura (an eighth)
// after the note's beat takes off its end, never more than half of it
// (ADR-0095).
Fraction heldLength(const Fraction& length, const Articulation& articulation) {
    const Fraction held = articulatedLength(length, articulation);
    if (articulation.beat == nullptr || articulation.beat->breath == core::Breath::None) {
        return held;
    }
    const Fraction cut =
        articulation.beat->breath == core::Breath::Caesura ? Fraction(1, 8) : Fraction(1, 16);
    return std::max(held - cut, held / Fraction(2));
}

// The place of `note` in the order a strum strikes its beat: a downstroke
// from the lowest string up, an upstroke from the highest down. Without fret
// positions, pitch stands in for strings.
int strumPlace(const core::Beat& beat, const core::Note& note, core::Strum strum) {
    const auto lower = [](const core::Note& a, const core::Note& b) {
        if (a.position && b.position) {
            return a.position->string > b.position->string; // string 1 is the highest
        }
        return a.pitch.midiNumber() < b.pitch.midiNumber();
    };
    int place = 0;
    for (const core::Note& other : beat.notes) {
        if (strum == core::Strum::Down ? lower(other, note) : lower(note, other)) {
            ++place;
        }
    }
    return place;
}

// A point of a note's pitch line: seconds from its start, cents from its key.
struct PitchPoint {
    double time = 0.0;
    double cents = 0.0;
};

// When a note's pitch line starts and ends, in seconds.
struct Span {
    double start = 0.0;
    double end = 0.0;
};

// A bend curve at `fraction` of the note (0 to 1), interpolated.
double curveAt(const core::BendCurve& curve, double fraction) {
    const std::vector<core::BendPoint>& points = curve.points();
    if (points.empty()) {
        return 0.0;
    }
    const double position = fraction * 100.0;
    if (position <= points.front().position) {
        return points.front().cents;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (position <= points[i].position) {
            const core::BendPoint& a = points[i - 1];
            const core::BendPoint& b = points[i];
            return a.cents + ((b.cents - a.cents) * (position - a.position) /
                              static_cast<double>(b.position - a.position));
        }
    }
    return points.back().cents;
}

// A triangle wave of `depth` cents either side at kVibratoRate, rising first.
double vibratoAt(double depth, double time) {
    if (depth == 0.0) {
        return 0.0;
    }
    const double quarters = time * kVibratoRate * 4.0;
    const double whole = std::floor(quarters);
    constexpr std::array<double, 4> kCorners{0.0, 1.0, 0.0, -1.0};
    const auto corner = static_cast<std::size_t>(whole) % 4;
    const double from = kCorners[corner];
    const double to = kCorners[(corner + 1) % 4];
    return depth * (from + ((to - from) * (quarters - whole)));
}

// The pitch of a note over its `length` seconds as a line through `out`, in
// cents from its key; empty when it does not move. A slide out goes
// `slideOut` cents (none when 0) over the end of the note.
void pitchLine(const Articulation& articulation, double length, double slideOut,
               std::vector<double>& times, std::vector<PitchPoint>& out) {
    out.clear();
    if (length <= 0.0) {
        return;
    }
    const core::NoteEffects& effects = *articulation.effects;
    const core::BendCurve& whammy = articulation.beat->whammyBar;
    const double depth = effects.vibrato == core::Vibrato::Wide     ? kWideVibrato
                         : effects.vibrato == core::Vibrato::Slight ? kSlightVibrato
                                                                    : 0.0;
    const double slideIn = effects.slideIn == core::SlideIn::FromBelow   ? -kSlideCents
                           : effects.slideIn == core::SlideIn::FromAbove ? kSlideCents
                                                                         : 0.0;
    const double in = std::min(length / 2.0, kSlideIn);
    const double out0 = length - std::min(length / 2.0, kSlideOut);

    times.assign({0.0, length});
    for (const core::BendCurve* curve : {&effects.bend, &whammy}) {
        for (const core::BendPoint& point : curve->points()) {
            times.push_back(length * point.position / 100.0);
        }
    }
    if (slideIn != 0.0) {
        times.push_back(in);
    }
    if (slideOut != 0.0) {
        times.push_back(out0);
    }
    if (depth != 0.0) {
        const double quarter = 1.0 / (4.0 * kVibratoRate);
        for (double t = quarter; t < length; t += quarter) {
            times.push_back(t);
        }
    }
    std::ranges::sort(times);
    const auto [first, last] = std::ranges::unique(times);
    times.erase(first, last);

    bool moves = false;
    for (const double t : times) {
        double cents =
            curveAt(effects.bend, t / length) + curveAt(whammy, t / length) + vibratoAt(depth, t);
        if (t < in) {
            cents += slideIn * (1.0 - (t / in));
        }
        if (t > out0) {
            cents += slideOut * (t - out0) / (length - out0);
        }
        moves = moves || cents != 0.0;
        out.push_back({.time = t, .cents = cents});
    }
    if (!moves) {
        out.clear();
    }
}

// Keeps the part of `line` from `from` to `to` seconds, timed from `from`:
// each attack of a tremolo-picked note takes its part of the note's line.
void clipLine(std::vector<PitchPoint>& line, double from, double to,
              std::vector<PitchPoint>& part) {
    if (line.empty() || (from <= 0.0 && to >= line.back().time)) {
        return;
    }
    const auto at = [&](double t) { // straight between the points
        const auto next = std::ranges::upper_bound(line, t, {}, &PitchPoint::time);
        if (next == line.begin()) {
            return line.front().cents;
        }
        if (next == line.end()) {
            return line.back().cents;
        }
        const PitchPoint& before = *(next - 1);
        return before.cents +
               ((next->cents - before.cents) * (t - before.time) / (next->time - before.time));
    };
    part.clear();
    part.push_back({.time = 0.0, .cents = at(from)});
    for (const PitchPoint& point : line) {
        if (point.time > from && point.time < to) {
            part.push_back({.time = point.time - from, .cents = point.cents});
        }
    }
    if (to > from) {
        part.push_back({.time = to - from, .cents = at(to)});
    }
    line.swap(part);
}

// The metronome (ADR-0024): a click on every beat of every played measure,
// a high block on the first. A beat is the time signature's pulse, so a
// compound meter clicks its dotted beats. Each click is held kClick seconds,
// or until the next one when that comes sooner.
std::vector<NoteEvent> metronomeEvents(const core::Song& song,
                                       const std::vector<PlayedMeasure>& measures,
                                       const TempoMap& tempo, std::size_t track) {
    struct Click {
        double time = 0.0;
        bool accent = false;
    };
    std::vector<Click> clicks;
    for (const PlayedMeasure& measure : measures) {
        const core::TimeSignature& time = song.measureHeader(measure.measure).timeSignature;
        const Fraction pulse = time.pulseLength();
        const Fraction length = time.measureLength();
        bool first = true;
        for (Fraction offset; offset < length; offset += pulse) {
            clicks.push_back(
                {.time = measure.start + tempo.secondsInto(measure.measure, offset.toDouble()),
                 .accent = first});
            first = false;
        }
    }
    std::vector<NoteEvent> events;
    events.reserve(2 * clicks.size());
    for (std::size_t i = 0; i < clicks.size(); ++i) {
        const int key = clicks[i].accent ? kMetronomeAccentKey : kMetronomeKey;
        const double next = i + 1 < clicks.size() ? clicks[i + 1].time : clicks[i].time + kClick;
        events.push_back({.time = clicks[i].time,
                          .kind = EventKind::NoteOn,
                          .track = track,
                          .key = key,
                          .velocity = clicks[i].accent ? kAccentClick : kClickVelocity});
        events.push_back({.time = std::min(clicks[i].time + kClick, next),
                          .kind = EventKind::NoteOff,
                          .track = track,
                          .key = key,
                          .velocity = 0});
    }
    return events;
}

NoteEvent pitchEvent(double time, std::size_t track, int key, double cents, double glide) {
    return {.time = time,
            .kind = EventKind::Pitch,
            .cents = static_cast<std::int16_t>(std::clamp(std::lround(cents), -32767L, 32767L)),
            .glide = static_cast<float>(glide),
            .track = track,
            .key = key,
            .velocity = 0};
}

// A note joined to the voice struck as `key` (ADR-0070).
NoteEvent legatoEvent(double time, std::size_t track, int key, double cents, int velocity) {
    NoteEvent event = pitchEvent(time, track, key, cents, 0.0);
    event.kind = EventKind::Legato;
    event.velocity = velocity;
    return event;
}

// The key a harmonic sounds: a partial of the open string (natural) or of
// the fretted note (artificial, pinch, tapped). A semi-harmonic keeps the
// fretted pitch, and so does a node that is not one.
int harmonicKey(const core::Note& note, int key) {
    const core::Harmonic& harmonic = note.effects.harmonic;
    const std::optional<int> semitones =
        core::naturalHarmonicSemitones(static_cast<double>(harmonic.fretTenths) / 10.0);
    int sounding = key;
    switch (harmonic.kind) {
    case core::HarmonicKind::Natural:
        if (note.position && semitones) {
            sounding = key - note.position->fret + *semitones;
        }
        break;
    case core::HarmonicKind::Artificial:
    case core::HarmonicKind::Pinch:
    case core::HarmonicKind::Tapped:
        if (semitones) {
            sounding = key + *semitones;
        }
        break;
    case core::HarmonicKind::None:
    case core::HarmonicKind::Semi:
        break;
    }
    return sounding >= 0 && sounding < kKeys ? sounding : key;
}

Articulation articulationOf(const core::Song& song, const core::SoundingNote& sounding) {
    const core::Track& track = song.track(sounding.track);
    const core::Beat& beat = core::beatOf(song, sounding);
    const core::Note& note = core::noteOf(song, sounding);
    // A grace note (ADR-0079) has none of its beat's strum, tremolo or whammy.
    static const core::BeatEffects kGraceBeat{};
    const core::BeatEffects& beatEffects = sounding.grace == 0 ? beat.effects : kGraceBeat;
    const core::NoteEffects& effects = note.effects;
    Articulation result{.key = sounding.key,
                        .velocity = sounding.velocity,
                        .staccato = effects.staccato,
                        .staccatissimo = effects.staccatissimo,
                        .tenuto = effects.tenuto,
                        .letRing = effects.letRing,
                        .string = note.position ? note.position->string : 0,
                        .effects = &effects,
                        .beat = &beatEffects};
    if (effects.accent == core::Accent::Accent) {
        result.velocity += kDynamicStep;
    } else if (effects.accent == core::Accent::Marcato) {
        result.velocity += 2 * kDynamicStep;
    }
    if (effects.ghost) {
        result.velocity -= 2 * kDynamicStep;
    }
    result.velocity = std::clamp(result.velocity, 1, 127);
    if (beatEffects.tremoloPicking) {
        result.tremolo = core::Duration(*beatEffects.tremoloPicking).length();
    }
    if (track.instrument().kind == core::InstrumentKind::Percussion) {
        return result; // keys are drums: no partials, no touches, no strings to strum
    }
    if (beatEffects.strum != core::Strum::None) {
        result.strum = strumPlace(beat, note, beatEffects.strum);
    }
    // A trill alternates with its other note at its own speed (ADR-0090).
    if (effects.trill.isTrill()) {
        result.trill = effects.trill.semitones;
        result.tremolo = core::Duration(effects.trill.speed).length();
    } else if (const core::OrnamentSteps steps = core::ornamentSteps(effects.ornament);
               steps.count > 0) {
        // A mordent or a turn starts with quick notes on the steps of the key
        // next to it (ADR-0091).
        const core::KeySignature& key = song.measureHeader(sounding.measure).keySignature;
        const int up = core::semitonesToNextStep(note.pitch, key);
        const int down = core::semitonesToPreviousStep(note.pitch, key);
        for (std::size_t i = 0; i < steps.count; ++i) {
            result.ornament[i] = steps.steps[i] > 0 ? up : (steps.steps[i] < 0 ? -down : 0);
        }
        result.ornamentNotes = steps.count;
    }
    result.key = harmonicKey(note, sounding.key);
    result.moves = !effects.bend.isEmpty() || !beatEffects.whammyBar.isEmpty() ||
                   effects.slideIn != core::SlideIn::None ||
                   effects.slideOut != core::SlideOut::None || effects.legatoFromPrevious ||
                   effects.vibrato != core::Vibrato::None;
    if (effects.deadNote) {
        result.touch = Touch::Dead;
    } else if (effects.harmonic.kind != core::HarmonicKind::None) {
        result.touch = Touch::Harmonic;
    } else if (effects.palmMute) {
        result.touch = Touch::PalmMute;
    }
    return result;
}

// The sustain pedal (ADR-0092): a note whose sound reaches into a pedalling of
// its track rings until the pedal comes up; never shorter. A pedalling goes on
// into the next played measure only where that is the next written one: where
// a repeat or a jump goes back, the pedal comes up and goes down again.
void sustain(const core::Song& song, const TempoMap& tempo, std::span<const PlayedMeasure> played,
             std::vector<PlayedNote>& notes) {
    struct Pedalling {
        double start = 0.0;
        double end = 0.0;
    };
    const std::size_t measureCount = song.measureCount();
    const std::size_t trackCount = song.trackCount();
    std::vector<std::vector<Pedalling>> pedallings(trackCount);
    // Reading every beat for its pedal is most of the work, even for a song
    // without any: one track per core.
    const auto pedalsOf = [&](std::size_t t) {
        std::vector<std::vector<core::PedalSpan>> written; // by measure, when any
        for (std::size_t m = 0; m < measureCount; ++m) {
            if (std::vector<core::PedalSpan> spans = song.pedalSpans(t, m); !spans.empty()) {
                written.resize(measureCount);
                written[m] = std::move(spans);
            }
        }
        if (written.empty()) {
            return;
        }
        std::vector<Pedalling>& spans = pedallings[t];
        const PlayedMeasure* before = nullptr;
        for (const PlayedMeasure& measure : played) {
            const bool follows = before != nullptr && before->measure + 1 == measure.measure;
            for (const core::PedalSpan& span : written[measure.measure]) {
                const Pedalling pedalling{
                    .start =
                        measure.start + tempo.secondsInto(measure.measure, span.start.toDouble()),
                    .end = measure.start + tempo.secondsInto(measure.measure, span.end.toDouble())};
                constexpr double kMeet = 1e-9;
                if (!span.change && !spans.empty() && spans.back().end + kMeet >= pedalling.start &&
                    (follows || !span.start.isZero())) {
                    spans.back().end = std::max(spans.back().end, pedalling.end);
                } else {
                    spans.push_back(pedalling);
                }
            }
            before = &measure;
        }
    };
    core::forEachIndex(0, trackCount, core::threadsFor(0, notes.size(), kParallelNotes, trackCount),
                       pedalsOf);
    if (std::ranges::all_of(pedallings, [](const auto& spans) { return spans.empty(); })) {
        return;
    }
    for (PlayedNote& note : notes) {
        const std::vector<Pedalling>& spans = pedallings[note.track];
        // The last pedalling to go down before the note ends.
        const auto after = std::ranges::lower_bound(spans, note.end, {}, &Pedalling::start);
        if (after != spans.begin() && std::prev(after)->end > note.start) {
            note.end = std::max(note.end, std::prev(after)->end);
        }
    }
}

} // namespace

Sequence::Sequence(const core::Song& song, SequenceOptions options)
    : speed_(std::isnan(options.speed) ? 1.0 : std::clamp(options.speed, kMinSpeed, kMaxSpeed)),
      metronome_(options.metronome), metronomeTrack_(song.trackCount()) {
    const std::size_t count = song.measureCount();
    const TempoMap tempo(song, speed_);
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

    // Where every played measure's tempi start and how it is counted, for
    // count-ins.
    pulses_.reserve(measures_.size());
    tempi_.reserve(measures_.size());
    for (const PlayedMeasure& measure : measures_) {
        const core::TimeSignature& time = song.measureHeader(measure.measure).timeSignature;
        const Fraction beats = time.measureLength() / time.pulseLength();
        pulses_.push_back({.length = time.pulseLength().toDouble(),
                           .count = static_cast<int>((beats.numerator() + beats.denominator() - 1) /
                                                     beats.denominator())});
        // A count-in keeps the written tempo, also where a fermata holds.
        for (const TempoMap::Segment& segment : tempo.segments(measure.measure)) {
            tempi_.push_back({.time = measure.start + segment.seconds,
                              .perWhole = 1.0 / (segment.speed * segment.stretch)});
        }
    }

    // Every written note articulated and timed once in its measure.
    const std::vector<core::SoundingNote> sounding = core::soundingNotes(song);
    std::vector<Timing> timings(sounding.size());
    std::vector<Articulation> articulations(sounding.size());
    const std::size_t blocks = (sounding.size() + kNotesPerBlock - 1) / kNotesPerBlock;
    core::forEachIndex(0, blocks, core::threadsFor(0, sounding.size(), kParallelNotes, blocks),
                       [&](std::size_t block) {
                           const std::size_t last =
                               std::min(sounding.size(), (block + 1) * kNotesPerBlock);
                           for (std::size_t i = block * kNotesPerBlock; i < last; ++i) {
                               const core::SoundingNote& note = sounding[i];
                               articulations[i] = articulationOf(song, note);
                               const Fraction offset = note.start - written[note.measure];
                               const Fraction end =
                                   offset + heldLength(note.length, articulations[i]);
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
    std::vector<std::size_t> writtenAs; // the sounding note each played note is
    writtenAs.reserve(played);
    bool letRing = false;
    bool moves = false;
    bool reorder = false; // strums and tremolo picking move attacks out of order
    // Once a note is tremolo-picked, the span of every note's pitch line: the
    // whole written note, over all its attacks.
    bool tremolo = false;
    std::vector<Span> spans;
    for (std::size_t k = 0; k < measures_.size(); ++k) {
        const std::size_t m = measures_[k].measure;
        const double base = measures_[k].start;
        for (std::size_t i = firstOf[m]; i < firstOf[m + 1]; ++i) {
            const core::SoundingNote& note = sounding[i];
            const Timing& timing = timings[i];
            const Articulation& articulation = articulations[i];
            PlayedNote attack{.track = note.track,
                              .staff = note.staff,
                              .voice = note.voice,
                              .pass = k,
                              .start = base + timing.start,
                              .end = timing.crosses ? endAfter(k, timing.endOffset)
                                                    : base + timing.end,
                              .key = articulation.key,
                              .velocity = articulation.velocity,
                              .touch = articulation.touch};
            letRing = letRing || articulation.letRing;
            moves = moves || articulation.moves;
            // A strum delays each string, by at most half the note (ADR-0025).
            if (articulation.strum > 0) {
                attack.start +=
                    std::min(articulation.strum * kStrumStep, (attack.end - attack.start) / 2.0);
                reorder = true;
            }
            // Tremolo picking strikes the note again at its speed: as many
            // times as that value fits in the note, rounded, evenly. A trill
            // does the same, every other attack on its other note.
            std::int64_t repeats = 1;
            if (articulation.tremolo > Fraction(0)) {
                const Fraction fits = heldLength(note.length, articulation) / articulation.tremolo;
                repeats = std::max<std::int64_t>(1, (2 * fits.numerator() + fits.denominator()) /
                                                        (2 * fits.denominator()));
                reorder = reorder || repeats > 1;
            }
            // A mordent or a turn (ADR-0091): its quick notes, each a
            // thirty-second or an equal share of a shorter note, then the note.
            double quick = 0.0;
            if (repeats == 1 && articulation.ornamentNotes > 0) {
                const auto quickNotes = static_cast<std::int64_t>(articulation.ornamentNotes);
                repeats = quickNotes + 1;
                const Fraction share =
                    std::min(Fraction(1, 32) / heldLength(note.length, articulation),
                             Fraction(1, quickNotes + 1));
                quick = (attack.end - attack.start) * share.toDouble();
                reorder = true;
            }
            if (repeats > 1 && !tremolo) {
                tremolo = true;
                spans.reserve(notes_.capacity());
                for (const PlayedNote& before : notes_) {
                    spans.push_back({.start = before.start, .end = before.end});
                }
            }
            const Span span{.start = attack.start, .end = attack.end};
            const double step =
                quick > 0.0 ? quick : (span.end - span.start) / static_cast<double>(repeats);
            for (std::int64_t r = 0; r < repeats; ++r) {
                if (repeats > 1) {
                    attack.end = r + 1 == repeats ? span.end : attack.start + step;
                }
                if (articulation.trill != 0) {
                    attack.key = r % 2 == 0 ? articulation.key
                                            : std::clamp(articulation.key + articulation.trill, 0,
                                                         kKeys - 1);
                } else if (quick > 0.0) {
                    const auto at = static_cast<std::size_t>(r);
                    attack.key =
                        at < articulation.ornamentNotes
                            ? std::clamp(articulation.key + articulation.ornament[at], 0, kKeys - 1)
                            : articulation.key;
                }
                notes_.push_back(attack);
                writtenAs.push_back(i);
                if (tremolo) {
                    spans.push_back(span);
                }
                attack.start += step;
            }
        }
    }
    if (reorder) { // back in order: by start, then track, staff, voice and key
        std::vector<std::size_t> byStart(notes_.size());
        std::iota(byStart.begin(), byStart.end(), std::size_t{0});
        std::ranges::stable_sort(byStart, [&](std::size_t a, std::size_t b) {
            const PlayedNote& x = notes_[a];
            const PlayedNote& y = notes_[b];
            return std::tie(x.start, x.track, x.staff, x.voice, x.key) <
                   std::tie(y.start, y.track, y.staff, y.voice, y.key);
        });
        std::vector<PlayedNote> sortedNotes;
        std::vector<std::size_t> sortedWrittenAs;
        std::vector<Span> sortedSpans;
        sortedNotes.reserve(byStart.size());
        sortedWrittenAs.reserve(byStart.size());
        sortedSpans.reserve(spans.size());
        for (const std::size_t p : byStart) {
            sortedNotes.push_back(notes_[p]);
            sortedWrittenAs.push_back(writtenAs[p]);
            if (tremolo) {
                sortedSpans.push_back(spans[p]);
            }
        }
        notes_ = std::move(sortedNotes);
        writtenAs = std::move(sortedWrittenAs);
        spans = std::move(sortedSpans);
    }
    // Each note's written end, before let ring: the span of its pitch line.
    std::vector<double> writtenEnds;
    if (moves) {
        writtenEnds.reserve(notes_.size());
        for (const PlayedNote& note : notes_) {
            writtenEnds.push_back(note.end);
        }
    }

    // Let ring: a note rings on until its string (without a fret position,
    // its key) is struck again on its staff, or to the end; never shorter.
    if (letRing) {
        std::unordered_map<std::uint64_t, std::size_t> ringing;
        for (std::size_t p = 0; p < notes_.size(); ++p) {
            const PlayedNote& note = notes_[p];
            const Articulation& articulation = articulations[writtenAs[p]];
            const auto place = static_cast<std::uint64_t>(
                articulation.string > 0 ? articulation.string : kKeys + note.key);
            const std::uint64_t slot = (static_cast<std::uint64_t>(note.track) << 40U) |
                                       (static_cast<std::uint64_t>(note.staff) << 16U) | place;
            const auto found = ringing.find(slot);
            if (found != ringing.end()) {
                PlayedNote& rung = notes_[found->second];
                rung.end = std::max(rung.end, note.start);
                ringing.erase(found);
            }
            if (articulation.letRing) {
                ringing.emplace(slot, p);
            }
        }
        for (const auto& entry : ringing) {
            PlayedNote& rung = notes_[entry.second];
            rung.end = std::max(rung.end, duration_);
        }
    }

    sustain(song, tempo, measures_, notes_);

    // Hammer-ons, pull-offs and legato slides join a note to the previous
    // note on its string, which keeps sounding; shift and legato slides go
    // to the next note on the string (ADR-0023).
    // Without moving notes none is joined, slides go nowhere and every note
    // ends at its own end, so these stay empty.
    std::vector<std::size_t> head; // the struck note each note sounds on
    std::vector<int> slideTo;      // the key a slide out goes to
    std::vector<double> ends;      // a struck note's end, or its last joined note's
    if (moves) {
        head.resize(notes_.size());
        std::iota(head.begin(), head.end(), std::size_t{0});
        slideTo.assign(notes_.size(), -1);
        // The attack before each later attack of a tremolo-picked note.
        constexpr std::size_t kFirst = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> attackBefore(tremolo ? notes_.size() : 0, kFirst);
        std::unordered_map<std::uint64_t, std::size_t> previous;
        for (std::size_t p = 0; p < notes_.size(); ++p) {
            const Articulation& articulation = articulations[writtenAs[p]];
            if (articulation.string == 0 || articulation.effects == nullptr) {
                continue;
            }
            const PlayedNote& note = notes_[p];
            const std::uint64_t slot = (static_cast<std::uint64_t>(note.track) << 40U) |
                                       (static_cast<std::uint64_t>(note.staff) << 16U) |
                                       static_cast<std::uint64_t>(articulation.string);
            const auto [found, added] = previous.try_emplace(slot, p);
            if (added) {
                continue;
            }
            const std::size_t q = found->second;
            found->second = p;
            if (tremolo && writtenAs[q] == writtenAs[p] && notes_[q].pass == notes_[p].pass) {
                attackBefore[p] = q; // the same note struck again: not joined, not slid to
                continue;
            }
            const core::SlideOut slide = articulations[writtenAs[q]].effects->slideOut;
            if (slide == core::SlideOut::Shift || slide == core::SlideOut::Legato) {
                slideTo[q] = note.key;
            }
            const bool joined =
                articulation.effects->legatoFromPrevious || slide == core::SlideOut::Legato;
            if (joined && notes_[q].end >= note.start) {
                head[p] = head[q];
                notes_[p].struck = false;
            }
        }
        // A tremolo-picked note slides to the note after its last attack;
        // every attack follows that one line.
        for (std::size_t p = attackBefore.size(); p-- > 0;) {
            if (attackBefore[p] != kFirst) {
                slideTo[attackBefore[p]] = slideTo[p];
            }
        }
        ends.resize(notes_.size());
        for (std::size_t p = 0; p < notes_.size(); ++p) {
            ends[p] = notes_[p].end;
            ends[head[p]] = std::max(ends[head[p]], notes_[p].end);
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
            const auto mine =
                std::span(byTrack).subspan(trackStart[t], trackStart[t + 1] - trackStart[t]);
            std::vector<NoteEvent> events = trackEvents(notes_, ends, mine, t);
            if (!moves) {
                tracks[t] = std::move(events);
                return;
            }
            // Pitch events on the key each note sounds on: a joined note
            // jumps to its own pitch, then every note follows its line.
            const std::size_t noteEvents = events.size();
            std::vector<double> times;
            std::vector<PitchPoint> line;
            std::vector<PitchPoint> part;
            for (const std::size_t p : mine) {
                const PlayedNote& note = notes_[p];
                const int key = notes_[head[p]].key;
                const Articulation& articulation = articulations[writtenAs[p]];
                if (key < 0 || key >= kKeys || articulation.effects == nullptr ||
                    (!articulation.moves && note.struck && slideTo[p] < 0)) {
                    continue;
                }
                const core::SlideOut slide = articulation.effects->slideOut;
                const double slideOut = slide == core::SlideOut::Downwards ? -kSlideCents
                                        : slide == core::SlideOut::Upwards ? kSlideCents
                                        : slideTo[p] >= 0 ? 100.0 * (slideTo[p] - note.key)
                                                          : 0.0;
                const Span span =
                    tremolo ? spans[p] : Span{.start = note.start, .end = writtenEnds[p]};
                pitchLine(articulation, span.end - span.start, slideOut, times, line);
                clipLine(line, note.start - span.start, writtenEnds[p] - span.start, part);
                const double base = 100.0 * (note.key - key);
                if (!note.struck) {
                    events.push_back(legatoEvent(note.start, t, key,
                                                 base + (line.empty() ? 0.0 : line.front().cents),
                                                 note.velocity));
                } else if (!line.empty() && line.front().cents != 0.0) {
                    events.push_back(pitchEvent(note.start, t, key, line.front().cents, 0.0));
                }
                for (std::size_t i = 1; i < line.size(); ++i) {
                    if (line[i].cents != line[i - 1].cents) {
                        events.push_back(pitchEvent(note.start + line[i - 1].time, t, key,
                                                    base + line[i].cents,
                                                    line[i].time - line[i - 1].time));
                    }
                }
            }
            if (events.size() > noteEvents) {
                std::ranges::stable_sort(events, earlier);
            }
            tracks[t] = std::move(events);
        });
    if (metronome_) {
        tracks.push_back(metronomeEvents(song, measures_, tempo, metronomeTrack_));
    }
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

CountIn Sequence::countInAt(double seconds) const noexcept {
    if (measures_.empty()) {
        return {};
    }
    // The measure played at `seconds` (the last one that starts by then), and
    // the tempo that holds from there.
    const auto measure = std::ranges::upper_bound(measures_, seconds, {}, &PlayedMeasure::start);
    const std::size_t k = measure == measures_.begin()
                              ? 0
                              : static_cast<std::size_t>(measure - measures_.begin()) - 1;
    const auto tempo = std::ranges::upper_bound(tempi_, seconds, {}, &TempoPoint::time);
    const TempoPoint& point = tempo == tempi_.begin() ? tempi_.front() : *(tempo - 1);
    return {.clicks = pulses_[k].count, .interval = pulses_[k].length * point.perWhole};
}

std::size_t Sequence::eventAt(double seconds) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::lower_bound(events_, seconds, {}, &NoteEvent::time) - events_.begin());
}

std::optional<double> Sequence::secondsAt(std::size_t measure, double fraction) const noexcept {
    const auto played = std::ranges::find(measures_, measure, &PlayedMeasure::measure);
    if (played == measures_.end()) {
        return std::nullopt;
    }
    return played->start + std::clamp(fraction, 0.0, 1.0) * (played->end - played->start);
}

std::size_t Sequence::playedMeasureAt(double seconds) const noexcept {
    if (measures_.empty() || seconds > measures_.back().end) {
        return measures_.size();
    }
    // The first measure that ends after `seconds`, or the last one.
    const auto after = std::ranges::upper_bound(measures_, seconds, {}, &PlayedMeasure::end);
    return std::min(static_cast<std::size_t>(after - measures_.begin()), measures_.size() - 1);
}

double carriedPosition(const Sequence& from, double seconds, const Sequence& to) noexcept {
    const std::vector<PlayedMeasure>& before = from.measures();
    const std::vector<PlayedMeasure>& after = to.measures();
    if (before.empty() || after.empty() || before.size() != after.size() ||
        !std::ranges::equal(before, after, {}, &PlayedMeasure::measure, &PlayedMeasure::measure)) {
        return std::clamp(seconds, 0.0, to.duration());
    }
    if (seconds >= from.duration()) {
        return to.duration();
    }
    const std::size_t played = from.playedMeasureAt(std::max(seconds, 0.0));
    const PlayedMeasure& was = before[played];
    const PlayedMeasure& is = after[played];
    if (is.start == was.start && is.end == was.end) {
        return seconds; // the same timing: exactly the same time
    }
    const double length = was.end - was.start;
    const double share = length > 0.0 ? std::clamp((seconds - was.start) / length, 0.0, 1.0) : 0.0;
    return is.start + share * (is.end - is.start);
}

} // namespace elara::audio
