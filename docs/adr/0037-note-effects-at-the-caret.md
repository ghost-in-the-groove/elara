# 0037. Note effects at the caret

- Status: Accepted
- Date: 2026-11-23

## Context

The model holds a rich set of note effects (`NoteEffects`: ties, hammer-ons
and pull-offs, slides, bends, vibrato, palm mutes, dead and ghost notes, let
ring, harmonics, accents, staccato), and the importers fill them in. Keyboard
entry (ADR-0021, ADR-0034, ADR-0035) types notes but cannot add any of them, so
a tablature written in Elara stays plain. Most effects have several forms (a
bend of any curve, five kinds of harmonic, three slides off a note); typing
needs one key per effect, not a dialog for each.

## Decision

**One toggle per effect, in its usual form.** `toggleEffect(song, caret,
effect)` toggles a `NoteEffect` on the notes the caret is on, in the form an
effect is most often written:

| Effect | On | Counts as on |
|---|---|---|
| Tie | tied from the previous note, at its pitch and fret | `tiedFromPrevious` |
| Legato | hammer-on or pull-off from the previous note | `legatoFromPrevious` |
| Slide | a shift slide to the next note | any slide off the note |
| Bend | a full-step bend (`BendCurve::bend(200)`) | any bend |
| Vibrato | slight | any vibrato |
| Harmonic | natural, at the note's fret | any harmonic |
| Accent | an accent | an accent or marcato |
| Palm mute, dead note, let ring, ghost, staccato | the flag | the flag |

Other forms (bend curves, wide vibrato, artificial harmonics, slides into a
note) are left to an effect editor; the toggle still turns them off.

**Which notes.** The note on the caret's string on a fretted staff, the lane's
hit on a percussion staff, every note of the beat elsewhere. The effect goes on
when any of them lacks it and off when all have it, as one undo step ("Change
effect"). Nothing changes on a rest, the free slot or a string without a note.

**A tie continues a note.** It takes the pitch and fret of the note it ties
from, in the beat just before (across the barline): on a fretted staff that
beat's note on the same string, elsewhere its note of the same pitch or its
only note. With nothing to continue it is refused rather than written as a tie
the validator would report.

**A natural harmonic needs a node.** It is refused on a staff without strings
and at a fret where no partial up to the 16th has a node
(`naturalHarmonicSemitones`).

**CLI.** `elara-cli edit` gains `effect <name>` with the names `tie`, `legato`,
`slide`, `bend`, `vibrato`, `palm-mute`, `dead-note`, `let-ring`, `harmonic`,
`ghost`, `accent` and `staccato`.

## Consequences

- The app maps keys to the toggles in a later change, on top of the caret in
  the app; letters A to G already type note names, so the effects take other
  keys.
- A tie keeps its pitch when it is turned off again; the note does not go back
  to the fret it had before.
- Dynamics, beat effects (strokes, tremolo picking, whammy bar) and bend curves
  need their own editors.
