# 0013. Tablature layout and staves aligned in systems

- Status: Accepted
- Date: 2026-11-23

## Context

A guitar score is read as standard notation over tablature, the two lined up
note for note (ADR-0004: tablature is a view of a fretted staff, not a second
copy of the music). ADR-0012 lays out one staff of standard notation on its
own. Tablature needs its own drawing (fret numbers on string lines) and the
guitar techniques the model holds (bends, slides, hammer-ons, palm mutes...),
and both staves must share one horizontal spacing or their notes drift apart.

## Decision

**Staff writers and one spacing pass.** Each staff of a system has a writer
(standard notation or tablature) that reads a measure and reports the ink of
its columns: the notes that start together, with how far their ink reaches left
and right of the column. The system merges the columns of all its staves by
start time and spaces them once with the rules of ADR-0012, then every writer
draws at the shared positions. Headers, signature changes and barlines take the
widest staff. `layoutSystem()` stacks the staves top to bottom, at least 4 staff
spaces apart between their lines and 1 staff space apart between their ink;
`layoutStaff()` is a system of one standard staff.

**More items in the display list.** Text (fret numbers and technique marks, in
the renderer's sans-serif font), cubic Bézier curves (ties, hammer-on arcs and
bends, drawn as a lens when the middle is thicker than the ends), filled
polygons (arrowheads) and dashed lines. Layout sizes text with the advance
widths of a Helvetica-like font (`textWidth()`) and centres digits on a line by
their cap height (0.72 em).

**Tablature.** One line per string, 1.5 staff spaces apart, string 1 on top; a
TAB clef (six-string glyph from five strings up, four-string below) and time
signatures, no key signature. A fret number is centred on its string line and
under the centre of the notehead standard notation draws for the same note, at
1.3 staff spaces, and clears the line behind it. A note without a fret position
takes the lowest fret that sounds it. Dead notes are `X`, ghost and tied notes
are in parentheses, natural harmonics in angle brackets (`<12>`, `<3.2>`).
Rests show only when the system has no standard notation of the staff.

**Guitar techniques.** On the tablature:

- hammer-ons and pull-offs: an arc over the two numbers with `H` or `P`
  (chosen by the frets), legato slides with an arc and no letter;
- slides: a line between the two numbers that climbs or falls with the frets;
  slides into and out of a note: a short line before or after it;
- bends: a curve from the number up to an arrowhead, labelled `1/4`, `1/2`,
  `full`, `1 1/2`...; a prebend rises straight from the number; a release curves
  back down.

Palm mute (`P.M.`), let ring, vibrato, harmonics other than natural (`A.H.`,
`P.H.`, `T.H.`, `S.H.`) and pick strokes go into lanes above the staff, in that
order upwards after the bend lane; only the lanes a line uses take room. Palm
mute, let ring and vibrato run on over consecutive notes, across barlines, and
end with a dashed line and a hook (a wavy line for vibrato). The lanes go above
the tablature, or above the standard notation when the system has no tablature
of that staff.

**Standard notation.** Ties join tied notes to the same note of the previous
chord of their voice, curving away from the stem (in a chord, the upper half up
and the lower half down). Staccato, accent and marcato sit on the notehead side,
the staccato dot in a space when inside the staff.

## Consequences

- `elara-cli render --view standard|tab|both` shows a staff as notation,
  tablature or both, as JSON or SVG; tests check fret numbers, alignment and each
  technique through the display list.
- The engraving constants live in `layout_common.hpp` (spacing),
  `standard_staff.cpp` and `tab_staff.cpp`.
- A system of several tracks, line breaks and pages build on the same writers
  (ADR-0014).
- Not covered yet: rhythm stems under tablature shown on its own, slurs in
  standard notation, bends in standard notation, tremolo picking, whammy bar,
  tapping and slap marks, fingering, chord diagrams.
