// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
#include <elara/core/transpose.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

const Voice& voiceAt(const Song& song, const Caret& caret) {
    return song.track(caret.track).staff(caret.staff).measure(caret.measure).voices.at(caret.voice);
}

bool isDrumStaff(const Staff& staff) {
    return staff.clef() == Clef::Percussion;
}

int stringCountOf(const Staff& staff) {
    if (staff.isFretted()) {
        return staff.tuning()->stringCount();
    }
    return isDrumStaff(staff) ? static_cast<int>(drumPieces().size()) : 1;
}

/// The General MIDI key of drum lane `lane` (1 is the top).
int laneKey(int lane) {
    return drumPieces()[static_cast<std::size_t>(lane - 1)].key;
}

/// Where a hit sits among the lanes; keys without a lane go after them.
std::size_t laneOf(const Note& note) {
    const auto pieces = drumPieces();
    const auto found = std::ranges::find(pieces, note.pitch.midiNumber(), &DrumPiece::key);
    return static_cast<std::size_t>(found - pieces.begin());
}

/// The furthest index a caret can have in its voice: the free slot of a voice
/// shorter than its measure, otherwise the last beat.
std::size_t lastIndex(const Song& song, const Caret& caret) {
    const Voice& voice = voiceAt(song, caret);
    const bool isShort =
        voice.length() < song.measureHeader(caret.measure).timeSignature.measureLength();
    return voice.beats.empty() || isShort ? voice.beats.size() : voice.beats.size() - 1;
}

/// The sign of measure `i` of a measure repeat of `measures` measures.
MeasureRepeat repeatKind(std::size_t measures, std::size_t i) {
    if (measures == 1) {
        return MeasureRepeat::One;
    }
    return i == 0 ? MeasureRepeat::TwoFirst : MeasureRepeat::TwoSecond;
}

/// The caret's measure as it is, to edit and put back.
Measure measureAt(const Song& song, const Caret& caret) {
    return song.track(caret.track).staff(caret.staff).measure(caret.measure);
}

Entry replace(const Caret& caret, Measure content, std::string name) {
    return Entry{.command = std::make_unique<ReplaceMeasure>(
                     caret.track, caret.staff, caret.measure, std::move(content), std::move(name)),
                 .caret = caret};
}

Entry unchanged(const Caret& caret) {
    return Entry{.command = nullptr, .caret = caret};
}

/// Nothing changed because nothing at the caret takes the edit (ADR-0123).
Entry missed(const Caret& caret) {
    return Entry{.command = nullptr, .caret = caret, .missed = true};
}

/// Where the caret's beat (or the free slot) starts in its measure.
Fraction caretStart(const Song& song, const Caret& caret) {
    const std::vector<Beat>& beats = voiceAt(song, caret).beats;
    Fraction start;
    for (std::size_t b = 0; b < caret.beat && b < beats.size(); ++b) {
        start += beats[b].duration.length();
    }
    return start;
}

/// The loudest dynamic of the notes struck in `notes`; tied notes only sustain.
std::optional<Dynamic> struckDynamic(const std::vector<Note>& notes) {
    std::optional<Dynamic> loudest;
    for (const Note& note : notes) {
        if (!note.effects.tiedFromPrevious && (!loudest || note.effects.dynamic > *loudest)) {
            loudest = note.effects.dynamic;
        }
    }
    return loudest;
}

/// The loudest struck dynamic at each start of measure m of `staff` with struck
/// notes, over every voice, from `from` on.
std::map<Fraction, Dynamic> struckStarts(const Staff& staff, std::size_t m, const Fraction& from) {
    std::map<Fraction, Dynamic> starts;
    for (const Voice& voice : staff.measure(m).voices) {
        Fraction start;
        for (const Beat& beat : voice.beats) {
            if (const std::optional<Dynamic> struck = struckDynamic(beat.notes);
                struck && !(start < from)) {
                const auto [it, added] = starts.try_emplace(start, *struck);
                if (!added && *struck > it->second) {
                    it->second = *struck;
                }
            }
            start += beat.duration.length();
        }
    }
    return starts;
}

bool isOnString(const Note& note, int string) {
    return note.position && note.position->string == string;
}

/// Steps from C0: C4 is 28, B4 34.
int diatonicIndex(const Pitch& pitch) {
    return pitch.octave() * 7 + static_cast<int>(pitch.step());
}

int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return quotient * divisor > value ? quotient - 1 : quotient;
}

/// The interval from the sound to the written note of an instrument written
/// `offset` semitones above its sound: a major second for 2, an octave for 12.
Interval writtenInterval(int offset) {
    const int octaves = floorDiv(offset, 12);
    const int chromatic = offset - 12 * octaves;
    return Interval{.steps = static_cast<int>(std::lround(chromatic * 7.0 / 12.0)) + 7 * octaves,
                    .semitones = offset};
}

Interval inverse(Interval interval) {
    return Interval{.steps = -interval.steps, .semitones = -interval.semitones};
}

/// The written note of the middle line of a staff with `clef`, as a step index.
int middleLine(Clef clef) {
    constexpr int kB4 = 34;
    constexpr int kD3 = 22;
    constexpr int kC4 = 28;
    constexpr int kA3 = 26;
    switch (clef) {
    case Clef::Bass:
    case Clef::Bass8vb:
        return kD3;
    case Clef::Alto:
        return kC4;
    case Clef::Tenor:
        return kA3;
    case Clef::Treble:
    case Clef::Treble8vb:
    case Clef::Percussion:
        break;
    }
    return kB4;
}

const Note& highest(const std::vector<Note>& notes) {
    return *std::ranges::max_element(notes, {},
                                     [](const Note& note) { return note.pitch.midiNumber(); });
}

