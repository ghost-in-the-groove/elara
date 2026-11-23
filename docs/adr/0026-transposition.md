# 0026. Transposition

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 5 asks for transposition: moving a passage, a track or a whole song
up or down. Pitches keep their spelling (ADR-0004), so moving a note by a number
of semitones leaves open how to spell the result: C# up a semitone could be D
or C##. Key signatures belong to the measure headers, which every track shares.
On a fretted staff a note also has a string and a fret, which must still play
its new pitch (validation checks this).

## Decision

**Pitches move by an interval.** An `Interval` is a number of diatonic steps
and a number of semitones. A pitch moves to the step that many steps away,
with the alteration that gives the new number of semitones: C#4 up a major
second {1, 2} is D#4, B3 up a minor second {1, 1} is C4. When that step would
need more than a double sharp or flat, the pitch is spelled from its MIDI
number instead.

**A number of semitones becomes an interval through the key.**
`Interval::fromSemitones(n, key)` picks the key on the tonic n semitones away
with the fewest accidentals: from 6 flats to 5 sharps, so a tritone from C goes
to G-flat. The interval is the one from the old tonic to the new one. C major
up a semitone goes to D-flat major, a minor second, so C# becomes D. Whole
octaves keep the key. Every note in the passage moves by that one interval,
taken from the key at its first measure. Every chromatic note then keeps its
role in the new key: the raised tonic of C, C#, is B# in B major.

**Key signatures move only when every pitched track moves.** The headers are
shared, so transposing some tracks leaves the keys alone. Their notes are
still spelled by the interval. A transposed key keeps its mode. When its new
tonic would need more than seven accidentals, its enharmonic key is used
instead.

**Fretted notes keep their string when they can.** A note whose new fret is on
the neck (0 to `Tuning::kMaxFret`, counted from the capo) stays on its string.
The others go to the nearest free string that can play them, the higher of two
as near. A tied note or a hammer-on first tries the string its note moved to.
A natural harmonic looks for a string tuned that many semitones away, where
the same node rings the new pitch. When there is none, it becomes an
artificial harmonic over a fretted note. When no string can play a note, the
whole transposition is refused.

**Percussion never moves.** Its keys are drums, not pitches.

`makeTranspose(song, Transposition)` builds all of this as one undo step named
"Transpose": a `ReplaceMeasure` for each changed measure and a
`SetMeasureHeader` for each changed key, in a `CommandGroup`.

## Consequences

- Transposing and transposing back restores the song when no note changed
  string or spelling. A note that moved to another string, a note respelled from
  its MIDI number, or a natural harmonic that became artificial stays that way.
- Strings are chosen one beat at a time. Notes in other voices at the same time
  are not taken into account.
- `elara-cli edit` takes `transpose <semitones> [measures <m> <n>] [tracks
  <t>...]`.
- Instruments with a written offset move by the same interval in sound and in
  writing, so no case is needed for transposing instruments.
