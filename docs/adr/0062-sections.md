# 0062. Sections of a song

- Status: Accepted
- Date: 2026-11-24

## Context

Songs are made of sections: an intro, verses, choruses, a bridge, a solo.
Guitar Pro files name them with markers (Guitar Pro 3-5) or section marks
(Guitar Pro 6-8), MusicXML with rehearsal marks and MIDI with markers; the
importers dropped them, and a score could not say where its chorus starts.

## Decision

**A section is a name on the measure header where it starts**:
`MeasureHeader::section`, empty when no section starts at the measure. It runs
until the next measure that names one; there is no separate list of sections
to keep in step with the measures.

- `changeSection(song, measure, name)` names it, one undo step ("Change
  section"); an empty name takes it off. A measure appended after the last one
  starts no section.
- The native format writes `"section"` only when a measure has one.
- **Guitar Pro 3-5**: the marker's name; its colour is dropped without a
  warning, as Elara chooses its own colours.
- **Guitar Pro 6-8**: the section mark's text, or its letter when it has no
  text ("A", "B").
- **MusicXML**: a `<rehearsal>` mark at the start of the measure, in the first
  part only, as the signs and jumps are (ADR-0049); on import, the first part's
  first rehearsal mark in the measure names it.
- **MIDI**: a marker meta event at the start of the measure; on import, the
  first marker that falls in a measure names it.
- `elara-cli edit "section <m> [name]"` names a section headlessly.

## Consequences

- Sections come in from the files that have them and go out to MusicXML and
  MIDI, so a song keeps its form through Elara.
- The score draws them (ADR-0063); the app cannot edit them yet, a separate
  task that reads the same field.
- A section name is free text: "Chorus 2" and "Chorus" are different sections
  to Elara, and nothing groups repeated sections.
