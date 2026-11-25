# 0079. Grace notes

- Status: Accepted
- Date: 2026-11-25

## Context

A grace note is a small note played just before a beat, or on it, that takes
no time in the measure's rhythm: it borrows its time from the beat before or
from the beat it leads into. Guitarists write them for quick slides and
hammer-ons into a note; keyboard and wind parts write acciaccaturas and
appoggiaturas.

- Guitar Pro 6-8 files store a grace note as a beat of its own, with
  `<GraceNotes>BeforeBeat</GraceNotes>` or `OnBeat`, a rhythm (its written
  value) and notes, placed before the beat it leads into. Several can follow
  each other and each can be a chord.
- Guitar Pro 3-5 files store one grace note per note: a fret, a dynamic, a
  transition (none, slide, bend, hammer), a duration and, in version 5, an
  on-beat and a dead-note flag.
- MusicXML writes `<grace/>` notes (`slash="yes"` for an acciaccatura) with no
  duration before the note they lead into, as chords when they are chords.

Every importer dropped them with a warning.

## Decision

**Grace notes belong to the beat they lead into.** `Beat::graces` lists them
in the order they are played; each `Grace` has a written value (`duration`:
an eighth, a sixteenth), its notes (one, or a chord) and `onBeat`: an on-beat
grace borrows its time from its own beat, the others from the beat before.
A grace takes no time, so a voice's beats, their starts and lengths, the caret
and every edit stay as they are: copying, moving or deleting a beat takes its
grace notes with it. A note of a grace can have its own effects (a slide out
of it, a dead note, a dynamic). The beat's first note hammered on or pulled
off (`legatoFromPrevious`) is legato from the last grace when the beat has
one, as a guitarist plays it.

**Files.**

- Guitar Pro 6-8: the beats with `<GraceNotes>` before a beat become its
  graces. Grace beats with no beat after them in their voice are dropped with
  a warning.
- Guitar Pro 3-5: the grace notes of a beat's notes become one grace, with
  each grace fret on the string of its note; a slide transition slides out of
  the grace note, a hammer transition makes the note legato from it, a bend
  transition is dropped with a warning.
- The native format writes a beat's `"graces"` as a list of
  `{"duration", "notes", "onBeat"}` and omits it when empty; format version 1
  still reads every older file (ADR-0006).
- MusicXML: `<grace>` notes lead into the next note of their voice and staff,
  a `<chord/>` grace joining the grace before it. A slashed grace (an
  acciaccatura) or one with `steal-time-previous` is played before the beat,
  the others on it; export writes `<grace slash="yes"/>` for the first and
  `<grace/>` for the second, with the notes' strings and frets and no
  duration. A grace note's dynamic is the one in force at its beat.
- The MIDI export reports grace notes as dropped until playback plays them.

## Consequences

- Guitar Pro and MusicXML grace notes survive import, saving and MusicXML
  export. Drawing them as small notes and fret numbers, playing them by
  borrowing time and typing them come next, each recorded here as it lands. Until they are
  drawn and played, a song with grace notes looks and sounds as it did before
  they were kept.
- A beat is 24 bytes larger on 64-bit systems: the list, empty for almost
  every beat, allocates nothing.
- Grace notes after the last beat of a voice have nothing to lead into and
  are not kept.
- Tests: `io.MusicXML: grace notes lead into the next note of their voice`,
  `io.MusicXML export: grace notes on a guitar keep their strings and slides`
  and the round trips of the corpus and of a song with every feature (a
  grace before a beat and a chord of them on one).
