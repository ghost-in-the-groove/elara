# 0021. Keyboard entry at a caret

- Status: Accepted
- Date: 2026-11-23

## Context

The editor (milestone 5) starts with keyboard entry on tablature, as guitar
tab editors do: a caret sits on a beat and a string, the arrow keys move it,
typing a number puts that fret on the string, other keys change the duration,
delete notes or insert rests. Every keystroke must be one undo step
(ADR-0005), and all of it must be testable without the app (ADR-0003).

Two questions shape it. Where does the caret go after the last beat of a
measure that is not full yet? And what does an edit leave behind when it makes
a measure too short or too long?

## Decision

**In the model.** `core::Caret` (`entry.hpp`) names a track, staff, measure,
voice, beat and string. Moving and editing are free functions over a
`const Song&`:

- `moveCaret()` for the arrow keys: next and previous beat, next and previous
  measure, string up and down.
- The edits, which return an `Entry`: the `ReplaceMeasure` command to execute
  (named after the edit: "Enter note", "Change duration", "Delete", "Delete
  beat", "Insert rest") and where the caret goes.
  - `enterFret()` puts a fret on the caret's string.
  - `setBeatDuration()` changes the caret beat's duration.
  - `deleteAtCaret()` and `deleteBeat()` remove a note or a beat.
  - `insertRest()` adds a rest.

The caller executes the command on its `Document`, so the app, the CLI and the
tests share one implementation and one undo history. The caret and the input
duration are editor state, not song data: they are not saved.

**The free slot.** A voice shorter than its measure (an empty voice included)
has one more caret position after its last beat. Typing there adds a beat of
the input duration. Moving right from the last beat goes to the free slot when
there is one, otherwise to the next measure; moving left is the reverse. So
filling an empty measure is: type a fret, right, type a fret, right...

**Entry rules.**

- A fret on a beat replaces the note on that string, keeping its effects, or
  joins the chord. Chords keep their notes from the highest string down, and a
  rest becomes a note of its own duration.
- Notes are spelled in the measure's key, so the G string's third fret is
  B-flat in F major.
- Delete removes the note on the caret's string; on a rest it removes the beat.
- Nothing changes (no command, no undo step) when an edit would change nothing:
  the same fret again, the same duration, delete on an empty string or on the
  free slot.
- Edits may leave a measure short or overfull. Validation reports it
  (`voice-incomplete`, `voice-overfull`) instead of moving notes across
  barlines, which would make one keystroke rewrite measures the user did not
  touch.

`clampCaret()` brings any caret back into the song (after an undo removed its
beat, for example): it is how callers keep the caret valid after every edit.

**Headless.** `elara-cli edit` keeps a caret and an input duration across its
steps: `caret <track> <measure> <beat> [string]`, `right`, `left`, `up`,
`down`, `next-measure`, `previous-measure`, `fret <n>`, `duration <d>` (the
input duration, and the caret beat's), `delete`, `delete-beat`,
`insert-rest`. Its output includes the caret and the input duration.

## Consequences

- The app's key handling becomes a thin mapping from keys to these functions,
  plus drawing the caret where the score shows that beat and string.
- Entry on staves without a tuning (pitch names on a staff, drums by
  instrument) and multi-digit frets typed as separate keys are left to the
  callers and to later work: `enterFret()` takes the whole fret number.
- Short and overfull measures stay visible as validation issues until the user
  fixes them; a later "fill with rests" or "split across the barline" command
  can work on them.
