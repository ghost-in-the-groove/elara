# 0049. Signs and jumps

- Status: Accepted
- Date: 2026-11-23

## Context

Besides repeats and alternate endings (ADR-0016), songs save space with
navigation marks: a sign written over a measure (segno, coda) and an
instruction at the end of another that sends the player back to the start or
to the sign (da capo, dal segno), to play on until the end, until "Fine", or
until "To Coda", where they leave for the coda. Pop and rock transcriptions use
them constantly ("D.S. al Coda" after the second chorus), Guitar Pro files
store them with a second set (segno segno, double coda), and
MusicXML writes them as `<segno/>`, `<coda/>` and `<sound>` attributes. The
importers dropped them with a warning, so a song played straight through and
the scope's "Coda/Segno" was missing.

## Decision

**In the model.** Like repeats, navigation marks are song-wide and live in
`MeasureHeader`, so they move with their measure:

- Signs that open the measure, where jumps go: `segno`, `segnoSegno`, `coda`,
  `doubleCoda`.
- Marks that close it: `fine`, `toCoda`, `toDoubleCoda`, and one jump: `jump`
  says where it goes (`JumpTo::Start` for da capo, `Segno`, `SegnoSegno`) and
  `jumpThen` what ends the music after it (`JumpThen::End`, `Fine`, `Coda`,
  `DoubleCoda`). Every combination Guitar Pro offers ("D.S.S. al Fine", "D.C.
  al Double Coda") is one pair; "To Coda" is a mark of its own because it sits
  on another measure than the jump.

They are plain fields, not a list of directions: a measure has at most one of
each, and the header stays a value that `operator==` compares.

**The play order.** `Song::playOrder()` follows the reading of ADR-0016 and,
when a played measure is done (after its repeat, if it closes one):

1. Once a jump was taken, at a measure marked fine the music ends (al Fine),
   and a measure marked to coda (al Coda) or to double coda (al Double Coda)
   continues at the first measure after it with that coda sign. With no such
   sign the music plays on.
2. A jump not taken yet goes to the first measure (da capo) or to the nearest
   measure at or before it with its sign. Without one it is not taken.

Each jump is taken once. After a jump repeats are not taken again and a run of
alternate endings plays only its last ending, as players read "D.S." without
"con ripetizione". Fine and To Coda mean nothing before a jump. The order is
still capped at 256 times the measures.

**In files and the CLI.** `.elara` stores the signs as flags (`"segno":true`)
and the jump as `"jump":"dal-segno"` with `"jumpThen":"al-coda"`, both left out
when unset, so format version 1 still reads every older file (ADR-0006).
`elara-cli edit` sets them with `sign <m> <segno|segno-segno|coda|double-coda|
fine|to-coda|to-double-coda>... | none` and `jump <m> <da-capo|dal-segno|
dal-segno-segno|none> [al-fine|al-coda|al-double-coda]`, one undo step each
("Change navigation marks"); `dump` and `edit` report them per measure and
`sequence` plays them.

The importers and the notation follow in their own changes: Guitar Pro and
MusicXML read and write the marks, and the score draws the signs above the
system and the jump texts at the end of their measure.

## Consequences

- The sequencer, the playback cursor and offline rendering follow jumps without
  changes: they all read `playOrder()`.
- A measure can carry a sign and a jump at once, as files do (a segno on the
  measure that also says "To Coda").
- Repeats taken again after a jump ("con ripetizione") are not supported; no
  format we read marks them.
