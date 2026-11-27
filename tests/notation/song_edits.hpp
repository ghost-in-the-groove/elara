// SPDX-License-Identifier: AGPL-3.0-or-later
// Random edits of the kinds an editor makes, for the layout tests that check
// what an edit does to the pages.
#pragma once

#include <elara/core/effects.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace elara::notation::test {

/// One of `count` things, from the generator's own output: the same on every
/// standard library (distributions are not), so a failure seen on one platform
/// is the failure seen on all.
inline std::size_t pick(std::mt19937& random, std::size_t count) {
    return static_cast<std::size_t>(random() % count);
}

/// The first note of `measure`, or null.
inline core::Note* firstNote(core::Measure& measure) {
    for (auto& voice : measure.voices) {
        for (auto& beat : voice.beats) {
            if (!beat.notes.empty()) {
                return &beat.notes.front();
            }
        }
    }
    return nullptr;
}

/// The beats of the first voice of `measure` that has notes, or none.
inline std::vector<core::Beat>* firstVoiceWithNotes(core::Measure& measure) {
    for (auto& voice : measure.voices) {
        for (const auto& beat : voice.beats) {
            if (!beat.notes.empty()) {
                return &voice.beats;
            }
        }
    }
    return nullptr;
}

/// `note` a whole step lower on `staff`, or null when it cannot go there. On a
/// fretted staff, on a string `taken` does not hold, as a guitarist plays two
/// voices; never on a drum staff, whose notes are kit pieces.
inline std::optional<core::Note> stepBelow(const core::Staff& staff, const core::Note& note,
                                           std::vector<int>& taken) {
    if (staff.clef() == core::Clef::Percussion || note.pitch.midiNumber() < 2) {
        return std::nullopt;
    }
    const core::Pitch lower = core::Pitch::fromMidi(note.pitch.midiNumber() - 2);
    if (!staff.isFretted()) {
        return core::Note{.pitch = lower};
    }
    for (const core::FretPosition at : staff.tuning()->positionsFor(lower, staff.capo())) {
        if (std::ranges::find(taken, at.string) == taken.end()) {
            taken.push_back(at.string);
            return staff.frettedNote(at.string, at.fret);
        }
    }
    return std::nullopt;
}

