# 0089. Octave lines

- Status: Accepted
- Date: 2026-11-25

## Context

An octave line ("8va", "8vb", "15ma", "15mb") over or under a run of notes
says they sound one or two octaves above or below where they are written. It
keeps very high or very low passages near the staff instead of on stacks of
ledger lines. Elara dropped them:

- MusicXML writes `<octave-shift type="down|up|stop" size="8|15"/>` in a
  `<direction>`. Notes keep their true pitches: a treble passage under 8va is
  an octave-shift *down* from the pitches in its notes. The importer warned
  "octave lines dropped".
- Guitar Pro 6-8 put `<Ottavia>8va</Ottavia>` (or 8vb, 15ma, 15mb) on every
  beat under the line; frets do not change. The importer warned "octave signs
  dropped". Guitar Pro 3-5 have none.
- Engraving manuals put 8va and 15ma above the staff and 8vb and 15mb under
  it: the numeral over (or under) the first note, a dashed line to the end of
  the last one and a short hook towards the staff. A line cut by the end of a
  system runs to its end without a hook, and the next system repeats the
  numeral. SMuFL has `ottavaAlta`, `ottavaBassaVb`, `quindicesimaAlta` and
  `quindicesimaBassaMb`, and the engraving default `octaveLineThickness`.
- Lines of three octaves (22ma) exist but are rare.

## Decision

**A beat can be under an octave line**: `BeatEffects::ottava`, an `Ottava`
(`None`, `Alta`, `Bassa`, `QuindicesimaAlta`, `QuindicesimaBassa`).
`ottavaOctaves()` gives how many octaves the written notes move: 1, -1, 2,
-2 or 0. Like a hairpin (ADR-0078), the line runs over the beats that have it,
whichever voice they are in. The field takes a byte of its own, which pads
`BeatEffects` from 32 to 40 bytes and `Beat` from 136 to 144: packing it into
three bits next to `slur` saves those eight bytes, but GCC 14 then rejects
`BeatEffects x = {}` (an enum bit-field with a default member initializer
breaks copy-list-initialization), and a bit-field without one would leave
`BeatEffects x;` holding garbage.

**Pitches stay the sounding ones.** The model, playback, MIDI export and the
tablature are unchanged: only standard notation writes the notes under the
line one or two octaves nearer the staff, with the same spelling and key.
Rests keep their beat's line, so a rest inside a passage does not break it.

**Drawn** by the standard staff when the line closes: one line per run of
beats under the same octave line, merged over barlines. The numeral sits with
its left at the first beat's column, `kOctaveGap` off the ink under (or over)
the whole run. The dashes go through its middle to the end of the last
notehead, at least a staff space long, then a hook to the level of the
numeral's edge nearest the staff. A run going on into the next system ends at
the end of the line without a hook; the next system starts with the numeral
again. Lines are `octaveLineThickness` thick, with `Role::OctaveLine`
("octaveLine") for the numeral, dashes and hook.

**Kept** by the native format as the beat effect `"ottava"`: `"8va"`, `"8vb"`,
`"15ma"`, `"15mb"`. MusicXML reads octave-shifts of 8 and 15 on every voice of
their staff and writes them back, once per staff, starting and stopping where
the line changes; three-octave shifts warn "octave lines of three octaves
dropped". Guitar Pro 6-8 read `<Ottavia>`.

**Edited** as the other beat effects: the CLI's `ottava <8va|8vb|15ma|15mb|none>`,
and two buttons in the app's entry bar, one up and one down, where a second
click makes 8va 15ma (8vb 15mb) and a third takes the line off.

## Consequences

- An octave line changes how a measure is drawn, not what it plays: the
  incremental layout lays out again the systems around an edited measure, as
  for hairpins, and the measure widths of the measures that changed.
- Accidentals follow the written notes: the key and earlier notes of the
  measure are compared at their written positions.
- Not covered: three-octave lines, "loco" marks, the parenthesised numeral
  some editions repeat at a system's start, and octave lines on tablature.
