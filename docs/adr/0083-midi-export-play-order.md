# 0083. MIDI export writes the play order

- Status: Accepted
- Date: 2026-11-25

## Context

MIDI has no repeats, alternate endings or jumps: a Standard MIDI File is one
run of time. The MIDI export (ADR-0009) wrote each measure once, in written
order, with no warning, so a song with a repeated chorus came out shorter than
it plays, and its first and second endings both sounded, one after the other.
Playback and audio rendering already go through `Song::playOrder()` (ADR-0016,
ADR-0049): the file disagreed with what Elara plays.

## Decision

**The file holds the measures as they are played.** The export lays out
`Song::playOrder()`: every played measure starts where the ones before it, as
played, end. A note of written measure m played as measure k moves by the
difference between where k is played and where m is written. A note held past
its measure goes on over the measures played after it, as playback holds it.
When the order is not every measure once, as written, the export warns
`repeats and jumps written as played`; a song played straight through gives
the same file as before.

**The conductor follows the play order.** For each played measure:

- a meter or key event when it differs from the measure played before, and the
  measure's section marker every time it is played;
- the measure's tempo changes, moved with it;
- after a jump (the measure played before is not the one written before), the
  tempo in force where the music lands, unless the measure starts with a tempo
  change or the measure played before ends at that same steady speed;
- inside a gradual change (ADR-0082), a step at the barline, then the steps of
  the ramp, cut at the measure's end, so every barline falls at its written
  time whatever was played before.

Warnings about a tempo (beat units, rounding) name the written measure once,
however many times it is played.

Reading the file back gives the unrolled song: the importer has no way to tell
a repeated passage from music written out twice, and does not try.

## Consequences

- MIDI files of songs with repeats and jumps play as long as Elara plays them,
  and other programs hear the same song.
- A round trip through MIDI unrolls repeats; the warning says so.
- Steps of a gradual change whose sixteenths do not meet a barline get an
  extra step there, also for songs played straight through.
- Tests: `io.MIDI export writes repeats and jumps as they are played` (a repeat
  with first and second endings, a tempo change, a section and a meter change:
  six measures read back, the tempo set again after the repeat, the length in
  seconds), `io.MIDI export keeps the timing of a gradual tempo change over a
  repeat` (every played measure lasts as long as written, landing inside the
  ramp off its sixteenths) and `io.MIDI export of a song played straight
  through warns of nothing`.
