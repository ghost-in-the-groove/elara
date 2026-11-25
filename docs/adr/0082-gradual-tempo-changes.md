# 0082. Gradual tempo changes

- Status: Accepted
- Date: 2026-11-25

## Context

A tempo change happened at once (ADR-0017, ADR-0042): Guitar Pro's linear
tempo points (an accelerando from 72 to 80, a ritardando from 85 to 55) came
in as sudden jumps with the warning "gradual tempo changes written as sudden
ones", and playback lurched where the music eases.

Guitar Pro 6 to 8 mark a tempo point `Linear`: from it, the tempo moves to the
next point's, reached where that point stands. It writes the point with the
tempo in force there, so the ramp starts where the music was.

## Decision

**A flag on the change, not a new event.** `TempoChange::gradual`: from this
change, the tempo moves to the next change's and reaches it at that change.
Gradual or not, every change still holds its own tempo, so marks, the measure
popup and `Song::tempoAt()` keep reading the written tempo; only time changes.
A gradual change with no change after it holds, as a sudden one would.

**Linear in speed along the score.** The speed, in whole notes a second
(`Tempo::wholesPerSecond()`, which makes changes of different beat units
comparable), moves linearly with the position in whole notes between the two
changes. The time of a stretch of such a ramp has a closed form:
`rampSeconds(speed, slope, distance) = ln(1 + slope * distance / speed) / slope`
(the plain `distance / speed` when the slope is zero). Being exact, times are
the same however a ramp is cut into measures, notes or clicks.

**Where it is read.** `Song::secondsAt()` and the sequencer's tempo map
(segments with a slope; ADR-0017) time everything from it: notes, measure
starts, the metronome, the playback speed (which scales both speeds of a ramp).
The count-in takes the speed where its measure starts.

**Formats.** Guitar Pro 6 to 8 read `Linear` points as gradual changes; a
linear point at the very start is the start tempo and a gradual change there.
The native format writes `"gradual": true`.

**MusicXML: words and dashes.** MusicXML has no gradual tempo, only words. The
export writes "rit." or "accel." (by the speeds of the two changes) in italics
in the direction of the change, then dashes that stop in the direction of the
next change. The import reads "accel.", "accelerando", "stringendo", "rit.",
"ritard.", "ritardando", "rall.", "rallentando", "allarg." and "allargando",
with "poco", "molto" or "poco a poco" before them, as a gradual change at that
onset: one with the tempo of its direction, else at the tempo in force. Only
when the part sets a tempo after them, within 64 measures and before "a
tempo", "tempo primo" or another such word: a ramp needs somewhere to go, and
words with nowhere to go stay the text of the beat (ADR-0081).

**MIDI: steps.** MIDI holds tempos at points only, so the export writes a ramp
as a tempo event every sixteenth note, and one at each barline inside the ramp
(ADR-0083), each with the steady tempo that lasts as long as that stretch of
the ramp (`rampSeconds`). The song keeps its timing, to
the microsecond a quarter note; read back, the steps are sudden changes.

A gradual change to the same speed, or with no change after it, has nothing
to write: both formats write it as a sudden one, with no warning.

**Drawing.** After the change's mark, or at its column when it keeps the
tempo in force (as Guitar Pro writes it), "accel." or "rit." in italics on the
tempo line, as the next change is faster or slower, then a dashed line to that
change, or to the end of the system; the systems the ramp crosses carry the
line on from their first measure. A ramp to the same speed, or with no change
after it, draws nothing more.

**Editing.** `changeTempoGradual()` makes the change that starts a measure
gradual or sudden again ("Change tempo"); on a measure without one it starts
one at the tempo in force, as Guitar Pro does, and `changeTempo()` keeps a
change gradual when its tempo changes. The command line types `tempo-gradual
<m> on|off`; the app's measure popup has a Gradual chip under the tempo. The
measures of `elara-cli edit --json` show `"gradual": true`.

## Consequences

- Imported accelerandos and ritardandos play as written, with no warning.
- Tests: `core.A gradual tempo change moves to the next tempo along the score`
  (values from the closed form: 4 ln 2 seconds for a whole note from quarter =
  60 to 120), `audio.Sequence: a gradual tempo change speeds the notes up on the
  way`, `io.Guitar Pro: a linear tempo point moves to the next one`, the native
  round trip, `io.MusicXML: rit. and accel. make the tempo change there
  gradual`, `io.MusicXML export: a gradual tempo change is a word with dashes
  to the next tempo` and `io.MIDI export steps a gradual tempo change along the
  way` (read back, two quarters from quarter = 100 to 140 last 3 ln 1.4 s), the
  `notation.Tempo marks: a gradual change ...` layout tests (which way it goes,
  what it leaves out, and the line across systems), `core.A tempo change can be
  made gradual and sudden again`, `cli.edit_gradual_tempo` with
  `cli.sequence_gradual_tempo` (9.545 s: 8 ln 2 s for the ramp over two
  measures) and `cli.render_gradual_tempo`, and the app's smoke test.
