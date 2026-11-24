# 0034. Note names at the caret

- Status: Accepted
- Date: 2026-11-23

## Context

Keyboard entry (ADR-0021) types frets on the caret's string, which only works
on a fretted staff. Piano, voice, violin, flute and the wind instruments have no
strings, so nothing can be typed on them; guitarists who read notation also
think in note names more often than in frets. Score editors settled long ago on
typing the letter of a note, with the octave chosen for the user, and moving the
note by semitones or octaves with modified arrow keys.

## Decision

**A letter types the note on that step.** `enterPitch(song, caret, step,
duration, mode)` writes the note on `step` (C to B) with the accidental of the
measure's key. The octave is the one nearest the highest note of the previous
beat with notes in the voice, looking back through earlier measures: at most
three steps away, as the eye measures it on the staff (A above E, G below A).
With no earlier note the reference is the staff's middle line (B4 in the treble
clef, D3 in the bass clef, C4 and A3 in the alto and tenor clefs).

**What the staff writes, not what sounds.** On a transposing instrument (a
B-flat trumpet, a guitar written an octave up) the letter is the written note:
the reference and the key are moved to written pitch by the instrument's
written offset, the note is chosen there, and it is stored at its sounding
pitch. A written B on a trumpet in a concert F major song is A4.

**Replace or add.** By default the note replaces the notes of the caret's beat
(a rest becomes a note of the rest's duration; the free slot gets a new beat of
the input duration). `PitchMode::AddToChord` puts the note above the beat's
highest note, within an octave, so a chord is built from the bottom up.

**On tablature too.** On a fretted staff the note goes on the caret's string
when that string reaches it, otherwise on the nearest string that does (one
without a note when it joins a chord), and the caret moves to that string. A
note no string can play is refused.

**Pitch moves.** `shiftPitch(song, caret, semitones)` moves the note on the
caret's string along that string, or every note of the beat on a staff without
strings, respelled in the key and keeping its effects. Delete on a staff without
strings removes every note of the beat, leaving a rest, since there is no
string to choose one note.

**Percussion has no names.** Drums take General MIDI keys, not note names;
`enterPitch` refuses a percussion staff and drum entry gets its own design.

`elara-cli edit` gains `pitch <C..B> [add]` and `shift <semitones>`.

## Consequences

- Every pitched instrument can be typed on from the keyboard, with the same
  caret, durations and undo as frets.
- The app maps letter keys, Shift with a letter (add to chord) and Alt with the
  arrows (semitone, octave with Ctrl) onto these edits, and its status line
  names the notes at the caret.
- Accidentals outside the key come from `shiftPitch`; a spelling choice
  (sharp or flat) follows the key's own preference.
