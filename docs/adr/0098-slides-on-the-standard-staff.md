# 0098. Slides on the standard staff

- Status: Accepted
- Date: 2026-11-25

## Context

Slides are note effects: a shift or legato slide to the next note on the
string (`SlideOut::Shift`, `SlideOut::Legato`), a slide out of a note
downwards or upwards, and a slide into a note from below or above
(`SlideIn`, ADR-0070). Tablature draws them as lines between and beside fret
numbers, but standard notation drew nothing: a Guitar Pro file shown in
notation lost every slide, and only the slur of a legato slide was drawn
(ADR-0097).

Guitar engraving draws a slide as a straight line between the noteheads, a
slide into a note as a short line rising or falling to its notehead from the
left, and one out of a note as a short line leaving it to the right; a line
cut by a system break goes on from the start of the next system.

## Decision

**Standard notation draws slides as lines.** A note that slides to the next
note on its string (or, where strings are not known, to the nearest note in
pitch of the next beat of its voice) gets a straight line from just right of
its notehead and dots to just left of that note's notehead or accidental,
from the middle of one to the middle of the other; across a barline too. As
with legato (ADR-0097), only the next beat counts. A line that a system break
cuts goes to the end of the system, three quarters of a space up or down
towards the note it slides to, and on from the start of the next system,
coming from three quarters of a space below or above that note.

**Into and out of a note.** A slide into a note is a line 1.2 spaces long
rising (from below) or falling (from above) three quarters of a space to the
left of its notehead or accidental; a slide out of it is such a line leaving
it to the right, falling or rising. The chord's ink reaches over them, so the
beats around make room.

**Grace notes.** A grace note that slides into its note gets a line from it to
that note, and stands a little further from its chord to make room for it.

**Nothing else changes.** The model, the formats and playback keep slides as
note effects; tablature keeps its lines. A MusicXML glissando, read as a
legato slide, is drawn straight, not wavy.

## Consequences

- Guitar Pro and MusicXML files shown in standard notation draw their slides,
  in every layout (lines, pages, strips, SVG, PDF and the app).
- Lines are the `slide` role in the display list, as on tablature.
- A slide to a note several beats later, past notes on other strings, is drawn
  on the tablature only.
- Tests: the `notation.Slides: ...` layout cases and the render snapshots of
  the Guitar Pro corpus files.
