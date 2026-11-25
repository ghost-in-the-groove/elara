# 0073. A brace and the barlines join a grand staff

- Status: Accepted
- Date: 2026-11-25

## Context

The scope (`docs/SCOPE.md`) includes piano on a grand staff. The model holds a
piano as one track with a treble and a bass staff (the `piano` preset,
MusicXML parts with `<staves>2</staves>`), and the notation engine lays them
out as two staves of a system, but drawn like the staves of two instruments:
each staff has its own barlines and nothing groups them. A reader cannot tell
a piano's two hands from two instruments, and the convention of engraved
music is a brace left of the two staves with barlines running through both.

The line that starts a system already joins all its staves. Barlines between
the staves of different instruments stay apart, and so do the notation and the
tablature of a guitar, which are one staff seen twice.

## Decision

**Two or more staves of one track shown in standard notation one after the
other are a grand staff: a brace stands left of them in every system and each
barline goes on down through the gap to the next staff.**

- The brace is SMuFL's `brace` glyph (U+E000) scaled to the height from the
  top line of the first staff to the bottom line of the last, 0.3 staff spaces
  left of the line starting the system, with the role `brace`.
- Every system with a grand staff starts 1.8 staff spaces further right, room
  the line breaker takes off the width of every system, so justified systems
  still end at the right margin. Track names stay left of the brace.
- The barlines of the staves above the last are extended with lines of their
  thickness from the bottom line of one staff to the top line of the next:
  plain, final and repeat barlines alike.
- Staves of different tracks, and the notation and tablature of one staff, are
  joined as before: only by the line starting the system.

## Consequences

- A piano reads as one instrument in the page and continuous views, the SVG
  output and the app, which draw the brace like any glyph.
- The brace's width follows its height (Bravura's brace is drawn for one
  staff): taller grand staves get a wider brace, up to about 1.5 staff spaces
  for 20 spaces of height, inside the room kept for it.
- Brackets grouping the staves of several instruments (an ensemble's
  sections) are not drawn; they would use the same room left of the systems.
