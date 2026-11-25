// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes of a song as they sound: absolute times, ties joined.
#pragma once

#include <elara/core/fraction.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace elara::core {

struct Beat;
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

/// Moves the start and the end of each of `notes` as the triplet feel of the
/// measure each falls in plays it (ADR-0104, swung()); exact, and in the same
/// order. Nothing moves in a song without a triplet feel.
void applyTripletFeel(const Song& song, std::span<SoundingNote> notes);

/// The beat holding the note `sounding` strikes, as written: in its measure,
/// or for a measure repeat in the measure repeated (repeatSource(), ADR-0102).
[[nodiscard]] const Beat& beatOf(const Song& song, const SoundingNote& sounding);

/// The note `sounding` strikes: a note of its beat or of one of the beat's
/// grace notes.
[[nodiscard]] const Note& noteOf(const Song& song, const SoundingNote& sounding);

} // namespace elara::core
