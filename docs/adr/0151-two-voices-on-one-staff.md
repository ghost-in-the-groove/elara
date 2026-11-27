# 0151. Two voices on one staff

- Status: Accepted
- Date: 2026-11-27

## Context

A staff holds up to four voices (ADR-0004). With several, the first and third
point their stems up and the others down, and their rests move up or down
(ADR-0012). Their notes, though, were all placed on their column's x: where
two voices had notes starting together a second apart, or in unison with
different noteheads, the noteheads were drawn into each other. ADR-0012 listed
it as not covered. The layout issues of ADR-0150 find it on a song written for
it: it is a fault of the automatic layout, and no writer should have to fix it
by hand.

Engraving practice is the same in the references: Elaine Gould, *Behind Bars*
(Faber Music, 2011), on two parts sharing a staff, and the behaviour LilyPond
documents for collisions of voices (Notation Reference, "Collision
resolution"):

- notes a second apart are written side by side, the upper part's note on the
  left, so that the two stems stand in one line;
- a unison of the same notehead and the same dots is one notehead, both stems
  on it;
- a unison of different noteheads (a quarter against a half) or different dots
  is written side by side, the lower part's note on the right, clear of the
  upper one's dots.

## Decision

- After the chords of a measure are shaped, each column whose chords come from
  several voices is looked at. A chord of a voice with stems down moves right
  of its column (`Chord::shift`) as far as the chords of voices with stems up
  that start with it need:
  - nothing when every note of one is a third or more from every note of the
    other, or their unisons share their notehead (the same glyph and dots, not
    moved aside inside their chord, neither a ghost note);
  - a second apart, with stems on both and no dots on the upper chord: until
    its stem stands on the upper chord's stem;
  - otherwise (a unison of different noteheads or dots, a unison and a second
    together, notes without stems, dots or noteheads moved aside inside their
    chord): right of the upper chord's noteheads, and of its dots with a dot's
    gap after them.
- The chord that moves takes its noteheads, dots, ledger lines, stem, flag,
  ties, slurs and marks with it, since they are all drawn from its x. Its
  accidentals (and the fingerings left of them) go back left of the upper
  chord's ink.
- The column's ink is the union of its chords' ink as they stand, so spacing
  makes room for the chord that moved. The column's x, where its caret, lyrics
  and dynamics are, does not change.
- Tablature is not affected: frets of different voices are on their own
  strings.
- Only columns with chords of several voices are looked at, in one pass over
  their few chords, with nothing allocated.

## Consequences

- No notehead of two voices overlaps another: the layout issues of ADR-0150
  find none on the cases above, and the corpus stays clean.
- A dotted note of the upper voice a second above the lower voice's note puts
  the lower one right of the dot, wider than engravers who let the dot touch
  it; an upper voice with a note a step under the lower voice's (voices
  crossed) is treated as a second.
- Grace notes of the chord that moves move with it and may stand over the
  upper chord's noteheads; not handled yet.
- Three or four voices are handled pairwise, upper against lower; two voices
  with stems in the same direction are not moved.