/// The highest note of the last beat with notes before the caret's, in its
/// voice, looking back through earlier measures.
const Note* previousNote(const Song& song, const Caret& caret) {
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    for (std::size_t m = caret.measure + 1; m-- > 0;) {
        const std::vector<Beat>& beats = staff.measure(m).voices.at(caret.voice).beats;
        std::size_t end = m == caret.measure ? std::min(caret.beat, beats.size()) : beats.size();
        while (end-- > 0) {
            if (!beats[end].isRest()) {
                return &highest(beats[end].notes);
            }
        }
    }
    return nullptr;
}

/// Where a note of `pitch` goes on a fretted staff: the caret's string when it
/// reaches it, otherwise the nearest string that does, skipping `taken` strings.
FretPosition placeOnString(const Staff& staff, const Pitch& pitch, int caretString,
                           const std::vector<int>& taken) {
    std::optional<FretPosition> best;
    for (const FretPosition& position : staff.tuning()->positionsFor(pitch, staff.capo())) {
        if (std::ranges::find(taken, int{position.string}) != taken.end()) {
            continue;
        }
        const int distance = std::abs(position.string - caretString);
        if (!best || distance < std::abs(best->string - caretString)) {
            best = position;
        }
    }
    if (!best) {
        throw std::out_of_range("enterPitch: no string can play the note");
    }
    return *best;
}

/// The step index of a new note on `step`: above `top` within an octave for a
/// chord, otherwise at most three steps from `reference`.
int stepIndex(int wanted, int reference, bool above) {
    int delta = (((wanted - reference) % 7) + 7) % 7;
    if (above) {
        return reference + (delta == 0 ? 7 : delta);
    }
    if (delta > 3) {
        delta -= 7;
    }
    return reference + delta;
}

bool hasEffect(const NoteEffects& fx, NoteEffect effect) {
    switch (effect) {
    case NoteEffect::Tie:
        return fx.tiedFromPrevious;
    case NoteEffect::Legato:
        return fx.legatoFromPrevious;
    case NoteEffect::Slide:
        return fx.slideOut != SlideOut::None;
    case NoteEffect::Bend:
        return !fx.bend.isEmpty();
    case NoteEffect::Vibrato:
        return fx.vibrato != Vibrato::None;
    case NoteEffect::PalmMute:
        return fx.palmMute;
    case NoteEffect::DeadNote:
        return fx.deadNote;
    case NoteEffect::LetRing:
        return fx.letRing;
    case NoteEffect::Harmonic:
        return fx.harmonic.kind != HarmonicKind::None;
    case NoteEffect::Ghost:
        return fx.ghost;
    case NoteEffect::Accent:
        return fx.accent != Accent::None;
    case NoteEffect::Staccato:
        return fx.staccato;
    case NoteEffect::Tenuto:
        return fx.tenuto;
    case NoteEffect::Staccatissimo:
        return fx.staccatissimo;
    }
    return false;
}

/// The beat just before the caret's in its voice, across the barline; null at
/// the start of the song or after an empty measure.
const Beat* beatBefore(const Song& song, const Caret& caret) {
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    if (caret.beat > 0) {
        return &staff.measure(caret.measure).voices.at(caret.voice).beats.at(caret.beat - 1);
    }
    if (caret.measure == 0) {
        return nullptr;
    }
    const std::vector<Beat>& beats = staff.measure(caret.measure - 1).voices.at(caret.voice).beats;
    return beats.empty() ? nullptr : &beats.back();
}

/// The note of `previous` that a tie on `note` continues.
const Note* tiedTo(const Beat* previous, const Note& note, bool fretted) {
    if (previous == nullptr) {
        return nullptr;
    }
    const auto& notes = previous->notes;
    if (fretted) {
        const auto found = std::ranges::find_if(notes, [&](const Note& candidate) {
            return candidate.position && note.position &&
                   candidate.position->string == note.position->string;
        });
        return found == notes.end() ? nullptr : &*found;
    }
    const auto same = std::ranges::find_if(
        notes, [&](const Note& candidate) { return candidate.pitch.isEnharmonicWith(note.pitch); });
    if (same != notes.end()) {
        return &*same;
    }
    return notes.size() == 1 ? &notes.front() : nullptr;
}

// Whether toggleEffect reaches `note` with the caret on `string`: the note on
// that string on a fretted staff, the lane's hit on a percussion staff, every
// note elsewhere.
bool isEffectTarget(const Note& note, const Staff& staff, int string) {
    if (staff.isFretted()) {
        return isOnString(note, string);
    }
    return !isDrumStaff(staff) || note.pitch.midiNumber() == laneKey(string);
}

} // namespace

bool hasCaret(const Song& song) noexcept {
    return song.trackCount() > 0 && song.measureCount() > 0;
}

Caret clampCaret(const Song& song, Caret caret) {
    if (!hasCaret(song)) {
        throw std::invalid_argument("clampCaret: a song without tracks or measures has no caret");
    }
    caret.track = std::min(caret.track, song.trackCount() - 1);
    const Track& track = song.track(caret.track);
    caret.staff = std::min(caret.staff, track.staffCount() - 1);
    caret.measure = std::min(caret.measure, song.measureCount() - 1);
    caret.voice = std::min(caret.voice, Measure::kMaxVoices - 1);
    caret.beat = std::min(caret.beat, lastIndex(song, caret));
    caret.string = std::clamp(caret.string, 1, stringCountOf(track.staff(caret.staff)));
    return caret;
}

bool onFreeSlot(const Song& song, const Caret& caret) {
    return caret.beat >= voiceAt(song, caret).beats.size();
}

