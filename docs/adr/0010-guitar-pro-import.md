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
`.gp`, `.gpx`, `.gp5`, `.gp4` and `.gp3`. Guitar Pro 7-8 came first: they are what
Guitar Pro writes today, and their XML also covers Guitar Pro 6 once its
container is unpacked (`importGpif`). Guitar Pro 3-5 followed: most tablature
shared online is in those versions.

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

**Guitar Pro 3, 4 and 5.** The binary stream is little-endian: the song's
information (text in Windows-1252), a header per measure, the tracks, then each
track's content measure by measure (one voice in 3 and 4, two in 5). The
versions differ in which fields exist, never in their meaning, so one reader
takes the version (300, 400, 406, 500, 510) and skips what it does not use
(page setup, sound settings, chord diagrams). The mapping:

- Tracks: the instrument comes from the General MIDI program of the track's
  channel. Guitars and basses (programs 24-39, and banjo tracks) keep their
  tuning and capo, with the guitar or bass 8vb clef and a written offset of an
  octave; drum tracks (channel 10) are percussion with the fret as the MIDI
  key; any other program (piano, flute, voice) keeps only its pitches, in the
  clef that suits them, since its fret positions are just how Guitar Pro
  stored it.
- Notes: string 1 is the highest; frets count from the capo. A tied note takes
  the last fret of its string; a tie written after a rest is played as a new
  note (warning). Hammer-ons and pull-offs are flagged on the note they start
  from, so the legato mark goes to the next note on that string. Dynamics run
  from 1 (ppp) to 8 (fff); notes without one are forte.
- Bends and whammy-bar curves: positions count to 60 over the note, values are
  hundredths of a tone, as in GPIF. Guitar Pro 3 keeps the whammy bar as a
  single dip, and harmonics and vibrato per beat.
- Strums: the first speed is the upward strum, the second the downward one.
  This reading is the least certain one; a file that shows otherwise becomes a
  corpus case and a fix.
- Tempo changes come from mix-table changes, at the offset of their beat.
  Instrument and volume changes, repeats, alternate endings, markers, triplet
  feel, lyrics, chord diagrams, text, grace notes and trills are listed in the
  warnings, as for Guitar Pro 7-8.

**Corpus.** `tests/corpus/gp/generate.py` writes self-made `.gp` files and
`generate_binary.py` self-made `.gp5`, `.gp4` and `.gp3` files from the same
structures. The readers were also checked locally against Guitar Pro 3, 4 and
8 files that cannot be shared (read to the last byte, with no validation
issue); no Guitar Pro 5 file was at hand, so Guitar Pro 5 rests on the
self-made files and on what it shares with Guitar Pro 4.

## Consequences

- Guitarists can open Guitar Pro 3-5 and 7-8 files; Guitar Pro 6 follows as a
  separate task behind the same entry point.
- Where the meaning of a GPIF value is inferred (bend scale, tempo position,
  accent flags, slide flags), the guess is written down here and in the code;
  a file that shows otherwise becomes a corpus case and a fix.
- Repeats are not in the model yet, so songs with repeats play straight
  through until they are.
