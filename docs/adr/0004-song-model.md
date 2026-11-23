# 0004. Song model

- Status: Accepted
- Date: 2026-11-23

## Context

Tablature, standard notation, the piano roll and playback all read one model
(golden rule 1). Its shape decides how importers map Guitar Pro and MusicXML
files, how commands address what they change, and what the native format stores.
Guitar players expect tablature to keep the string and fret they chose, while
notation needs spelled pitches (F# is not Gb).

## Decision

**Hierarchy.** `Song > Track > Staff > Measure > Voice > Beat > Note`:

- `Song` holds the metadata (`SongInfo`), the tracks and one `MeasureHeader` per
  measure. Headers are song-wide: every staff has the same number of measures and
  they share time signature, key signature, repeats and markers. The `Song` API
  keeps that invariant: measures are inserted and removed in all staves at once.
- `Track` is one instrument (`Instrument`: kind, MIDI program, written
  transposition) with one or more staves (a piano has two).
- `Staff` holds the measures and, for fretted instruments, the `Tuning` and capo.
  Tablature is a *view* of a fretted staff, not a separate staff type.
- `Measure` holds up to four `Voice`s. A voice is a sequence of `Beat`s; a beat's
  start is the sum of the durations before it, so positions are derived, never
  stored.
- `Beat` has a `Duration` and the `Note`s that start together; no notes means a
  rest.

**Pitch of a note.** A `Note` stores its *sounding* (concert) `Pitch`, spelled,
plus an optional `FretPosition` on fretted staves. When both are present the
position must sound the pitch under the staff's tuning and capo. Written pitch
(guitar notation is an octave above the sound, a B-flat clarinet a tone above)
comes from `Instrument::writtenOffset` at display time. Percussion notes store
the General MIDI percussion key as the pitch's MIDI number; where they sit on the
staff and their notehead come from a drum map (see `docs/instruments.md`).

**Addressing.** Commands address objects by index path (track, staff, measure,
voice, beat, note). Undo restores the exact previous structure, so indices stay
valid along the undo stack.

**Value types.** Model objects are plain C++ values (copyable, comparable with
`==`) owned by the `Song`. No pointers between objects and no Qt or JUCE types.

## Consequences

- Retuning a staff is a command that either keeps frets (pitches change, as Guitar
  Pro does) or keeps pitches (positions are recomputed); both are possible because
  the note stores both.
- Polymeter (different time signatures per staff at the same time) is not
  supported. It is rare in guitar music and can be added later with per-staff
  header overrides.
- Many small vectors (notes per beat, beats per voice). Large songs stay well
  within memory (tens of thousands of beats); if profiling shows allocation
  pressure, beats can move to a per-measure arena without changing the API.
- Index paths make commands simple but forbid holding references across edits;
  the UI keeps index paths, not pointers.