Caret moveCaret(const Song& song, const Caret& from, CaretMove move) {
    Caret caret = clampCaret(song, from);
    const bool hasNextMeasure = caret.measure + 1 < song.measureCount();
    switch (move) {
    case CaretMove::NextBeat:
        if (caret.beat < lastIndex(song, caret)) {
            ++caret.beat;
        } else if (hasNextMeasure) {
            ++caret.measure;
            caret.beat = 0;
        }
        break;
    case CaretMove::PreviousBeat:
        if (caret.beat > 0) {
            --caret.beat;
        } else if (caret.measure > 0) {
            --caret.measure;
            caret.beat = lastIndex(song, caret);
        }
        break;
    case CaretMove::NextMeasure:
        if (hasNextMeasure) {
            ++caret.measure;
            caret.beat = 0;
        }
        break;
    case CaretMove::PreviousMeasure:
        if (caret.beat == 0 && caret.measure > 0) {
            --caret.measure;
        }
        caret.beat = 0;
        break;
    case CaretMove::StringUp:
        caret.string = std::max(1, caret.string - 1);
        break;
    case CaretMove::StringDown:
        caret.string =
            std::min(stringCountOf(song.track(caret.track).staff(caret.staff)), caret.string + 1);
        break;
    }
    return caret;
}

Entry enterFret(const Song& song, const Caret& at, int fret, Duration duration) {
    const Caret caret = clampCaret(song, at);
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    if (!staff.isFretted()) {
        throw std::logic_error("enterFret: the staff has no tuning");
    }
    Note note = staff.frettedNote(caret.string, fret);
    note.pitch = song.measureHeader(caret.measure).keySignature.spell(note.pitch.midiNumber());
    note.effects.dynamic = dynamicAt(song, caret);

    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    if (caret.beat == beats.size()) {
        beats.push_back(Beat{.duration = duration, .notes = {std::move(note)}});
        return replace(caret, std::move(content), "Enter note");
    }
    std::vector<Note>& notes = beats[caret.beat].notes;
    const auto same = std::ranges::find_if(
        notes, [&](const Note& existing) { return isOnString(existing, caret.string); });
    if (same != notes.end()) {
        note.effects = same->effects;
        if (*same == note) {
            return unchanged(caret);
        }
        *same = std::move(note);
    } else {
        // Chords keep their notes from the highest string down.
        const auto lower = std::ranges::find_if(notes, [&](const Note& existing) {
            return existing.position && existing.position->string > caret.string;
        });
        notes.insert(lower, std::move(note));
    }
    return replace(caret, std::move(content), "Enter note");
}

Entry enterPitch(const Song& song, const Caret& at, Step step, Duration duration, PitchMode mode) {
    Caret caret = clampCaret(song, at);
    const Track& track = song.track(caret.track);
    const Staff& staff = track.staff(caret.staff);
    if (staff.clef() == Clef::Percussion || track.instrument().kind == InstrumentKind::Percussion) {
        throw std::logic_error("enterPitch: a percussion staff has no note names");
    }
    const Interval toWritten = writtenInterval(track.instrument().writtenOffset);
    const KeySignature writtenKey =
        transposed(song.measureHeader(caret.measure).keySignature, toWritten);
    const auto written = [&](const Note& note) {
        const std::optional<Pitch> pitch = transposed(note.pitch, toWritten);
        return diatonicIndex(pitch ? *pitch : note.pitch);
    };

    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    const bool onBeat = caret.beat < beats.size();
    const bool chord = mode == PitchMode::AddToChord && onBeat && !beats[caret.beat].isRest();

    int index = 0;
    if (chord) {
        index = stepIndex(static_cast<int>(step), written(highest(beats[caret.beat].notes)), true);
    } else {
        const Note* previous = previousNote(song, caret);
        index = stepIndex(static_cast<int>(step),
                          previous ? written(*previous) : middleLine(staff.clefAt(caret.measure)),
                          false);
    }
    const int octave = floorDiv(index, 7);
    const int alter = writtenKey.alterFor(step);
    const int midi = (octave + 1) * 12 + Pitch(step, 0, 4).midiNumber() - 60 + alter;
    if (midi < 0 || midi > 127) {
        throw std::out_of_range("enterPitch: the note leaves the MIDI range");
    }
    const std::optional<Pitch> sounding =
        transposed(Pitch(step, alter, octave), inverse(toWritten));
    if (!sounding) {
        throw std::out_of_range("enterPitch: the note leaves the MIDI range");
    }

    Note note{.pitch = *sounding};
    note.effects.dynamic = dynamicAt(song, caret);
    if (staff.isFretted()) {
        std::vector<int> taken;
        if (chord) {
            for (const Note& existing : beats[caret.beat].notes) {
                if (existing.position) {
                    taken.push_back(existing.position->string);
                }
            }
        }
        note.position = placeOnString(staff, note.pitch, caret.string, taken);
        caret.string = note.position->string;
    }

    if (!onBeat) {
        beats.push_back(Beat{.duration = duration, .notes = {std::move(note)}});
        return replace(caret, std::move(content), "Enter note");
    }
    std::vector<Note>& notes = beats[caret.beat].notes;
    if (!chord) {
        if (notes.size() == 1 && notes.front() == note) {
            return unchanged(caret);
        }
        notes = {std::move(note)};
    } else if (note.position) {
        // Chords keep their notes from the highest string down.
        const int string = note.position->string;
        const auto lower = std::ranges::find_if(notes, [&](const Note& existing) {
            return existing.position && existing.position->string > string;
        });
        notes.insert(lower, std::move(note));
    } else {
        notes.insert(notes.begin(), std::move(note)); // the highest note first
    }
    return replace(caret, std::move(content), "Enter note");
}

