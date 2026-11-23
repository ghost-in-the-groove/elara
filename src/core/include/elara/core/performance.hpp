// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes of a song as they sound: absolute times, ties joined.
#pragma once

#include <elara/core/fraction.hpp>

#include <cstddef>
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
    int key = 60;            // sounding MIDI key; percussion: the General MIDI percussion key
    int velocity = 80;       // from the note's dynamic (dynamicVelocity)
    std::size_t measure = 0; // where the attack is

    friend bool operator==(const SoundingNote&, const SoundingNote&) = default;
};

/// Every note of the song as written: a tied note extends the note it is tied to
/// (the same pitch, and string on a fretted staff, in the previous beat of its
/// voice). Ordered by start, then track, staff, voice and key. Articulations
/// (staccato, bends, slides...) are not applied: that is playback's job.
[[nodiscard]] std::vector<SoundingNote> soundingNotes(const Song& song);

} // namespace elara::core
