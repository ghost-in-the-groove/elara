# 0051. Measures shared between copies of a song

- Status: Accepted
- Date: 2026-11-24

## Context

The app keeps an immutable copy of the song for everything that reads it off
the interface thread: layout, the sequence, painting (ADR-0031). It takes that
copy after every edit, on the interface thread, and copying the 20-track,
500-measure benchmark song took 11.6 ms: most of a 60 Hz frame for typing one
fret. An edit changes one measure, or a few; the copy duplicated all 10,000.

ADR-0005 turned down persistent data structures for undo because every model
type would become a handle with reference counts. Undo is not the problem here
(commands stay as they are); the snapshot is.

## Decision

**A staff holds its measures as shared values.** `SharedValue<T>` keeps a `T`
behind a reference count: copying it shares the `T`, and the first change
through a copy that shares it gives that copy its own `T`. `Staff` stores a
`SharedValue<Measure>` per measure, so copying a song copies 16 bytes per
measure instead of its voices, beats and notes.

**Nothing outside `Staff` changes.** `Staff::measure()` already was the only
way to a measure: the const overload reads the shared measure, the non-const
one copies it first when another song shares it. Measures stay plain values
(ADR-0004); headers, tracks and staves are still copied, being small.

**Threads.** Copies are taken from an object one thread owns and read on
others; a thread only changes measures of its own copy. A measure is changed
in place only when no other copy holds it (an acquire fence orders the
other copy's reads before the change), so a song read on a worker never sees
an edit.

**Empty measures are shared too.** Measures inserted together, or the
measures of a new track, start as one empty measure.

## Consequences

- Copying the benchmark song takes 63 µs instead of 11.6 ms, and inserting a
  measure at its start 17 µs instead of 260 µs (16-byte handles shift instead
  of 96-byte measures).
- Building a song costs an allocation per measure the first time it changes:
  the benchmark song builds in 14.4 ms instead of 13.1.
- Snapshots that differ by one edit share every other measure, which also
  saves memory while layout, sequence and painting hold different versions.
- Two copies can now tell an unchanged measure by identity instead of by
  content, which incremental relayout can build on.
- A non-const `measure()` call made only to read copies a shared measure; code
  that reads should go through a const song.