Entry shiftPitch(const Song& song, const Caret& at, int semitones) {
    const Caret caret = clampCaret(song, at);
    if (semitones == 0 || onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const KeySignature& key = song.measureHeader(caret.measure).keySignature;
    Measure content = measureAt(song, caret);
    std::vector<Note>& notes = content.voices.at(caret.voice).beats[caret.beat].notes;
    bool changed = false;
    for (Note& note : notes) {
        if (staff.isFretted()) {
            if (!isOnString(note, caret.string)) {
                continue;
            }
            const int fret = note.position->fret + semitones;
            if (fret < 0 || staff.capo() + fret > Tuning::kMaxFret) {
                throw std::out_of_range("shiftPitch: the note leaves the neck");
            }
            const Note moved = staff.frettedNote(caret.string, fret);
            note.pitch = key.spell(moved.pitch.midiNumber());
            note.position = moved.position;
        } else {
            const int midi = note.pitch.midiNumber() + semitones;
            if (midi < 0 || midi > 127) {
                throw std::out_of_range("shiftPitch: the note leaves the MIDI range");
            }
            note.pitch = key.spell(midi);
        }
        changed = true;
    }
    if (!changed) {
        return unchanged(caret);
    }
    return replace(caret, std::move(content), "Change pitch");
}

Entry setBeatDuration(const Song& song, const Caret& at, Duration duration) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    if (beat.duration == duration) {
        return unchanged(caret);
    }
    beat.duration = duration;
    return replace(caret, std::move(content), "Change duration");
}

Entry deleteAtCaret(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    const auto beat = beats.begin() + static_cast<std::ptrdiff_t>(caret.beat);
    if (beat->isRest()) {
        beats.erase(beat);
        return replace(caret, std::move(content), "Delete");
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const bool drums = isDrumStaff(staff);
    const bool fretted = staff.isFretted();
    const auto removed = std::ranges::remove_if(beat->notes, [&](const Note& note) {
        if (drums) {
            return note.pitch.midiNumber() == laneKey(caret.string);
        }
        return !fretted || isOnString(note, caret.string);
    });
    if (removed.empty()) {
        return unchanged(caret);
    }
    beat->notes.erase(removed.begin(), removed.end());
    return replace(caret, std::move(content), "Delete");
}

Entry deleteBeat(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    beats.erase(beats.begin() + static_cast<std::ptrdiff_t>(caret.beat));
    return replace(caret, std::move(content), "Delete beat");
}

Entry insertRest(const Song& song, const Caret& at, Duration duration) {
    const Caret caret = clampCaret(song, at);
    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    beats.insert(beats.begin() + static_cast<std::ptrdiff_t>(caret.beat),
                 Beat{.duration = duration});
    return replace(caret, std::move(content), "Insert rest");
}

Entry toggleDrum(const Song& song, const Caret& at, Duration duration) {
    const Caret caret = clampCaret(song, at);
    if (!isDrumStaff(song.track(caret.track).staff(caret.staff))) {
        throw std::logic_error("toggleDrum: the staff is not a percussion staff");
    }
    const int key = laneKey(caret.string);
    Note hit{.pitch = Pitch::fromMidi(key)};
    hit.effects.dynamic = dynamicAt(song, caret);
    Measure content = measureAt(song, caret);
    std::vector<Beat>& beats = content.voices.at(caret.voice).beats;
    if (caret.beat == beats.size()) {
        beats.push_back(Beat{.duration = duration, .notes = {std::move(hit)}});
        return replace(caret, std::move(content), "Enter note");
    }
    std::vector<Note>& notes = beats[caret.beat].notes;
    const auto same = std::ranges::find_if(
        notes, [key](const Note& note) { return note.pitch.midiNumber() == key; });
    if (same != notes.end()) {
        notes.erase(same);
        return replace(caret, std::move(content), "Delete");
    }
    const std::size_t lane = laneOf(hit);
    const auto below =
        std::ranges::find_if(notes, [lane](const Note& note) { return laneOf(note) > lane; });
    notes.insert(below, std::move(hit));
    return replace(caret, std::move(content), "Enter note");
}

Entry toggleEffect(const Song& song, const Caret& at, NoteEffect effect) {
    constexpr int kFullStep = 200; // cents
    constexpr int kTenths = 10;
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const bool fretted = staff.isFretted();
    Measure content = measureAt(song, caret);
    std::vector<Note*> targets;
    for (Note& note : content.voices.at(caret.voice).beats[caret.beat].notes) {
        if (isEffectTarget(note, staff, caret.string)) {
            targets.push_back(&note);
        }
    }
    if (targets.empty()) {
        return unchanged(caret);
    }
    const bool on = std::ranges::any_of(
        targets, [effect](const Note* note) { return !hasEffect(note->effects, effect); });
    if (on && !fretted && needsStrings(effect)) {
        throw std::logic_error("toggleEffect: a technique of strings on a staff without them");
    }
    const Beat* previous = effect == NoteEffect::Tie && on ? beatBefore(song, caret) : nullptr;
    for (Note* note : targets) {
        NoteEffects& fx = note->effects;
        switch (effect) {
        case NoteEffect::Tie:
            if (on) {
                const Note* from = tiedTo(previous, *note, fretted);
                if (from == nullptr) {
                    throw std::logic_error("toggleEffect: no note to tie to");
                }
                note->pitch = from->pitch;
                note->position = from->position;
            }
            fx.tiedFromPrevious = on;
            break;
        case NoteEffect::Legato:
            fx.legatoFromPrevious = on;
            break;
        case NoteEffect::Slide:
            fx.slideOut = on ? SlideOut::Shift : SlideOut::None;
            break;
        case NoteEffect::Bend:
            fx.bend = on ? BendCurve::bend(kFullStep) : BendCurve{};
            break;
        case NoteEffect::Vibrato:
            fx.vibrato = on ? Vibrato::Slight : Vibrato::None;
            break;
        case NoteEffect::PalmMute:
            fx.palmMute = on;
            break;
        case NoteEffect::DeadNote:
            fx.deadNote = on;
            break;
        case NoteEffect::LetRing:
            fx.letRing = on;
            break;
        case NoteEffect::Harmonic:
            fx.harmonic = Harmonic{};
            if (on) {
                const int fret = note->position->fret;
                if (!naturalHarmonicSemitones(fret)) {
                    throw std::out_of_range("toggleEffect: no natural harmonic at that fret");
                }
                fx.harmonic = Harmonic{.kind = HarmonicKind::Natural,
                                       .fretTenths = static_cast<std::int16_t>(fret * kTenths)};
            }
            break;
        case NoteEffect::Ghost:
            fx.ghost = on;
            break;
        case NoteEffect::Accent:
            fx.accent = on ? Accent::Accent : Accent::None;
            break;
        case NoteEffect::Staccato:
            fx.staccato = on;
            fx.staccatissimo = fx.staccatissimo && !on; // one takes the other's place
            break;
        case NoteEffect::Tenuto:
            fx.tenuto = on;
            break;
        case NoteEffect::Staccatissimo:
            fx.staccatissimo = on;
            fx.staccato = fx.staccato && !on;
            break;
        }
    }
    return replace(caret, std::move(content), "Change effect");
}

namespace {

// Changes the effects of the notes the caret is on (as for toggleEffect) with
// `change`, as one undo step named `name`; unchanged when no note changes,
// missed when there is none.
template <typename Change>
Entry changeEffects(const Song& song, const Caret& at, std::string name, Change change) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return missed(caret);
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    Measure content = measureAt(song, caret);
    bool found = false;
    bool changed = false;
    for (Note& note : content.voices.at(caret.voice).beats[caret.beat].notes) {
        if (isEffectTarget(note, staff, caret.string)) {
            const NoteEffects before = note.effects;
            change(note);
            found = true;
            changed = changed || note.effects != before;
        }
    }
    if (!changed) {
        return found ? unchanged(caret) : missed(caret);
    }
    return replace(caret, std::move(content), std::move(name));
}

// What a rest keeps of `effects`: a hairpin, a fermata, a slur, an octave line
// and the pedal going on over it (ADR-0078, ADR-0085, ADR-0086, ADR-0089,
// ADR-0092).
BeatEffects restEffects(const BeatEffects& effects) {
    return BeatEffects{.hairpin = effects.hairpin,
                       .fermata = effects.fermata,
                       .slur = effects.slur,
                       .ottava = effects.ottava,
                       .pedal = effects.pedal};
}

} // namespace

