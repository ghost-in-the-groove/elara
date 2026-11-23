// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
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

bool isOnString(const Note& note, int string) {
    return note.position && note.position->string == string;
}

} // namespace

Caret clampCaret(const Song& song, Caret caret) {
    if (song.trackCount() == 0 || song.measureCount() == 0) {
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
    const bool drums = isDrumStaff(song.track(caret.track).staff(caret.staff));
    const auto removed = std::ranges::remove_if(beat->notes, [&](const Note& note) {
        return drums ? note.pitch.midiNumber() == laneKey(caret.string)
                     : isOnString(note, caret.string);
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

} // namespace elara::core
