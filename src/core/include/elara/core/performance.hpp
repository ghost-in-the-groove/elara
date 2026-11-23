// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes of a song as they sound: absolute times, ties joined.
#pragma once

#include <elara/core/fraction.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace elara::core {

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
    int velocity = 80; // from the note's dynamic (dynamicVelocity)
    // Where the attack is: its beat in its voice of `measure`, and its note in
    // that beat, which holds its effects. 32 bits keep the struct at 80 bytes.
    std::uint32_t beat = 0;
    std::uint32_t note = 0;
    std::size_t measure = 0;

    friend bool operator==(const SoundingNote&, const SoundingNote&) = default;
};

/// Every note of the song as written: a tied note extends the note it is tied to
/// (the same pitch, and string on a fretted staff, in the previous beat of its
/// voice). Ordered by start, then track, staff, voice and key. Articulations
/// (staccato, bends, slides...) are not applied: that is playback's job, which
/// finds them through each note's measure, beat and note.
[[nodiscard]] std::vector<SoundingNote> soundingNotes(const Song& song);

} // namespace elara::core