Entry setBend(const Song& song, const Caret& at, const BendCurve& curve) {
    return changeEffects(song, at, "Change bend",
                         [&curve](Note& note) { note.effects.bend = curve; });
}

BendCurve bendAtCaret(const Song& song, const Caret& at) {
    return noteEffectsAt(song, at).bend;
}

NoteEffects noteEffectsAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return {};
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    for (const Note& note : voiceAt(song, caret).beats[caret.beat].notes) {
        if (isEffectTarget(note, staff, caret.string)) {
            return note.effects;
        }
    }
    return {};
}

Entry setSlides(const Song& song, const Caret& at, SlideIn in, SlideOut out) {
    return changeEffects(song, at, "Change slide", [in, out](Note& note) {
        note.effects.slideIn = in;
        note.effects.slideOut = out;
    });
}

Entry setSlideIn(const Song& song, const Caret& at, SlideIn in) {
    return changeEffects(song, at, "Change slide", [in](Note& note) { note.effects.slideIn = in; });
}

Entry setSlideOut(const Song& song, const Caret& at, SlideOut out) {
    return changeEffects(song, at, "Change slide",
                         [out](Note& note) { note.effects.slideOut = out; });
}

Entry setVibrato(const Song& song, const Caret& at, Vibrato vibrato) {
    return changeEffects(song, at, "Change vibrato",
                         [vibrato](Note& note) { note.effects.vibrato = vibrato; });
}

Entry setAccent(const Song& song, const Caret& at, Accent accent) {
    return changeEffects(song, at, "Change accent",
                         [accent](Note& note) { note.effects.accent = accent; });
}

Entry setTrill(const Song& song, const Caret& at, Trill trill) {
    if (std::abs(trill.semitones) > Trill::kMaxSemitones) {
        throw std::out_of_range("setTrill: a trill goes at most an octave away");
    }
    return changeEffects(song, at, "Change trill",
                         [trill](Note& note) { note.effects.trill = trill; });
}

Entry setTrillToNextStep(const Song& song, const Caret& at, NoteValue speed) {
    const Caret caret = clampCaret(song, at);
    const KeySignature& key = song.measureHeader(caret.measure).keySignature;
    return changeEffects(song, caret, "Change trill", [&key, speed](Note& note) {
        note.effects.trill =
            Trill{.semitones = static_cast<std::int8_t>(semitonesToNextStep(note.pitch, key)),
                  .speed = speed};
    });
}

Entry setOrnament(const Song& song, const Caret& at, Ornament ornament) {
    return changeEffects(song, at, "Change ornament",
                         [ornament](Note& note) { note.effects.ornament = ornament; });
}

Entry setFingering(const Song& song, const Caret& at, std::span<const Finger> fingers) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return missed(caret);
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    Measure content = measureAt(song, caret);
    std::vector<Note*> targets;
    for (Note& note : content.voices.at(caret.voice).beats[caret.beat].notes) {
        if (isEffectTarget(note, staff, caret.string)) {
            targets.push_back(&note);
        }
    }
    if (fingers.empty() || (fingers.size() != 1 && fingers.size() != targets.size())) {
        throw std::invalid_argument("setFingering: one finger for each note, or one for all");
    }
    std::ranges::stable_sort(targets, {},
                             [](const Note* note) { return note->pitch.midiNumber(); });
    bool changed = false;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Finger finger = fingers[fingers.size() == 1 ? 0 : i];
        changed = changed || targets[i]->effects.fingering != finger;
        targets[i]->effects.fingering = finger;
    }
    if (!changed) {
        return targets.empty() ? missed(caret) : unchanged(caret);
    }
    return replace(caret, std::move(content), "Change fingering");
}

