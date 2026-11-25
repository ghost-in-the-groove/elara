# 0096. Split beats for marks between beats

- Status: Accepted
- Date: 2026-11-25

## Context

A chord symbol (ADR-0080) and a beat's text (ADR-0081) belong to a beat. A
MusicXML `<harmony>` or `<direction>` can stand anywhere in time, and lead
sheets put many between beats: the melody holds a note while the chords
change under it (a whole note under "C" and then "Am" halfway). Every such
mark was dropped with a warning (`chord symbols between beats dropped`, `text
directions dropped`), so a lead sheet lost half of its chords on import.

Three ways to keep them were weighed:

- **Marks at a time instead of on a beat.** The model, the editing commands,
  the native format, both importers and the layout would change for a case
  only MusicXML import meets; Guitar Pro keeps chords on beats too.
- **Another voice of invisible rests** that starts there. The model has no
  invisible rests, and every view and the caret would have to skip them.
- **Splitting the beat** that sounds across the mark into tied beats, as an
  engraver writes a held note under a chord change. It sounds the same,
  keeps the model as it is, and reads as engravers write it.

## Decision

**Import splits the beat.** `core::splitBeatAt(voice, onset)` makes a beat
start at `onset` by splitting the beat sounding across it into beats of plain
or dotted values, longest first (a whole note under a change at its fourth
eighth: a dotted quarter, then a half and an eighth), its notes tied from each
part to the next. Rests split into rests. The first part keeps what the beat
held: its notes' effects, the strum and pick stroke, grace notes, chord
symbol, text and lyrics. The later parts tie the notes on and carry what spans
beats: the hairpin, octave line, slur and tremolo picking, the pedal (down,
not changed again), and the notes' harmonics, trill, vibrato, palm muting,
let ring, dead or ghost note and dynamic. Playback joins the tied parts into
one sounding note that takes its articulation from the first, so the song
plays as before.

**Some beats do not split.** A tuplet, a fermata (ADR-0085, it holds the whole
beat), a breath mark (ADR-0095, after the whole beat), a whammy bar, or a
note that bends or slides out belongs to the whole beat; such a beat stays
whole and the mark is still dropped with its warning.

**A measure rest is written out.** A measure rest written as one is an empty
voice, which has no beat; lead sheets name the chords of an introduction over
such rests, and parts write "solo" or "dolce" over them. When every voice of
the mark's staff is empty, the first is written out as rests filling the
measure (one rest in most meters, which the layout still draws as a measure
rest in the middle of the measure), as a held measure rest is (ADR-0085),
and then split like any beat.

**MusicXML import** writes out the staff's measure rest if it has one, looks
for a beat starting at the mark in the voices of its staff, in order, then
splits the first voice whose beat can be split there.
Slur ends recorded on later beats of that voice and measure move on with
them, so a slur keeps its notes (ADR-0086).

## Consequences

- Lead sheets keep their chords, and texts between beats keep their place;
  the held notes are written as tied notes, which MusicXML export writes back
  as such.
- Editing in the app or the CLI does not split beats: typing a chord or text
  still goes on the caret's beat.
- `splitBeatAt()` is a core function, so a later feature (an edit that sets a
  mark between beats, a paste) can use it.
