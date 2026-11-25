# 0087. Clef changes

- Status: Accepted
- Date: 2026-11-25

## Context

A staff can change clef along the way: a piano's left hand moves to the treble
clef for a high passage, a cello to the tenor clef, a bassoon to the tenor
clef. The clef changes where the notes sit on the staff, not what they are, so
without the change high notes pile up on ledger lines. Each staff had one clef
for the whole song, and the importers dropped changes with a warning ("clef
changes dropped").

How the formats keep them:

- MusicXML writes `<clef number="staff">` in a measure's `<attributes>`, which
  can stand anywhere in the measure: at its start, or before the note it
  applies from. Files usually put a change for the next measure at the start
  of that measure; editors write a change in the middle of a measure where the
  music needs it.
- Guitar Pro 6-8 give every bar of a staff its `<Clef>` (`G2`, `F4`, `C3`,
  `C4`, `Neutral`); a change is a bar whose clef differs from the one before.
  Guitar Pro 3-5 and MIDI have none.
- Engraving manuals draw a clef change smaller than the clef at the start of a
  line, before the barline of the measure it changes for, so the reader sees it
  before the measure starts; at the end of a line it warns of the next line's
  clef. The new clef's key signature, when the key changes too, comes after
  the barline.

## Decision

**A measure of a staff can change the clef**: `Measure::clef`, an optional
`Clef`, holds from the start of that measure until the next change.
`Staff::clefAt(m)` is the clef in force at measure m: the latest change at or
before it, else the staff's own clef. Changes live in the measures, so
inserting, deleting, copying and undoing measures carry them with no more
code. A change in the middle of a measure is not kept: a change mid-measure
moves to the start of the next measure, with a warning ("clef changes inside a
measure moved to the next measure") when notes follow it in the measure. The
notes keep their pitches either way, so only where they are drawn moves.

**Drawn** as engravers do: a measure followed by a change ends with the new
clef at three quarters of the size, before its barline, also at the end of a
line; the next line starts with the new clef. The measure's width counts the
clef, so line breaks make room for it. Notes, rests, key signatures and ledger
lines of each measure follow the clef in force there. Tablature does not
change.

**Kept** by the native format as a staff's `"clefs"`: a list of the measures
that change it and the clef they change to. MusicXML reads and writes
`<clef>` in the measure's `<attributes>` (the change mid-measure moved, as
above), Guitar Pro 6-8 bars whose clef differs from the bar before become
changes, keeping the staff's octave (a guitar's treble clef sounds an octave
lower, and so does its change to the bass clef).

**Edited** at the caret: `setClef(song, caret, clef)` changes the clef from the
caret's measure, as one undo step ("Change clef"); choosing the clef in force
before the measure removes the change. The staff keeps its octave as the
importers do: on a staff with an octave clef the treble and bass clefs become
their octave forms, elsewhere the octave forms become the plain ones, and the
C clefs have none. Drum staves keep their clef. The CLI's `edit` takes `clef
<name>` and the measure popup offers the treble, bass, alto and tenor clefs.
A first note name typed with no note before it starts near the middle line of
the clef in force at the caret.

## Consequences

- The incremental relayout (ADR-0052) marks every measure whose clef in force
  differs as changed, up to the next change, and measures again the measure
  before a changed one, since its width holds the clef it changes to.
- Layout looks up the clef of each measure as it goes; the first measure of a
  line walks back to the last change.
- Not covered: clef changes in the middle of a measure, clef changes on
  tablature, octave lines (8va).