Entry setPluck(const Song& song, const Caret& at, Finger finger) {
    const Caret caret = clampCaret(song, at);
    if (!song.track(caret.track).staff(caret.staff).isFretted()) {
        throw std::logic_error("setPluck: a plucking finger needs strings");
    }
    return changeEffects(song, caret, "Change plucking finger",
                         [finger](Note& note) { note.effects.pluck = finger; });
}

Entry setHarmonic(const Song& song, const Caret& at, HarmonicKind kind) {
    constexpr std::int16_t kOctave = 120; // tenths of a fret above the fretted note
    constexpr int kTenths = 10;
    const Caret caret = clampCaret(song, at);
    if (kind != HarmonicKind::None && !song.track(caret.track).staff(caret.staff).isFretted()) {
        throw std::logic_error("setHarmonic: a harmonic needs strings");
    }
    return changeEffects(song, caret, "Change harmonic", [kind](Note& note) {
        Harmonic harmonic{.kind = kind,
                          .fretTenths =
                              static_cast<std::int16_t>(kind == HarmonicKind::None ? 0 : kOctave)};
        if (kind == HarmonicKind::Natural) {
            const int fret = note.position ? note.position->fret : -1;
            if (!naturalHarmonicSemitones(fret)) {
                throw std::out_of_range("setHarmonic: no natural harmonic at that fret");
            }
            harmonic.fretTenths = static_cast<std::int16_t>(fret * kTenths);
        }
        note.effects.harmonic = harmonic;
    });
}

Entry setBeatEffects(const Song& song, const Caret& at, const BeatEffects& effects) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return missed(caret);
    }
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    const bool rest = beat.notes.empty();
    BeatEffects changed = effects;
    if (rest) {
        // A rest keeps only what restEffects() does, its other effects as they
        // were.
        const BeatEffects kept = restEffects(effects);
        changed = beat.effects;
        changed.hairpin = kept.hairpin;
        changed.fermata = kept.fermata;
        changed.slur = kept.slur;
        changed.ottava = kept.ottava;
        changed.pedal = kept.pedal;
    }
    if (beat.effects == changed) {
        return rest && restEffects(effects) != effects ? missed(caret) : unchanged(caret);
    }
    beat.effects = changed;
    return replace(caret, std::move(content), "Change beat effects");
}

BeatEffects beatEffectsAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return {};
    }
    const Beat& beat = voiceAt(song, caret).beats[caret.beat];
    return beat.notes.empty() ? restEffects(beat.effects) : beat.effects;
}

Entry addGraceNote(const Song& song, const Caret& at, int semitones, Duration duration,
                   bool onBeat) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const KeySignature& key = song.measureHeader(caret.measure).keySignature;
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    const auto target = std::ranges::find_if(
        beat.notes, [&](const Note& note) { return isEffectTarget(note, staff, caret.string); });
    if (target == beat.notes.end()) {
        return unchanged(caret);
    }
    Note grace;
    if (isDrumStaff(staff)) {
        grace.pitch = target->pitch; // a flam
        grace.effects.dynamic = target->effects.dynamic;
    } else if (staff.isFretted()) {
        const int fret = target->position->fret + semitones;
        if (fret < 0 || staff.capo() + fret > Tuning::kMaxFret) {
            throw std::out_of_range("addGraceNote: the grace note leaves the neck");
        }
        grace = staff.frettedNote(caret.string, fret);
        grace.pitch = key.spell(grace.pitch.midiNumber());
        grace.effects.dynamic = target->effects.dynamic;
    } else {
        const Note& top = highest(beat.notes);
        const int midi = top.pitch.midiNumber() + semitones;
        if (midi < 0 || midi > 127) {
            throw std::out_of_range("addGraceNote: the grace note leaves the MIDI range");
        }
        grace.pitch = key.spell(midi);
        grace.effects.dynamic = top.effects.dynamic;
    }
    beat.graces.push_back(
        Grace{.duration = duration, .notes = {std::move(grace)}, .onBeat = onBeat});
    return replace(caret, std::move(content), "Add grace note");
}

Entry removeGraceNotes(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    if (beat.graces.empty()) {
        return unchanged(caret);
    }
    beat.graces.clear();
    return replace(caret, std::move(content), "Remove grace notes");
}

std::size_t graceNotesAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    return onFreeSlot(song, caret) ? 0 : voiceAt(song, caret).beats[caret.beat].graces.size();
}

namespace {

// The diagram the frets of `beat` make on `staff` (ADR-0080): none unless two
// or more of its notes have a fret.
std::optional<ChordDiagram> diagramOf(const Staff& staff, const Beat& beat) {
    if (!staff.isFretted() || isDrumStaff(staff)) {
        return std::nullopt;
    }
    const int strings = staff.tuning()->stringCount();
    std::vector<std::int8_t> frets(static_cast<std::size_t>(strings), ChordDiagram::kMuted);
    int fretted = 0;
    for (const Note& note : beat.notes) {
        if (!note.position || note.position->string < 1 || note.position->string > strings) {
            continue;
        }
        frets[static_cast<std::size_t>(note.position->string - 1)] = note.position->fret;
        ++fretted;
    }
    if (fretted < 2) {
        return std::nullopt;
    }
    return ChordDiagram::ofFrets(std::move(frets));
}

std::string_view trimmed(std::string_view text) {
    const auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

Entry setChordSymbol(const Song& song, const Caret& at, std::string_view name) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    name = trimmed(name);
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    if (name.empty()) {
        if (!beat.chord) {
            return unchanged(caret);
        }
        beat.chord.reset();
        return replace(caret, std::move(content), "Remove chord symbol");
    }
    std::optional<ChordDiagram> diagram =
        diagramOf(song.track(caret.track).staff(caret.staff), beat);
    if (!diagram && beat.chord) {
        diagram = beat.chord->diagram;
    }
    SharedOptional symbol(ChordSymbol{.name = std::string(name), .diagram = std::move(diagram)});
    if (beat.chord == symbol) {
        return unchanged(caret);
    }
    beat.chord = std::move(symbol);
    return replace(caret, std::move(content), "Set chord symbol");
}

std::string chordNameAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return {};
    }
    const Beat& beat = voiceAt(song, caret).beats[caret.beat];
    return beat.chord ? beat.chord->name : std::string();
}