/// One edit of the kinds an editor makes, at a random place: a measure's
/// notes, a note's or beat's effects, lyrics, chord symbols, a second voice, a
/// measure's header or a staff's clef. Returns what it did, to name it when a
/// check fails.
inline std::string edit(core::Song& song, std::mt19937& random) {
    using namespace elara::core;
    const std::size_t m = pick(random, song.measureCount());
    const std::size_t t = pick(random, song.trackCount());
    Track& track = song.track(t);
    const std::size_t s = pick(random, track.staffCount());
    Staff& staff = track.staff(s);
    MeasureHeader& header = song.measureHeader(m);
    const std::size_t which = pick(random, 18);
    // What the edit did, at measure m of track t, staff s (from 0).
    const auto did = [&](std::string_view what) {
        return std::format("{} at measure {} of track {}, staff {}", what, m, t, s);
    };
    switch (which) {
    case 0: { // another measure's notes, as paste does
        const std::size_t from = pick(random, song.measureCount());
        staff.measure(m) = staff.measure(from);
        return did(std::format("the notes of measure {}", from));
    }
    case 1:
        staff.measure(m) = Measure{};
        return did("the notes cleared");
    case 2:
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.tiedFromPrevious = !note->effects.tiedFromPrevious;
        }
        return did("a tie toggled");
    case 3:
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.palmMute = !note->effects.palmMute;
            note->effects.letRing = pick(random, 2) == 0;
        }
        return did("palm mute and let ring");
    case 4: {
        constexpr std::array kDynamics{Dynamic::P, Dynamic::MF, Dynamic::FF};
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.dynamic = kDynamics[pick(random, kDynamics.size())];
        }
        return did("a dynamic");
    }
    case 5:
        if (header.tempoChanges.empty()) {
            header.tempoChanges.push_back(TempoChange{
                .offset = {}, .tempo = Tempo(60.0 + static_cast<double>(pick(random, 120)))});
            return did("a tempo change");
        }
        header.tempoChanges.clear();
        return did("the tempo changes removed");
    case 6:
        header.repeatStart = !header.repeatStart;
        return did("a repeat start toggled");
    case 7:
        header.repeatTimes =
            header.repeatEnd() ? 0 : static_cast<std::uint8_t>(2 + pick(random, 3));
        header.alternateEndings = static_cast<std::uint8_t>(pick(random, 4));
        return did("a repeat end and endings");
    case 8:
        song.setStartTempo(Tempo(60.0 + static_cast<double>(pick(random, 120))));
        return did("the start tempo");
    case 9: { // a clef change, or none (ADR-0087)
        constexpr std::array kClefs{Clef::Treble, Clef::Bass, Clef::Alto};
        const std::size_t clef = pick(random, kClefs.size() + 1);
        staff.measure(m).clef = clef < kClefs.size() ? std::optional(kClefs[clef]) : std::nullopt;
        return did("a clef change");
    }
    case 10: { // an octave line over the whole measure, running on into its neighbours
        constexpr std::array kOttavas{Ottava::None, Ottava::Alta, Ottava::Bassa};
        const Ottava ottava = kOttavas[pick(random, kOttavas.size())];
        for (auto& voice : staff.measure(m).voices) {
            for (auto& beat : voice.beats) {
                beat.effects.ottava = ottava;
            }
        }
        return did("an octave line");
    }
    case 11: { // the pedal over the whole measure, changed on its first beat or not
        constexpr std::array kPedals{Pedal::None, Pedal::Down, Pedal::Change};
        const Pedal pedal = kPedals[pick(random, kPedals.size())];
        for (auto& voice : staff.measure(m).voices) {
            for (auto& beat : voice.beats) {
                beat.effects.pedal = &beat == &voice.beats.front() ? pedal
                                     : pedal == Pedal::None        ? Pedal::None
                                                                   : Pedal::Down;
            }
        }
        return did("a pedal");
    }
    case 12: // a hammer-on from the measure before, slurred on it (ADR-0097)
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.legatoFromPrevious = !note->effects.legatoFromPrevious;
        }
        return did("a hammer-on toggled");
    case 13: // a slide to the next note, drawn on to it (ADR-0098)
        if (Note* note = firstNote(staff.measure(m))) {
            note->effects.slideOut =
                note->effects.slideOut == SlideOut::None ? SlideOut::Shift : SlideOut::None;
        }
        return did("a slide toggled");
    case 14: { // a syllable on every note of the first voice, or none (ADR-0084)
        constexpr std::array<std::string_view, 4> kWords{"la", "Hal", "le", "lujah"};
        const bool sung = pick(random, 3) != 0;
        if (std::vector<Beat>* beats = firstVoiceWithNotes(staff.measure(m))) {
            std::size_t word = pick(random, kWords.size());
            for (Beat& beat : *beats) {
                if (beat.notes.empty() || !sung) {
                    beat.lyrics = {};
                    continue;
                }
                beat.lyrics = SharedOptional(std::vector<Syllable>{
                    Syllable{.text = std::string(kWords[word]), .hyphen = word % 2 == 0}});
                word = (word + 1) % kWords.size();
            }
        }
        return did(sung ? "lyrics" : "the lyrics removed");
    }
    case 15: { // a chord symbol on every other beat, or none
        constexpr std::array<std::string_view, 4> kNames{"Cmaj7", "F#m7b5", "G7sus4", "Bb"};
        const bool named = pick(random, 3) != 0;
        std::vector<Beat>& beats = staff.measure(m).voices[0].beats;
        for (std::size_t b = 0; b < beats.size(); ++b) {
            beats[b].chord = named && b % 2 == 0
                                 ? SharedOptional(ChordSymbol{
                                       .name = std::string(kNames[(b / 2 + m) % kNames.size()])})
                                 : SharedOptional<ChordSymbol>{};
        }
        return did(named ? "chord symbols" : "the chord symbols removed");
    }
    case 16: { // a second voice a whole step under the first, or none (ADR-0151)
        Measure& measure = staff.measure(m);
        if (pick(random, 3) == 0) {
            measure.voices[1] = {};
            return did("the second voice removed");
        }
        Voice second;
        for (const Beat& beat : measure.voices[0].beats) {
            Beat lower{.duration = beat.duration};
            std::vector<int> taken; // the strings the first voice's notes are on
            for (const Note& note : beat.notes) {
                if (note.position) {
                    taken.push_back(note.position->string);
                }
            }
            for (const Note& note : beat.notes) {
                if (const std::optional<Note> below = stepBelow(staff, note, taken)) {
                    lower.notes.push_back(*below);
                }
            }
            second.beats.push_back(std::move(lower));
        }
        measure.voices[1] = std::move(second);
        return did("a second voice");
    }
    default:
        header.keySignature = KeySignature(static_cast<int>(pick(random, 15)) - 7, KeyMode::Major);
        return did("a key signature");
    }
}

} // namespace elara::notation::test
