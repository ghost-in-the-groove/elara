# 0042. Tempo marks

- Status: Accepted
- Date: 2026-11-23

## Context

Every song has a starting tempo and many change it, and playback has followed
those tempos since the sequencer (ADR-0017), but the score never said what
they were. Printed music and every score editor write a tempo mark above the
first staff: a note for the beat unit, "=", and the beats per minute.

## Decision

**Where.** Above everything over the first staff of a system (its ink, repeat
counts and the brackets of alternate endings), all marks of a system on one
baseline: at least 1.5 spaces above the top line and 0.6 spaces clear of the
ink below. A mark starts at the column where its tempo takes effect: the first
note of the song for the starting tempo, the column at a change's offset
inside its measure (or in proportion when no column starts there).

**When.** The tempo the song starts with (or a change at the very start, which
replaces it), and every change to a tempo different from the one in force
before it; a change to the same tempo writes nothing.

**What.** The beat unit's metronome note from the music font (`metNoteWhole`
to `metNote32ndUp`, with `metAugmentationDot` for a dotted unit), then
"= 120" in the text font (a tenth of a beat when the tempo has one: "= 92.5").
The note is drawn at 0.65 of the staff's size so it matches the text, with its
head on the text's line.

**Scaled glyphs.** `GlyphItem` gains `scale` (1 for every glyph drawn so far).
Ink extents, the SVG writer (a `font-size` on the glyph) and the app's
painter honour it; `elara-cli render --json` writes `"scale"` only when it is
not 1. Grace and cue notes will use it too.

**Output.** The note, its dot and the text carry a new `Role::Tempo`
("tempo"), so `render --json` counts them.

## Consequences

- Every rendered song gains its starting tempo, so the first system of every
  page layout is a little taller; the render snapshots change accordingly.
- Tempo words ("Allegro", "rit.") and gradual changes are not in the model yet
  and are not drawn.
- Tests: `notation.Tempo marks: ...` (the starting tempo over the first note,
  a change over its column and on the system's line, the same tempo again, a
  change at the very start, dotted and half-note units, a fractional tempo),
  and `cli.render_both` counts the corpus file's mark.
