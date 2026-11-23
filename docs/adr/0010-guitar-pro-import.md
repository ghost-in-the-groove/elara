# 0010. Guitar Pro import

- Status: Accepted
- Date: 2026-11-23

## Context

Most guitar tablature people already own is in Guitar Pro files, so reading
them is what makes Elara useful to guitarists from day one (`docs/SCOPE.md`).
There are three generations:

- Guitar Pro 3, 4 and 5 (`.gp3`, `.gp4`, `.gp5`): a binary stream.
- Guitar Pro 6 (`.gpx`): a compressed container holding an XML score (GPIF).
- Guitar Pro 7 and 8 (`.gp`): a zip archive whose `Content/score.gpif` is the
  same kind of XML score.

None of them has a public specification. Elara may not copy code from other
projects that read them (`CONTRIBUTING.md`, golden rule 6), and the corpus may
only hold files we made (`tests/corpus/README.md`).

## Decision

**One entry point.** `importGuitarPro(bytes)` recognises every generation by its
content: a zip archive (7-8), the `BCFZ`/`BCFS` container (6) or the `FICHIER
GUITAR PRO` header (3-5). Generations not read yet fail with a message that
names them, so users know the file is not damaged. `elara-cli import` takes
`.gp`, `.gpx`, `.gp5`, `.gp4` and `.gp3`. Guitar Pro 7-8 come first: they are what
Guitar Pro writes today, and their XML also covers Guitar Pro 6 once its
container is unpacked (`importGpif`).

**Written from the files.** The reader follows the structure of files saved by
Guitar Pro 8, examined as data. GPIF is normalised: master bars (meter, key,
one bar per staff), bars (clef, four voices), voices (beats), beats (rhythm,
notes, dynamic) and notes refer to each other by id. The mapping:

- Song info from `<Score>`; `WordsAndMusic` fills empty words and music.
- Tempo automations of the master track: "bpm unit" (unit 1 eighth, 2 quarter,
  3 dotted quarter, 4 half, 5 dotted half) at a bar and a position given as a
  share of the bar. Gradual changes become sudden ones (warning).
- Tracks: kind from the instrument set (drum kit), a staff tuning (fretted),
  two staves or a piano program (keyboard), else pitched. Program from the
  first sound. `Transpose` (chromatic and octave) gives the written offset; an
  octave down turns the clef into its 8vb form, as the presets do.
- Staves: tuning (`Pitches`, lowest string first) and capo. Strings count from
  0 at the lowest string; frets count from the capo. A note's `Midi` number,
  when present, wins over a fret position that disagrees with it.
- Pitches: the `Midi` number, else string and fret, else the concert pitch
  (whose octaves count from MIDI 0, so middle C is C5). Spelling: the concert
  pitch's when it matches, else the key's. Drum notes use their `Midi` number
  or the kit articulation they name.
- Rhythms: note value, dots and the primary tuplet (nested tuplets keep the
  outer one, with a warning).
- Techniques the model holds: ties, hammer-ons and pull-offs (on the
  destination note), slides in and out, bends (100 = a full step; offsets in
  percent of the note), harmonics, palm mutes, dead and ghost notes, let ring,
  vibrato, staccato and accents, dynamics, pick strokes, strums, tremolo
  picking, slap and pop.
- Everything else is listed in the warnings: repeats and alternate endings,
  sections, jumps, fermatas, triplet feel, grace notes, lyrics, chords, text,
  hairpins, whammy bar, tapping, trills. Short voices are filled with rests.
- Missing references (a bar, voice, beat, note or rhythm id that is not there)
  make the file damaged.

**Corpus.** `tests/corpus/gp/generate.py` writes self-made `.gp` files from the
same structure, and the reader was also checked locally against files saved by
Guitar Pro 8 that cannot be shared.

## Consequences

- Guitarists can open Guitar Pro 7-8 files now; Guitar Pro 6 and 3-5 follow as
  separate tasks behind the same entry point.
- Where the meaning of a GPIF value is inferred (bend scale, tempo position,
  accent flags, slide flags), the guess is written down here and in the code;
  a file that shows otherwise becomes a corpus case and a fix.
- Repeats are not in the model yet, so songs with repeats play straight
  through until they are.