Entry setBeatText(const Song& song, const Caret& at, std::string_view text) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return unchanged(caret);
    }
    text = trimmed(text);
    Measure content = measureAt(song, caret);
    Beat& beat = content.voices.at(caret.voice).beats[caret.beat];
    if (text.empty()) {
        if (!beat.text) {
            return unchanged(caret);
        }
        beat.text.reset();
        return replace(caret, std::move(content), "Remove beat text");
    }
    if (beat.text && *beat.text == text) {
        return unchanged(caret);
    }
    beat.text = SharedOptional(std::string(text));
    return replace(caret, std::move(content), "Set beat text");
}

Entry setClef(const Song& song, const Caret& at, Clef clef) {
    if (clef == Clef::Percussion) {
        throw std::invalid_argument("a staff changes to a pitched clef only");
    }
    const Caret caret = clampCaret(song, at);
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    if (isDrumStaff(staff)) {
        return unchanged(caret);
    }
    // The staff's octave stays (ADR-0008): a guitar reads the treble clef an
    // octave down whatever its clef.
    const bool octave = staff.clef() == Clef::Treble8vb || staff.clef() == Clef::Bass8vb;
    if (clef == Clef::Treble || clef == Clef::Treble8vb) {
        clef = octave ? Clef::Treble8vb : Clef::Treble;
    } else if (clef == Clef::Bass || clef == Clef::Bass8vb) {
        clef = octave ? Clef::Bass8vb : Clef::Bass;
    }
    const Clef before = caret.measure == 0 ? staff.clef() : staff.clefAt(caret.measure - 1);
    const std::optional<Clef> wanted = clef == before ? std::nullopt : std::optional(clef);
    Measure content = measureAt(song, caret);
    if (content.clef == wanted) {
        return unchanged(caret);
    }
    content.clef = wanted;
    return replace(caret, std::move(content), "Change clef");
}

namespace {

/// The other measure of the two-measure repeat measure m is part of, if any.
std::optional<std::size_t> repeatPartner(const Staff& staff, std::size_t m) {
    const MeasureRepeat repeat = staff.measure(m).repeat;
    if (repeat == MeasureRepeat::TwoFirst && m + 1 < staff.measureCount() &&
        staff.measure(m + 1).repeat == MeasureRepeat::TwoSecond) {
        return m + 1;
    }
    if (repeat == MeasureRepeat::TwoSecond && m > 0 &&
        staff.measure(m - 1).repeat == MeasureRepeat::TwoFirst) {
        return m - 1;
    }
    return std::nullopt;
}

/// Measure m without its sign, holding the music the sign played.
Measure writtenOut(const Song& song, const Staff& staff, std::size_t m) {
    Measure content = staff.measure(m);
    content.voices = staff.measure(repeatSource(song, staff, m)).voices;
    content.repeat = MeasureRepeat::None;
    return content;
}

} // namespace

Entry setMeasureRepeat(const Song& song, const Caret& at, std::size_t measures) {
    if (measures > 2) {
        throw std::invalid_argument("a measure repeat repeats one or two measures");
    }
    Caret caret = clampCaret(song, at);
    caret.beat = 0;
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const std::size_t m = caret.measure;
    std::vector<std::size_t> writeOut; // measures whose sign goes, music written out
    std::string name;
    if (measures == 0) {
        if (staff.measure(m).repeat == MeasureRepeat::None) {
            return unchanged(caret);
        }
        name = "Write out measure repeat";
        writeOut.push_back(m);
        if (const std::optional<std::size_t> partner = repeatPartner(staff, m)) {
            writeOut.push_back(*partner);
        }
    } else {
        if (m < measures || m + measures > staff.measureCount()) {
            return unchanged(caret);
        }
        const auto length = [&song](std::size_t measure) {
            return song.measureHeader(measure).timeSignature.measureLength();
        };
        bool there = true;
        for (std::size_t i = 0; i < measures; ++i) {
            if (length(m + i) != length(m + i - measures)) {
                return unchanged(caret);
            }
            const Measure& measure = staff.measure(m + i);
            there = there && measure.isEmpty() && measure.repeat == repeatKind(measures, i);
            if (const std::optional<std::size_t> partner = repeatPartner(staff, m + i);
                partner && (*partner < m || *partner >= m + measures)) {
                writeOut.push_back(*partner);
            }
        }
        if (there) {
            return unchanged(caret);
        }
        name = measures == 1 ? "Repeat measure" : "Repeat two measures";
    }
    std::vector<std::unique_ptr<Command>> commands;
    for (const std::size_t measure : writeOut) {
        commands.push_back(std::make_unique<ReplaceMeasure>(
            caret.track, caret.staff, measure, writtenOut(song, staff, measure), name));
    }
    for (std::size_t i = 0; i < measures; ++i) {
        Measure content = staff.measure(m + i);
        content.voices = {};
        content.repeat = repeatKind(measures, i);
        commands.push_back(std::make_unique<ReplaceMeasure>(caret.track, caret.staff, m + i,
                                                            std::move(content), name));
    }
    if (commands.size() == 1) {
        return Entry{.command = std::move(commands.front()), .caret = caret};
    }
    return Entry{.command = std::make_unique<CommandGroup>(name, std::move(commands)),
                 .caret = caret};
}

std::size_t measureRepeatAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    switch (song.track(caret.track).staff(caret.staff).measure(caret.measure).repeat) {
    case MeasureRepeat::None:
        return 0;
    case MeasureRepeat::One:
        return 1;
    case MeasureRepeat::TwoFirst:
    case MeasureRepeat::TwoSecond:
        return 2;
    }
    return 0;
}

