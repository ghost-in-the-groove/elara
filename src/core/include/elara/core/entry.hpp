// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/song.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

/// Keyboard entry (ADR-0021): a caret on a beat and a string, the moves of the
/// arrow keys, and the edits that typing frets, durations and deletes makes.
/// Edits come back as commands, so they go through the undo history (ADR-0005).
namespace elara::core {

/// Where keyboard entry happens: a beat of one voice and, on a fretted staff, a
/// string (1 is the highest); on a percussion staff `string` is a lane, one per
/// drumPieces() entry (1 is the top). `beat` may be one past the last beat of a voice
/// shorter than its measure (an empty voice included): the free slot, where
/// typing adds a beat.
struct Caret {
    std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t measure = 0;
    std::size_t voice = 0;
    std::size_t beat = 0;
    int string = 1;

    friend bool operator==(const Caret&, const Caret&) = default;
};

/// The nearest caret the song has: every index clamped (so a caret whose beat
/// or measure went away moves back), the beat to the free slot or else the last
/// beat, the string to the staff's strings or drum lanes (1 on any other staff).
/// Throws std::invalid_argument for a song without tracks or measures.
[[nodiscard]] Caret clampCaret(const Song& song, Caret caret);

/// True when the caret is on the free slot after the last beat of its voice.
[[nodiscard]] bool onFreeSlot(const Song& song, const Caret& caret);

enum class CaretMove : std::uint8_t {
    NextBeat,        ///< the next beat, then the free slot, then the next measure
    PreviousBeat,    ///< the reverse of NextBeat
    NextMeasure,     ///< the first beat of the next measure
    PreviousMeasure, ///< the first beat of the measure, then of the previous one
    StringUp,        ///< the next higher string
    StringDown,      ///< the next lower string
};

/// Where `move` takes a caret (clamped first). Moves stop at the ends of the
/// song and of the strings; moving between measures keeps the voice and string.
[[nodiscard]] Caret moveCaret(const Song& song, const Caret& caret, CaretMove move);

/// An edit to execute and where the caret goes after it.
struct Entry {
    std::unique_ptr<Command> command; ///< nullptr when nothing changes
    Caret caret;
};

/// Typing a fret: the note at `fret` (from the capo) on the caret's string,
/// spelled in the measure's key. On a beat it replaces the note on that string,
/// keeping its effects, or joins the chord (a rest becomes a note of the same
/// duration); on the free slot it adds a beat of `duration`. The caret stays.
/// A new note takes the dynamic in force at the caret (see dynamicAt()).
/// Throws std::logic_error on a staff without a tuning and std::out_of_range for
/// a fret off the neck.
[[nodiscard]] Entry enterFret(const Song& song, const Caret& caret, int fret, Duration duration);

/// What a note name does to the caret's beat.
enum class PitchMode : std::uint8_t {
    Replace,    ///< the note becomes the beat's only note
    AddToChord, ///< the note joins the beat's notes, above the highest
};

/// Typing a note name (ADR-0034): the note on `step`, as the staff writes it,
/// with the accidental of the measure's key (the written key on a transposing
/// instrument). `Replace` picks the octave nearest the highest note of the
/// previous beat with notes in the voice (or the staff's middle line when there
/// is none): at most three steps away, as on the staff. It replaces the notes
/// of the caret's beat (a rest becomes a note of the same duration); on the free
/// slot it adds a beat of `duration`. `AddToChord` puts the note above the
/// highest note of the beat, within an octave (on a rest or the free slot it
/// works as `Replace`). On a fretted staff the note goes on the caret's string
/// when that string reaches it, otherwise on the nearest string that does (in a
/// chord, one without a note), and the caret follows it. The note takes the
/// dynamic in force at the caret (see dynamicAt()). Throws
/// std::logic_error on a percussion staff and std::out_of_range when the note
/// leaves MIDI 0..127 or no string can play it.
[[nodiscard]] Entry enterPitch(const Song& song, const Caret& caret, Step step, Duration duration,
                               PitchMode mode = PitchMode::Replace);

/// Moves the caret's note by `semitones`, spelled in the measure's key and
/// keeping its effects. On a fretted staff that is the note on the caret's
/// string, which keeps its string; elsewhere every note of the beat. Nothing
/// changes on a rest, the free slot or a string without a note. Throws
/// std::out_of_range when a note would leave MIDI 0..127 or the neck.
[[nodiscard]] Entry shiftPitch(const Song& song, const Caret& caret, int semitones);

/// The caret's beat gets `duration`; nothing changes on the free slot.
[[nodiscard]] Entry setBeatDuration(const Song& song, const Caret& caret, Duration duration);

/// Delete: removes the note on the caret's string (on a percussion staff, the
/// lane's kit piece; on another staff without a tuning, every note of the beat),
/// leaving a rest when it was the last one. On a rest it
/// removes the beat, and the caret stays at its index (now the next beat or the
/// free slot). Nothing changes on the free slot or on a string without a note.
[[nodiscard]] Entry deleteAtCaret(const Song& song, const Caret& caret);

/// Removes the caret's beat with all its notes; the caret stays at its index.
[[nodiscard]] Entry deleteBeat(const Song& song, const Caret& caret);

/// Insert: a rest of `duration` before the caret's beat (or on the free slot);
/// the caret goes to it.
[[nodiscard]] Entry insertRest(const Song& song, const Caret& caret, Duration duration);

/// Drum entry (ADR-0035): toggles the kit piece of the caret's lane on its beat.
/// Absent, it joins the beat's hits in lane order (a rest becomes a hit of the
/// same duration; the free slot gets a beat of `duration`) at the dynamic in
/// force there (see dynamicAt()); present, it goes, leaving a rest when it was
/// the last. The caret stays. Throws
/// std::logic_error on a staff that is not a percussion staff.
[[nodiscard]] Entry toggleDrum(const Song& song, const Caret& caret, Duration duration);

/// A note effect that typing toggles (ADR-0037), in the form it is usually
/// written.
enum class NoteEffect : std::uint8_t {
    Tie,      ///< tied from the previous note
    Legato,   ///< hammer-on or pull-off from the previous note
    Slide,    ///< a shift slide to the next note
    Bend,     ///< a full-step bend
    Vibrato,  ///< slight vibrato
    PalmMute, ///< palm mute
    DeadNote, ///< a muted, percussive note
    LetRing,  ///< let ring
    Harmonic, ///< a natural harmonic at the note's fret
    Ghost,    ///< a soft note, written in parentheses
    Accent,   ///< an accent
    Staccato, ///< staccato
};

/// Whether `effect` is a technique of strings (hammer-on or pull-off, palm
/// mute, dead note, natural harmonic), which toggleEffect() puts only on a
/// fretted staff.
[[nodiscard]] constexpr bool needsStrings(NoteEffect effect) noexcept {
    return effect == NoteEffect::Legato || effect == NoteEffect::PalmMute ||
           effect == NoteEffect::DeadNote || effect == NoteEffect::Harmonic;
}

/// Toggles `effect` on the notes the caret is on: the note on its string on a
/// fretted staff, the lane's hit on a percussion staff, every note of the beat
/// elsewhere. It goes on when any of them lacks it and off when all have it,
/// as one undo step ("Change effect"); any slide off the note counts as a
/// slide, any bend as a bend. A tie takes the pitch (and fret) of the note it
/// continues, in the beat just before: on a fretted staff its note on the same
/// string, elsewhere its note of the same pitch or its only note. Nothing
/// changes on a rest, the free slot or a string without a note. Throws
/// std::logic_error for a tie with nothing to continue or for a technique of
/// strings (needsStrings()) going on a staff without them, and
/// std::out_of_range for a natural harmonic at a fret without a node.
[[nodiscard]] Entry toggleEffect(const Song& song, const Caret& caret, NoteEffect effect);

/// Sets the bend of the notes the caret is on (as for toggleEffect) to `curve`,
/// any form or an empty curve to remove it (ADR-0055), as one undo step
/// ("Change bend"). Nothing changes when they all have it already, on a rest,
/// the free slot or a string without a note.
[[nodiscard]] Entry setBend(const Song& song, const Caret& caret, const BendCurve& curve);

/// The bend of the first note the caret is on (as for toggleEffect); empty on
/// a rest, the free slot or a string without a note.
[[nodiscard]] BendCurve bendAtCaret(const Song& song, const Caret& caret);

/// The effects of the first note the caret is on (as for toggleEffect); the
/// defaults on a rest, the free slot or a string without a note.
[[nodiscard]] NoteEffects noteEffectsAt(const Song& song, const Caret& caret);

/// Each of these sets one effect of the notes the caret is on (as for
/// toggleEffect) to any of its kinds, as one undo step (ADR-0058). Nothing
/// changes when they all have it already, on a rest, the free slot or a string
/// without a note.
///
/// setSlides() sets how the notes slide in and out ("Change slide").
[[nodiscard]] Entry setSlides(const Song& song, const Caret& caret, SlideIn in, SlideOut out);
/// Slight, wide or no vibrato ("Change vibrato").
[[nodiscard]] Entry setVibrato(const Song& song, const Caret& caret, Vibrato vibrato);
/// An accent, a marcato or neither ("Change accent").
[[nodiscard]] Entry setAccent(const Song& song, const Caret& caret, Accent accent);
/// A harmonic of `kind`, or none ("Change harmonic"): a natural harmonic at the
/// note's fret, the others an octave above the fretted note. Throws
/// std::logic_error for a harmonic on a staff without strings, and
/// std::out_of_range for a natural one at a fret near no node.
[[nodiscard]] Entry setHarmonic(const Song& song, const Caret& caret, HarmonicKind kind);

/// Sets the effects of the caret's beat (ADR-0056): strum, pick stroke,
/// tremolo picking, whammy bar and bass technique, which apply to all its
/// notes, as one undo step ("Change beat effects"). Nothing changes when the
/// beat has them already, on a rest or on the free slot.
[[nodiscard]] Entry setBeatEffects(const Song& song, const Caret& caret,
                                   const BeatEffects& effects);

/// The effects of the caret's beat; none on a rest or the free slot.
[[nodiscard]] BeatEffects beatEffectsAt(const Song& song, const Caret& caret);

/// The dynamic in force at the caret's beat (ADR-0047): that of the notes struck
/// at the latest start no later than the beat's, over every voice of the staff
/// and looking back through earlier measures (the loudest when several are
/// struck together; tied notes only sustain); mf before the first note.
[[nodiscard]] Dynamic dynamicAt(const Song& song, const Caret& caret);

/// Sets a dynamic at the caret (ADR-0047), as one undo step ("Change dynamic"):
/// the passage from the caret's beat to the next change of dynamic takes
/// `dynamic`. The passage starts at the first start, from the beat's on, with
/// struck notes (on a rest or the free slot, the notes after it), over every
/// voice of the staff; it runs while the notes struck at each start have the
/// loudest dynamic of that first one, through later measures. Every note struck
/// in it changes, and a tied note with the note it continues, even past its
/// end. The caret stays. Nothing changes when the passage already has the
/// dynamic or no note follows the caret.
[[nodiscard]] Entry setDynamic(const Song& song, const Caret& caret, Dynamic dynamic);

/// Whether the notes the caret is on (as for toggleEffect) all have `effect`,
/// in any of its forms: what the next toggle turns off. False on a rest, the
/// free slot or a string without a note.
[[nodiscard]] bool hasEffectAtCaret(const Song& song, const Caret& caret, NoteEffect effect);

} // namespace elara::core
