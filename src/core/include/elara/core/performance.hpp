// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes of a song as they sound: absolute times, ties joined.
#pragma once

#include <elara/core/fraction.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace elara::core {

struct Note;
class Song;

/// One attack and how long it sounds, in whole notes from the start of the song
/// (seconds come from Song::secondsAt). Notes tied to it are part of its length.
struct SoundingNote {
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t voice = 0;
    Fraction start{};
    Fraction length{};
    int key = 60;      // sounding MIDI key; percussion: the General MIDI percussion key
    int velocity = 80; // from the note's dynamic (dynamicVelocity), ramped by hairpins
    // Where the attack is: its beat in its voice of `measure`, and its note in
    // that beat, or in the beat's grace `grace - 1` when `grace` is not 0
    // (ADR-0079); the note holds its effects. These sizes keep the struct at
    // 80 bytes.
    std::uint32_t beat = 0;
    std::uint16_t note = 0;
    std::uint16_t grace = 0;
    std::size_t measure = 0;

    friend bool operator==(const SoundingNote&, const SoundingNote&) = default;
};

/// Every note of the song as written: a tied note extends the note it is tied to
/// (the same pitch, and string on a fretted staff, in the previous beat of its
/// voice). Ordered by start, then track, staff, voice and key. Articulations
/// (staccato, bends, slides...) are not applied: that is playback's job, which
/// finds them through each note's measure, beat and note.
[[nodiscard]] std::vector<SoundingNote> soundingNotes(const Song& song);

/// The note `sounding` strikes: a note of its beat or of one of the beat's
/// grace notes.
[[nodiscard]] const Note& noteOf(const Song& song, const SoundingNote& sounding);

} // namespace elara::core
