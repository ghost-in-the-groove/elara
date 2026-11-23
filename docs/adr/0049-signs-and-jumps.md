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

**In Guitar Pro and MusicXML.** Guitar Pro 5 lists the measure of each of its
nineteen directions after the song header, and GPIF writes them as a master
bar's `<Directions>` (`<Target>` for a sign, `<Jump>` for the others); each sets
one field, Guitar Pro's "Da Coda" being "To Coda". MusicXML has no single
element for them, so the reader takes what notation programs write: the
`<sound>` attributes (`segno`, `coda`, `tocoda`, `fine`, `dacapo`, `dalsegno`),
the `<segno/>` and `<coda/>` glyphs of a direction or a barline, and the usual
texts ("D.S. al Coda", "Da Capo", "Fine", "To Coda"). A jump given only by its
`<sound>` does not say how it ends: it is read al Coda when the song has a To
Coda, else al Fine when it has a Fine. The writer puts the glyph or text with
its `<sound>` in the first part and names the second signs `segno-segno` and
`double-coda`; other readers only need a jump's name to match its sign's.

**In the score.** The marks are drawn above the first staff, over the
alternate-ending brackets and under the tempo marks (`Role::Navigation`):
segno and coda glyphs from the music font at the start of their measure (two
side by side for segno segno and double coda), and the texts that close a
measure in italics, ending at its barline: "Fine", "To Coda", "To Double Coda"
and the jump ("D.C.", "D.S.", "D.S.S.", with " al Fine", " al Coda" or " al
Double Coda"), the jump last. They share one line per system; a text that
would overlap another mark goes up to the first line where it fits, and the
signs keep the lowest line.

## Consequences

- The sequencer, the playback cursor and offline rendering follow jumps without
  changes: they all read `playOrder()`.
- A measure can carry a sign and a jump at once, as files do (a segno on the
  measure that also says "To Coda").
- Repeats taken again after a jump ("con ripetizione") are not supported; no
  format we read marks them.
