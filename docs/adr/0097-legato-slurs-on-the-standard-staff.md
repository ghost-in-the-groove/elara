# 0097. Legato slurs on the standard staff

- Status: Accepted
- Date: 2026-11-25

## Context

Hammer-ons, pull-offs and legato slides are note effects
(`NoteEffects::legatoFromPrevious`, `SlideOut::Legato`, ADR-0070). Tablature
draws them as arcs between fret numbers, with "H" or "P" over a hammer-on or
a pull-off, but standard notation drew nothing for them: a Guitar Pro file
shown in notation lost every legato, and a grace note hammered into its note
stood apart from it. Slurs of standard notation are beats slurred to the next
(ADR-0086), which Guitar Pro does not have.

Guitar engraving writes one slur in the notation over the notes a hammer-on,
a pull-off or a legato slide joins, a run of them under one slur, and leaves
"H" and "P" to the tablature; a grace note played legato into its note gets a
short slur to it.

## Decision

**Standard notation draws legato as slurs.** A beat is slurred to the next
beat of its voice when it is marked so (ADR-0086) or when a note of it goes on
legato to a note of the next on its string: it slides legato into it, or that
note is hammered on or pulled off from it. Notes without a string (off a
fretted staff) count as on any. The slur is drawn as a marked one, and a run
of legato notes and marked slurs in a row is one slur. Only the next beat
counts: a note that goes on legato to a note several beats later, past notes
on other strings, is not slurred in the notation (the tablature still draws
its arc).

**Grace notes.** When the notes of a beat go on legato from its last grace
note (hammered on, pulled off or slid into from it, as Guitar Pro writes a
grace note's transition), a short slur joins the grace note to them, on the
side away from the grace notes' stems, ending before the chord's stem where
that comes down on the same side. Such a beat is played legato from its grace
note, so the beat before is not slurred to it.

**Nothing else changes.** The model, the formats and playback keep legato as
note effects; MusicXML export still writes hammer-ons and pull-offs as
technical marks without slurs, so a file read back is the same song. Tablature
keeps its arcs.

## Consequences

- Guitar Pro files shown in standard notation slur their hammer-ons, pull-offs
  and legato slides, in every layout (lines, pages, strips, SVG, PDF and the
  app).
- A legato note several beats after its note on the same string is slurred in
  the tablature only.
- Tests: the `notation.Slurs: hammer-ons, pull-offs and legato slides slur
  their notes`, `notation.Slurs: a grace note played legato into its note is
  slurred to it` and `notation.Slurs: a grace note sliding legato into its note
  is slurred to it` layout cases, and the render snapshots of the Guitar Pro
  corpus files and `tests/corpus/musicxml/guitar-tab.musicxml`.
