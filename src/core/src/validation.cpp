// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>

namespace elara::core {

namespace {

constexpr std::array kPlainValues{
    NoteValue::DoubleWhole,  NoteValue::Whole,       NoteValue::Half,
    NoteValue::Quarter,      NoteValue::Eighth,      NoteValue::Sixteenth,
    NoteValue::ThirtySecond, NoteValue::SixtyFourth, NoteValue::HundredTwentyEighth,
};

bool isWhole(const Fraction& value) {
    return value.denominator() == 1;
}

Fraction ceilToMultiple(const Fraction& value, const Fraction& step) {
    const Fraction count = value / step;
    const std::int64_t whole =
        (count.numerator() + count.denominator() - 1) / count.denominator(); // value >= 0
    return step * Fraction(whole);
}

void appendRest(Voice& voice, const Duration& duration) {
    voice.beats.push_back(Beat{.duration = duration, .notes = {}});
}

// Appends the largest rests from `candidates` (longest first) that fit before
// `target` and start on a multiple of their own length counted from `origin`.
bool fillAligned(Voice& voice, Fraction& position, const Fraction& target, const Fraction& origin,
                 const std::vector<Duration>& candidates) {
    while (position < target) {
        const auto chosen = std::ranges::find_if(candidates, [&](const Duration& candidate) {
            const Fraction length = candidate.length();
            return position + length <= target && isWhole((position - origin) / length);
        });
        if (chosen == candidates.end()) {
            return false;
        }
        appendRest(voice, *chosen);
        position += chosen->length();
    }
    return true;
}

std::vector<Duration> plainDurations(Tuplet tuplet = {}) {
    std::vector<Duration> durations;
    for (const NoteValue value : kPlainValues) {
        durations.emplace_back(value, 0, tuplet);
    }
    return durations;
}

// The plain and dotted values that add up to `length`, longest first; empty
// when they cannot (off the 1/128 grid).
std::vector<Duration> partsOf(Fraction length) {
    // Dotted, then plain, value by value: 3/2, 1, 3/4, 1/2... longest first.
    static const std::vector<Duration> kCandidates = [] {
        std::vector<Duration> candidates;
        for (const NoteValue value : kPlainValues) {
            candidates.emplace_back(value, 1);
            candidates.emplace_back(value, 0);
        }
        return candidates;
    }();
    std::vector<Duration> parts;
    while (length > Fraction(0)) {
        const auto fits = std::ranges::find_if(
            kCandidates, [&](const Duration& candidate) { return candidate.length() <= length; });
        if (fits == kCandidates.end()) {
            return {};
        }
        parts.push_back(*fits);
        length -= fits->length();
    }
    return parts;
}

// Whether splitting the beat into tied parts keeps how it sounds and reads.
bool splits(const Beat& beat) {
    const BeatEffects& fx = beat.effects;
    return beat.duration.tuplet().isNone() && fx.fermata == Fermata::None &&
           fx.breath == Breath::None && fx.whammyBar.isEmpty() &&
           std::ranges::none_of(beat.notes, [](const Note& note) {
               return !note.effects.bend.isEmpty() || note.effects.slideOut != SlideOut::None;
           });
}

// A later part of `beat`: its notes tied on, with what spans beats.
Beat continuationOf(const Beat& beat) {
    const BeatEffects& fx = beat.effects;
    Beat part;
    part.effects.tremoloPicking = fx.tremoloPicking;
    part.effects.hairpin = fx.hairpin;
    part.effects.slur = fx.slur;
    part.effects.ottava = fx.ottava;
    part.effects.pedal = fx.pedal == Pedal::None ? Pedal::None : Pedal::Down;
    part.notes.reserve(beat.notes.size());
    for (const Note& note : beat.notes) {
        const NoteEffects& from = note.effects;
        NoteEffects tied;
        tied.harmonic = from.harmonic;
        tied.trill = from.trill;
        tied.tiedFromPrevious = true;
        tied.vibrato = from.vibrato;
        tied.palmMute = from.palmMute;
        tied.deadNote = from.deadNote;
        tied.ghost = from.ghost;
        tied.letRing = from.letRing;
        tied.dynamic = from.dynamic;
        part.notes.push_back(Note{.pitch = note.pitch, .position = note.position, .effects = tied});
    }
    return part;
}

// Splits beat `index`, `offset` after its start.
std::optional<BeatSplit> splitBeat(Voice& voice, std::size_t index, const Fraction& offset) {
    Beat& beat = voice.beats[index];
    if (!splits(beat)) {
        return std::nullopt;
    }
    const std::vector<Duration> head = partsOf(offset);
    const std::vector<Duration> tail = partsOf(beat.duration.length() - offset);
    if (head.empty() || tail.empty()) {
        return std::nullopt;
    }
    const Beat continuation = continuationOf(beat);
    std::vector<Beat> later;
    later.reserve(head.size() + tail.size() - 1);
    for (std::size_t p = 1; p < head.size() + tail.size(); ++p) {
        Beat& part = later.emplace_back(continuation);
        part.duration = p < head.size() ? head[p] : tail[p - head.size()];
    }
    beat.duration = head.front();
    voice.beats.insert(voice.beats.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                       std::make_move_iterator(later.begin()),
                       std::make_move_iterator(later.end()));
    return BeatSplit{.first = index, .at = index + head.size(), .added = later.size()};
}

// Dotted values that last a whole number of compound beats (6/8: 2., 4.).
std::vector<Duration> dottedBeatDurations(const Fraction& beat) {
    std::vector<Duration> durations;
    for (const NoteValue value : kPlainValues) {
        const Duration dotted(value, 1);
        if (isWhole(dotted.length() / beat)) {
            durations.push_back(dotted);
        }
    }
    return durations;
}

// The beat before `beat` in the same voice, looking into the previous measure.
const Beat* previousBeat(const Staff& staff, std::size_t measure, std::size_t voice,
                         std::size_t beat) {
    if (beat > 0) {
        return &staff.measure(measure).voices[voice].beats[beat - 1];
    }
    if (measure > 0) {
        const auto& beats = staff.measure(measure - 1).voices[voice].beats;
        return beats.empty() ? nullptr : &beats.back();
    }
    return nullptr;
}

bool continuesFrom(const Note& note, const Beat* previous) {
    if (previous == nullptr) {
        return false;
    }
    return std::ranges::any_of(previous->notes, [&note](const Note& candidate) {
        const bool samePitch = candidate.pitch.isEnharmonicWith(note.pitch);
        const bool sameString = !candidate.position || !note.position ||
                                candidate.position->string == note.position->string;
        return samePitch && sameString;
    });
}

void checkNotes(const Staff& staff, Issue where, const Beat& beat, const Beat* previous,
                std::vector<Issue>& issues) {
    const auto report = [&](IssueKind kind, std::size_t note) {
        Issue issue = where;
        issue.kind = kind;
        issue.note = note;
        issues.push_back(issue);
    };
    for (std::size_t n = 0; n < beat.notes.size(); ++n) {
        const Note& note = beat.notes[n];
        if (note.position) {
            const auto& tuning = staff.tuning();
            const int string = note.position->string;
            const int fret = note.position->fret;
            if (!tuning) {
                report(IssueKind::PositionWithoutTuning, n);
            } else if (string < 1 || string > tuning->stringCount()) {
                report(IssueKind::StringOutOfRange, n);
            } else if (fret < 0 || staff.capo() + fret > Tuning::kMaxFret) {
                report(IssueKind::FretOutOfRange, n);
            } else if (tuning->midiAt(string, fret, staff.capo()) != note.pitch.midiNumber()) {
                report(IssueKind::FretMismatch, n);
            }
            const bool reused = std::ranges::any_of(
                beat.notes.begin(), beat.notes.begin() + static_cast<std::ptrdiff_t>(n),
                [string](const Note& other) {
                    return other.position && other.position->string == string;
                });
            if (reused) {
                report(IssueKind::StringReused, n);
            }
        }
        if (note.effects.tiedFromPrevious && !continuesFrom(note, previous)) {
            report(IssueKind::TieWithoutPrevious, n);
        }
    }
}

} // namespace

VoiceFill voiceFill(const Voice& voice, const TimeSignature& time) {
    if (voice.isEmpty()) {
        return VoiceFill::Empty;
    }
    const Fraction length = voice.length();
    const Fraction measure = time.measureLength();
    if (length < measure) {
        return VoiceFill::Incomplete;
    }
    return length == measure ? VoiceFill::Complete : VoiceFill::Overfull;
}

bool fillWithRests(Voice& voice, const TimeSignature& time) {
    switch (voiceFill(voice, time)) {
    case VoiceFill::Empty:
    case VoiceFill::Complete:
        return true;
    case VoiceFill::Overfull:
        return false;
    case VoiceFill::Incomplete:
        break;
    }
    const Fraction end = time.measureLength();
    const Fraction beat = time.pulseLength();
    Fraction position = voice.length();

    // 1. Finish the current beat. Off the 1/128 grid means an unfinished tuplet:
    //    complete it with rests of the same tuplet.
    const Fraction nextBeat = ceilToMultiple(position, beat);
    const Fraction beatStart = nextBeat - beat;
    const Fraction beatEnd = std::min(nextBeat, end);
    const bool onGrid = isWhole(position * Fraction(128));
    const auto partial =
        onGrid ? plainDurations() : plainDurations(voice.beats.back().duration.tuplet());
    // 2. Whole beats, aligned to the start of the measure.
    const auto whole = time.isCompound() ? dottedBeatDurations(beat) : plainDurations();

    const std::size_t written = voice.beats.size();
    if (!fillAligned(voice, position, beatEnd, beatStart, partial) ||
        !fillAligned(voice, position, end, Fraction(0), whole)) {
        voice.beats.resize(written); // all or nothing
        return false;
    }
    return true;
}

std::optional<BeatSplit> splitBeatAt(Voice& voice, const Fraction& onset) {
    Fraction start;
    for (std::size_t b = 0; b < voice.beats.size(); ++b) {
        if (start == onset) {
            return BeatSplit{.first = b, .at = b, .added = 0};
        }
        const Fraction end = start + voice.beats[b].duration.length();
        if (onset > start && onset < end) {
            return splitBeat(voice, b, onset - start);
        }
        start = end;
    }
    return std::nullopt;
}

std::string_view issueName(IssueKind kind) noexcept {
    switch (kind) {
    case IssueKind::VoiceIncomplete:
        return "voice-incomplete";
    case IssueKind::VoiceOverfull:
        return "voice-overfull";
    case IssueKind::StringOutOfRange:
        return "string-out-of-range";
    case IssueKind::FretOutOfRange:
        return "fret-out-of-range";
    case IssueKind::FretMismatch:
        return "fret-mismatch";
    case IssueKind::StringReused:
        return "string-reused";
    case IssueKind::PositionWithoutTuning:
        return "position-without-tuning";
    case IssueKind::TieWithoutPrevious:
        return "tie-without-previous";
    }
    return "unknown";
}

std::vector<Issue> validate(const Song& song) {
    std::vector<Issue> issues;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const Track& track = song.track(t);
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            const Staff& staff = track.staff(s);
            for (std::size_t m = 0; m < staff.measureCount(); ++m) {
                const TimeSignature& time = song.measureHeader(m).timeSignature;
                for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                    const Voice& voice = staff.measure(m).voices[v];
                    const Issue where{.track = t, .staff = s, .measure = m, .voice = v};
                    const VoiceFill fill = voiceFill(voice, time);
                    if (fill == VoiceFill::Incomplete || fill == VoiceFill::Overfull) {
                        Issue issue = where;
                        issue.kind = fill == VoiceFill::Incomplete ? IssueKind::VoiceIncomplete
                                                                   : IssueKind::VoiceOverfull;
                        issues.push_back(issue);
                    }
                    for (std::size_t b = 0; b < voice.beats.size(); ++b) {
                        Issue atBeat = where;
                        atBeat.beat = b;
                        checkNotes(staff, atBeat, voice.beats[b], previousBeat(staff, m, v, b),
                                   issues);
                    }
                }
            }
        }
    }
    return issues;
}

} // namespace elara::core
