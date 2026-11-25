# 0092. Pedal marks

- Status: Accepted
- Date: 2026-11-25

## Context

A piano's sustain pedal lifts the dampers: every note struck or still sounding
while it is down rings on until it comes up. Scores mark it under the staff,
either "Ped." where it goes down and a star where it comes up, or "Ped." with a
line along its foot, a hook up where the pedal comes up and a notch where it is
changed (lifted and pressed straight down again).

- MusicXML writes `<pedal type="start|stop|change|continue|resume|discontinue|sostenuto"/>`
  in a `<direction>`, with `line` and `sign` for the style and `<staff>` for the
  staff it is under. The importer warned "pedal marks dropped".
- Standard MIDI Files send the pedal as controller 64: 64 or more is down.
- SMuFL has `keyboardPedalPed`, `keyboardPedalUp` and the engraving default
  `pedalLineThickness`.

## Decision

**A beat can be under the pedal**: `BeatEffects::pedal`, a `Pedal` (`None`,
`Down`, `Change`), on notes and rests alike. Beats under it in a row share one
pedalling, in any staff and voice of the track; `Change` lifts the pedal and
presses it again where its beat starts. `pedalSpans()` gives a measure's
pedallings (`PedalSpan`: start, end, whether it starts with a change), one
staff's or the whole track's. The field takes a byte `BeatEffects` had as
padding: `Beat` stays 144 bytes.

**Played**: every note of the track still sounding when the pedal goes down,
or struck while it is down, rings on until it comes up, never shorter, as let
ring does (ADR-0022). A pedalling goes on into the next played measure only when
that is the next written one: a repeat or a jump lifts the pedal.

**Kept** by the native format as the beat effect `"pedal"`: `"down"` or
`"change"`. MusicXML reads start, resume, change, stop and discontinue on every
voice of their staff, the pedal staying down from one measure to the next, and
writes a line with "Ped." (`line="yes" sign="yes"`) once per staff: start before
the first beat, change before a changed beat, stop after the last. Sostenuto
marks warn "sostenuto pedal marks dropped". MIDI export writes controller 64 at
127 where a pedalling starts and 0 where it ends. MIDI import reads it back, 64
or more being down: each pedalling, on the grid the notes are quantized to,
goes on the beats it reaches in the first voice of the lowest staff with notes
in each measure (rests included, which fill the measure); a beat reached by a
later pedalling than the beat before changes the pedal, so a pedal lifted and
pressed again around a beat, as played, becomes a change there.

**Drawn** on standard notation under the staff, below the ink and the dynamics:
"Ped." at the first beat, a line along its foot to just before the beat where
the pedal comes up, or to the barline when it stays down into the next measure,
and a hook up; a notch peaking over each change. A pedalling runs over barlines;
one going on into the next system runs to the end of the line without a hook,
and the next line starts with "Ped." again. The notes of a measure spread so that
a notch or the end of the line is never under "Ped.", as they do for syllables
(ADR-0084). Tablature draws no pedal.

**Edited** with the CLI's `pedal <down|change|none>` and a button in the app's
entry bar that goes down, changes and comes up on successive clicks.

## Consequences

- A pedal edit changes how its measure is laid out, and the neighbour measures a
  line runs into: the incremental layout lays those out again as for any beat
  effect.
- The sequencer and MIDI export read the pedallings of every measure of a track
  with pedal marks; tracks without any skip that work.
- A pedal played in a measure with no notes in its track has no beat to go on:
  it comes up at the barline before.
- Not covered: the star style ("Ped." and "*" without a line), half pedalling,
  and the sostenuto and soft pedals.
