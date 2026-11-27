# 0113. Tablature for reading: larger numbers, and the time signature only when it stands alone

- Status: Accepted
- Date: 2026-11-26

## Context

A player reads tablature from arm's length, with the instrument in the way.
ADR-0013 set the fret numbers at 1.3 staff spaces and the texts of the
techniques ("P.M.", "let ring", the harmonic labels) at 0.95, the "H" and "P"
over an arc at 0.85 and the amount over a bend at 0.95. On paper at a staff
space of 1.75 mm that is type of 6.4 pt for the numbers and 4.2 to 4.7 pt for
the rest; on screen at paper size, 8.6 and about 6 pixels. The numbers were the
smallest thing on the page that had to be read, and the texts could not be.

Looking at how tablature is printed and drawn elsewhere:

- MuseScore sets fret numbers at 9 pt on lines 1.5 staff spaces apart (about
  1.8 staff spaces at its default size).
- Songbooks and the programs our users come from print "P.M." and "let ring"
  at about the size of fingering, 6 to 7 pt, never below.
- When a tablature staff hangs under a standard staff, the time signature is
  printed once, on the standard staff. MuseScore's linked tablature leaves it
  out by default, and so do printed guitar editions; a tablature staff that
  stands alone carries it.

ADR-0013 drew the time signature on every tablature staff.

## Decision

ADR-0131 put every text of the score on one scale and raised these sizes
again; the time signature's place stands as decided here.

- **Fret numbers at 1.6 staff spaces** (7.9 pt on paper): their capitals are
  1.15 staff spaces high between lines 1.5 apart, so a number still clears the
  lines above and below it. Grace numbers stay at 0.65 of that.
- **Technique texts at 1.25** (6.2 pt), the letters over an arc at 1.05, the
  amount over a bend at 1.2 and the whammy bar's label at 1.1. The lanes that
  hold them grow by what the text grew, so nothing touches the lane above.
- **Chord diagrams four tenths larger**: strings 0.85 staff spaces apart and
  frets 1.0 (they were 0.6 and 0.7), the dots and the open and muted marks at
  0.95 of the font's size, the first fret's number at 1.2. A dot now fills
  most of the room between two strings, so the string it is on, and an open
  string from a muted one, can be told at 100 %.
- **The time signature is the standard staff's.** A tablature staff under the
  standard staff of the same instrument leaves it out, at the start and at
  every change; the measures of both staves still start together, the
  tablature's header ending where the notation's does. A tablature staff
  alone draws it as before.

## Consequences

- A line with chord diagrams is 1.3 staff spaces higher, and two diagrams
  over close beats push those beats a little further apart.
- A system with many fretted notes is a little wider: two-digit frets take
  more room, and a long song may break its lines elsewhere.
- Notation and tablature together are a little higher where techniques are
  written, by the growth of their lanes.
- The corpus snapshots changed for every file with tablature; they were
  regenerated and looked at.
- The sizes are constants of the layout (`tab_staff.cpp`,
  `layout_common.cpp`); a style the user can change would replace them.
