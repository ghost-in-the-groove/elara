# 0008. MusicXML export

- Status: Accepted
- Date: 2026-11-23

## Context

MusicXML is how scores move between notation programs, so Elara must write it
as well as read it (ADR-0007). Two forces pull on the writer: other programs
must show a score a musician recognises (a guitar with its tablature, drums on
a drum staff, transposing instruments in their written key), and Elara must be
able to read its own output back without losing anything MusicXML can hold.
The model (ADR-0004) is richer than MusicXML in a few guitar techniques, and
poorer in many notation features, which do not exist in it yet.

## Decision

**Reverse of the import mapping.** The writer makes the choices the importer
(ADR-0007) undoes, so that `import(export(song)) == song` for every song made
only of what MusicXML holds. Tests check this for every corpus file, for a song
that uses every supported feature and for random songs.

- One track is one `part`, numbered `P1`, `P2`... A pitched part names its
  instrument in `score-instrument` (read back as the instrument's name; the
  part name is the track name) and its program and channel in
  `midi-instrument`.
- A single fretted staff is written as two staves: the standard staff and a
  `TAB` staff with the same notes, carrying `staff-lines`, `staff-tuning` and
  `capo`. Both have `string` and `fret`. A fretted track with several staves
  keeps one standard staff each, with its tuning in `staff-details`.
- Written pitches: the instrument's `writtenOffset`, minus 12 for an octave
  clef (`treble-8vb`, `bass-8vb`, written as `clef-octave-change` -1), becomes
  `transpose` (`chromatic`, `octave-change` and `diatonic` steps estimated as
  `round(chromatic x 7 / 12)`). Notes are spelled by moving the sounding
  spelling back by those steps, so a B-flat clarinet's sounding B-flat is a
  written C. Keys move by the same interval in both directions (a diminished
  fifth gives six flats, not six sharps), so concert G-flat stays G-flat.
- Voices are numbered `staff x 4 + voice + 1` (MuseScore's scheme). The
  importer numbers voices by order of appearance, so every voice up to the last
  non-empty one is written; an empty one in between is a hidden measure rest
  (`print-object="no"`). An empty voice 1 is a visible measure rest.
- Drums use `unpitched` with the staff position and notehead of the
  conventional drum map (`docs/instruments.md`), one `score-instrument` and
  `midi-instrument` (channel 10, `midi-unpitched` = key + 1) per key used.
- Dynamics are written per staff where they change (from mf), as `direction`s
  before the first note at that time. Notes that start together on a staff
  therefore need the same dynamic.
- Tempo marks go in the first part's first voice: `metronome` with beat unit
  and dots, plus `sound tempo` in quarter notes per minute, with an `offset`
  when the change falls inside a note. A gradual change adds "rit." or "accel."
  and dashes that stop at the next tempo mark (ADR-0082).
- `divisions` is the least common multiple that makes every duration and tempo
  offset a whole number (capped at 2^20, beyond which durations are rounded
  and reported).
- Guitar techniques: ties, `hammer-on`/`pull-off` (chosen by pitch direction),
  `slide`, `bend` (`pre-bend`, `release`), natural and artificial `harmonic`,
  `wavy-line` with the SMuFL glyph `guitarVibratoStroke` or
  `guitarWideVibratoStroke` (so wide vibrato survives), `scoop`/`plop` for
  slides into a note, `doit`/`falloff` for slides off it, `play/mute` for palm
  mute, `x` and parenthesised noteheads for dead and ghost notes,
  `arpeggiate` for strums, `up-bow`/`down-bow` for pick strokes, `tap` on a
  tapped beat's first note and single `tremolo` for tremolo picking. The
  importer reads all of them; a `tap` on any note taps its beat (ADR-0060).
- Song info: `work-title`, `movement-title` (subtitle), `creator`s
  (`composer`, `lyricist`, `artist`, `transcriber`), `rights`, and
  `miscellaneous-field`s `album` and `notes`. `encoding/supports` tells readers
  to compute accidentals, beams and stems themselves.

**Warnings for what MusicXML cannot hold.** Let ring, whammy bar, slap and
pop, pinch/tapped/semi harmonics (written as artificial), shift slides
(written as legato slides), arbitrary bend curves (the nearest bend, release or
pre-bend), harmonic nodes away from the fret, effects on rests, tuplet tempo
beat units (written in quarters), mixed dynamics at one time on a staff and
keys a transposition cannot give back are written as close as possible and
listed in `ExportResult::warnings`, in the importer's format.

**Output.** Indented MusicXML 4.0 text with the partwise DOCTYPE, or a
compressed `.mxl` archive (`mimetype`, `META-INF/container.xml`,
`score.musicxml`) holding the same XML without indentation, which halves the
work of deflate (measured in `docs/performance.md`). Files are chosen by
extension and written atomically.

**Performance.** Parts only read the song, so large songs are written one part
per thread into separate buffers joined in score order; the text never depends
on the number of threads (tested).

## Consequences

- Elara scores open in other notation programs, and the round-trip tests make
  every importer or exporter change prove that nothing else was lost.
- Writing the tablature twice roughly doubles the size of guitar parts; `.mxl`
  archives are the compact choice for large scores.
- Model features MusicXML lacks stay in the warning list until MusicXML (or a
  documented extension) can carry them. Notation features added to the model
  (lyrics, grace notes...) need both directions and a round-trip case, as
  repeats and alternate endings got (ADR-0016).
- `elara-cli export <song.elara> <out.musicxml|.mxl>` writes the file and prints
  the warnings.
