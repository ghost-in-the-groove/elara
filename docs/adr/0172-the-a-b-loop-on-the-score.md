# 0172. The A-B loop on the score

- Status: Accepted
- Date: 2026-11-28

## Context

The A-B loop (ADR-0024) is marked at the playback position, A then B, and
shown only as a band on the song string under the command bar. Practising a
passage means reading it on the score while it loops, and the score said
nothing of where the loop starts and ends: the song string is a line for the
whole song, where a loop of four measures in a long song is a few pixels.

## Decision

- **A band over the loop's measures on the pages**, as the song string draws
  it: a rounded band just over the top staff of each system the loop goes
  through, from the first of its measures on that system to the last. While
  only A is marked, the band covers A's measure, fainter.
- **Whole measures**: from the measure A is in to the measure B is in, B at
  the very start of a measure ending the loop with the measure before. The
  transport loops by time, so a loop marked within measures shows the
  measures it touches; the song string keeps the exact times.
- `ScoreDocument::measureSpans(page, first, last)` gives a rectangle a system
  for a range of measures on a page; the score view draws one band each.

## Consequences

- The band, the shaded measure (ADR-0170) and the selection are three marks
  of three kinds: a band over the systems for the loop, a light shade across
  the measure playing or the caret's, a tint with an outline for the
  selection.
- The smoke test marks A, then B, checks the bands against `measureSpans` and
  ends the loop (`--follow`).
