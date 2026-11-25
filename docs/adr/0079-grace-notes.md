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

**Drawing.** Grace notes are drawn at 65% of their staff's size, in the order
they are played, left of the chord they lead into:

- Standard notation: small noteheads, accidentals and ledger lines; stems up,
  or down in a voice pointing down; a small flag each or, for two or more
  flagged grace notes of one beat, small beams across them; a slash through
  the stem of a grace before the beat (an acciaccatura), none for one on it
  (an appoggiatura). Their accidentals count before their chord's: a grace F
  sharp before an F makes it need a natural.
- Tablature: small fret numbers on their strings, joined to their note by
  the slides and hammer-on arcs drawn between any two notes.
- The column of their chord reaches left over them, so spacing makes room for
  them where the beat before leaves too little; they take no time and add no
  column.

**Playback.** A grace note sounds for its written value, taken from the beat
it borrows from:

- A grace before the beat ends where the beat begins and cuts short the notes
  of the beat before, which end where the first grace before the beat begins.
- A grace on the beat starts with it and delays the beat's own notes, which
  keep their end.
- The grace notes of each side take at most half of the beat they borrow
  from: when they would take more, all of them are shortened alike.
- Grace notes borrow no time from the measure before: repeats and jumps do
  not always play it just before. A grace before the first beat of a measure
  is played on it, which is how most notation programs play every grace note.
- `SoundingNote::grace` says which grace of its beat a note belongs to, so
  playback finds its effects: a grace note slides, bends and rings like any
  note, with none of its beat's strum, tremolo picking or whammy bar. The MIDI
  export writes grace notes as it writes every sounding note.

## Consequences

- Guitar Pro and MusicXML grace notes survive import, saving and MusicXML
  export, are drawn in notation, tablature, PDF and SVG, and are heard in
  playback, audio rendering and MIDI export. Typing them at the caret comes
  next, recorded here when it lands. The caret and selection pass over them:
  they belong to their beat.
- A beat is 24 bytes larger on 64-bit systems: the list, empty for almost
  every beat, allocates nothing.
- Grace notes after the last beat of a voice have nothing to lead into and
  are not kept.
- A sounding note stays 80 bytes: its note index shrank to 16 bits to make
  room for its grace's.
- Tests: `io.MusicXML: grace notes lead into the next note of their voice`,
  `io.MusicXML export: grace notes on a guitar keep their strings and slides`,
  `core.Grace notes borrow their time from the beat before or their own`,
  `core.A grace note before the first beat of a measure plays on it`,
  `cli.sequence_grace_before_beat`, `cli.sequence_grace_on_measure_start`,
  `notation.Staff layout: grace notes are drawn small before their chord`,
  `notation.Tablature layout: grace notes are small fret numbers before their note`,
  the render snapshots of the corpus
  and the round trips of the corpus and of a song with every feature (a
  grace before a beat and a chord of them on one).
