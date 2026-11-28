# 0093. Fingerings

- Status: Accepted
- Date: 2026-11-25

## Context

Scores write which finger plays a note. Keyboard music numbers the fingers of
each hand 1 to 5 from the thumb, over the notes of the right hand and under
those of the left. Music for fretted instruments numbers the fingers of the
fretting hand 1 to 4 from the index (T for the thumb), small and left of the
notehead, and names those of the plucking hand by letters: p, i, m, a and c,
from the thumb (Spanish names of the fingers).

- MusicXML writes `<fingering>` (free text, mostly a digit) and `<pluck>` (a
  letter) in a note's `<technical>`. A second `<fingering>` is a substitution
  or an alternative. The importer dropped both silently.
- Guitar Pro 3-5 store, behind bit 7 of a note's flags, two signed bytes: the
  finger of the left hand and of the right hand, -1 for none and 0 the thumb
  to 4 the little finger. The reader skipped them. How Guitar Pro 6-8 store
  fingerings in GPIF is not documented where we could check it.
- SMuFL has `fingering0` to `fingering9`, `fingeringTUpper` and the lowercase
  letters `fingeringPLower`, `fingeringILower`, `fingeringMLower`,
  `fingeringALower` and `fingeringCLower`.

## Decision

**A note can have a finger and a plucking finger**: `NoteEffects::fingering`
and `NoteEffects::pluck`, each a `Finger` (`None`, `Thumb`, `Index`, `Middle`,
`Ring`, `Little`). The model keeps which finger, not the number written:
`fingeringText()` writes it "1" to "5" from the thumb, or on a fretted
instrument "T" and "1" to "4" from the index; `pluckText()` as p, i, m, a, c.
`fingerOfText()` and `pluckOfText()` read them back. The two take bytes
`NoteEffects` had as padding: `Note` stays 48 bytes. Fingerings do not change
how a note sounds.

**Kept** by the native format as the note effects `"fingering"` and
`"pluck"`: `"thumb"`, `"index"`, `"middle"`, `"ring"`, `"little"`. MusicXML
reads and writes `<fingering>` by the part's kind of instrument (fretted or
not) and `<pluck>`; a second fingering of a note, or another text, warns.
Guitar Pro 3-5 read both bytes.

**Drawn** on standard notation, with `Role::Fingering`:

- for a fretted instrument, each fingering small (0.8) and level with its
  notehead, left of the accidentals and of the ledger lines, in columns so
  that two keep a staff space apart; the notes make room for them as for
  accidentals. The plucking fingers go over the notes, or under them for the
  lower voice of two;
- for the others, the fingers one over the other in the order of the chord's
  notes, centred on the noteheads, clear of the ink: over the notes, under
  them on a lower staff of the track (a piano's left hand) and for the lower
  voice of two.

Slurs go round them. Tablature draws none.

**Edited** with `core::setFingering()` (one finger per note from the lowest
up, or one for all) and `core::setPluck()` (fretted staves only), the CLI's
`fingering <finger...>` (`1`-`5`, or `t` and `1`-`4` on a fretted staff,
`none`) and `pluck <p|i|m|a|c|none>`, and two buttons in the app's entry
bar: one steps the caret's notes through the fingers, giving all of them the
same one (on a fretted staff from the index, the thumb last), the other,
shown on fretted staves, through p, i, m, a and c. The order is core's
(`core::nextFingering()`, `core::nextPluck()`), which the app's
`CaretEditor::nextFingering()` and `nextPluck()` follow.

## Consequences

- A fingering changes how its measure is drawn: the incremental layout lays
  the measure out again as for any other note effect.
- Not covered: substitutions (3-1 on a held note), fingerings in parentheses
  or brackets, thumb positions, the fingering of Guitar Pro 6-8 files, and
  the string number in a circle.
