# 0099. Ghost notes in parentheses

- Status: Accepted
- Date: 2026-11-25

## Context

A ghost note (`NoteEffects::ghost`) is played very softly: a muted rake on a
guitar, a ghost stroke on a snare. Guitar Pro marks them, MusicXML writes
them as `<notehead parentheses="yes">`, MIDI import finds them among drum
hits, and tablature draws their fret numbers in parentheses, but standard
notation drew them as plain notes: the softness was lost on the page.

Engraving puts the notehead of a ghost note between parentheses, the
accidental outside them on the left and the dots after them; on drum staves
too.

## Decision

**Standard notation draws ghost notes between parentheses.** The SMuFL
parentheses for noteheads go round each ghost note's notehead, a small gap
from it, level with it; only the ghost notes of a chord get them, grace notes
too (at their size). When a chord has a ghost note, its accidentals stand
left of the parentheses and its dots after them, ties and slides start after
and end before them, and the chord's ink takes them in, so the beats around
make room. A `parenthesis` role marks them in the display list.

**Nothing else changes.** The model, the formats and playback are as they
were; tablature keeps its parenthesized fret numbers.

## Consequences

- Ghost notes read as such in every layout of standard notation (lines,
  pages, strips, SVG, PDF and the app), on drum staves as well.
- `elara-cli render --json` counts a new role, `parenthesis`.
- Tests: the `notation.Ghost notes: ...` layout cases, the role counts of the
  `cli.render_*` cases, and the render snapshot of the MIDI drums corpus file.