std::string beatTextAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return {};
    }
    const Beat& beat = voiceAt(song, caret).beats[caret.beat];
    return beat.text ? *beat.text : std::string();
}

VerseText verseAt(const Song& song, const Caret& at, std::size_t verse) {
    const Caret caret = clampCaret(song, at);
    return verseText(song.track(caret.track).staff(caret.staff), verse);
}

VerseEntry setVerse(const Song& song, const Caret& at, std::size_t verse, std::size_t first,
                    std::string_view text) {
    const Caret caret = clampCaret(song, at);
    if (first >= song.measureCount()) {
        throw std::out_of_range("setVerse: no such measure");
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    // Measures are shared between copies (ADR-0051): the copy writes only
    // those the verse changes.
    Staff typed = staff;
    placeVerse(typed, verse, 0, "");
    const std::size_t leftover = placeVerse(typed, verse, first, text);
    bool sung = false;
    std::vector<std::unique_ptr<Command>> steps;
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        const Measure& now = std::as_const(typed).measure(m);
        sung = sung || std::ranges::any_of(now.voices[0].beats, [verse](const Beat& beat) {
                   return beat.lyrics && verse < beat.lyrics->size() &&
                          !(*beat.lyrics)[verse].text.empty();
               });
        if (now != staff.measure(m)) {
            steps.push_back(
                std::make_unique<ReplaceMeasure>(caret.track, caret.staff, m, now, "Set lyrics"));
        }
    }
    if (steps.empty()) {
        return {.entry = unchanged(caret), .leftover = leftover};
    }
    return {.entry = Entry{.command = std::make_unique<CommandGroup>(
                               sung ? "Set lyrics" : "Remove lyrics", std::move(steps)),
                           .caret = caret},
            .leftover = leftover};
}

Dynamic dynamicAt(const Song& song, const Caret& at) {
    const Caret caret = clampCaret(song, at);
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const Fraction until = caretStart(song, caret);
    for (std::size_t m = caret.measure + 1; m-- > 0;) {
        std::optional<Fraction> latest;
        std::optional<Dynamic> loudest;
        for (const Voice& voice : staff.measure(m).voices) {
            Fraction start;
            for (const Beat& beat : voice.beats) {
                if (m == caret.measure && until < start) {
                    break;
                }
                if (const std::optional<Dynamic> struck = struckDynamic(beat.notes)) {
                    if (!latest || *latest < start) {
                        latest = start;
                        loudest = struck;
                    } else if (*latest == start && *struck > *loudest) {
                        loudest = struck;
                    }
                }
                start += beat.duration.length();
            }
        }
        if (loudest) {
            return *loudest;
        }
    }
    return Dynamic::MF;
}

Entry setDynamic(const Song& song, const Caret& at, Dynamic dynamic) {
    const Caret caret = clampCaret(song, at);
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    const Fraction from = caretStart(song, caret);

    // The passage ends at the first start whose struck notes leave its dynamic.
    std::optional<Dynamic> passage;
    std::size_t endMeasure = staff.measureCount();
    Fraction endStart;
    for (std::size_t m = caret.measure;
         m < staff.measureCount() && endMeasure == staff.measureCount(); ++m) {
        for (const auto& [start, loudest] :
             struckStarts(staff, m, m == caret.measure ? from : Fraction{})) {
            if (!passage) {
                passage = loudest;
            } else if (loudest != *passage) {
                endMeasure = m;
                endStart = start;
                break;
            }
        }
    }
    if (!passage) {
        return unchanged(caret);
    }
    const auto inPassage = [&](std::size_t m, const Fraction& start) {
        if (m == caret.measure && start < from) {
            return false;
        }
        return m < endMeasure || (m == endMeasure && start < endStart);
    };

    // Struck notes in the passage change; tied notes follow the note they
    // continue, the same pitch in the voice's beat before.
    std::array<std::vector<int>, Measure::kMaxVoices> changed{}; // per voice: MIDI numbers
    std::vector<std::unique_ptr<Command>> commands;
    for (std::size_t m = caret.measure; m < staff.measureCount(); ++m) {
        if (m > endMeasure && std::ranges::all_of(changed, &std::vector<int>::empty)) {
            break;
        }
        Measure content = staff.measure(m);
        for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
            Fraction start;
            for (Beat& beat : content.voices[v].beats) {
                std::vector<int> next;
                const bool inside = inPassage(m, start);
                for (Note& note : beat.notes) {
                    const int midi = note.pitch.midiNumber();
                    const bool tied = note.effects.tiedFromPrevious;
                    if (tied ? std::ranges::find(changed[v], midi) != changed[v].end() : inside) {
                        note.effects.dynamic = dynamic;
                        next.push_back(midi);
                    }
                }
                changed[v] = std::move(next);
                start += beat.duration.length();
            }
        }
        if (content != staff.measure(m)) {
            commands.push_back(std::make_unique<ReplaceMeasure>(
                caret.track, caret.staff, m, std::move(content), "Change dynamic"));
        }
    }
    if (commands.empty()) {
        return unchanged(caret);
    }
    if (commands.size() == 1) {
        return Entry{.command = std::move(commands.front()), .caret = caret};
    }
    return Entry{.command = std::make_unique<CommandGroup>("Change dynamic", std::move(commands)),
                 .caret = caret};
}

bool hasEffectAtCaret(const Song& song, const Caret& at, NoteEffect effect) {
    const Caret caret = clampCaret(song, at);
    if (onFreeSlot(song, caret)) {
        return false;
    }
    const Staff& staff = song.track(caret.track).staff(caret.staff);
    bool any = false;
    for (const Note& note : voiceAt(song, caret).beats[caret.beat].notes) {
        if (isEffectTarget(note, staff, caret.string)) {
            if (!hasEffect(note.effects, effect)) {
                return false;
            }
            any = true;
        }
    }
    return any;
}

} // namespace elara::core
