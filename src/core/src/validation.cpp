// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <algorithm>
#include <array>

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

bool isCompound(const TimeSignature& time) {
    return time.numerator() > 3 && time.numerator() % 3 == 0;
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
    const Fraction beat = isCompound(time) ? Fraction(3, time.denominator()) : time.beatLength();
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
    const auto whole = isCompound(time) ? dottedBeatDurations(beat) : plainDurations();

    const std::size_t written = voice.beats.size();
    if (!fillAligned(voice, position, beatEnd, beatStart, partial) ||
        !fillAligned(voice, position, end, Fraction(0), whole)) {
        voice.beats.resize(written); // all or nothing
        return false;
    }
    return true;
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
