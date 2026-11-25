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
GUITAR PRO` header (3-5); Guitar Pro 1 and 2 fail with a message that names
them, so users know the file is not damaged. `elara-cli import` takes
`.gp`, `.gpx`, `.gp5`, `.gp4` and `.gp3`. Guitar Pro 7-8 came first: they are what
Guitar Pro writes today, and their XML also covers Guitar Pro 6 once its
container is unpacked (`importGpif`). Guitar Pro 3-5 followed: most tablature
shared online is in those versions. Guitar Pro 6 came last.

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
- Guitar Pro gives every staff a tuning, a flute's or a piano's too (a
  guitar's by default), and string and fret numbers to every note. What it
  shows is in `Content/PartConfiguration`, beside the score: the number of
  score views, then for each view a byte (multi-measure rests), its number of
  tracks and a byte of flags per track (1 standard notation, 2 tablature, 4
  slashes, 8 numbered notation), numbers big-endian, the full score first. A
  track the full score shows without tablature keeps its tuning only when its
  program is a guitar or a bass; otherwise it is not fretted and its notes
  keep their pitch. Without that file, or when it does not fit the score (it
  only says how tracks are shown, so a damaged one is ignored), every tuning is
  kept.
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
  picking, slap and pop, and tapping: a note tapped with either hand
  (`Tapped`, `LeftHandTapped`) taps its beat, where the model keeps it
  (ADR-0060), unless the beat is slapped or popped.
- Repeats and alternate endings are kept (ADR-0016).
- Everything else is listed in the warnings: sections, jumps, fermatas, triplet feel, grace notes, lyrics, chords, text,
  hairpins, whammy bar, trills. Short voices are filled with rests.
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
    Repeats and alternate endings are kept (ADR-0016). Instrument and volume
  changes, markers, triplet feel, lyrics, chord diagrams, text, grace notes and trills are listed in the
  warnings, as for Guitar Pro 7-8.

**Guitar Pro 6.** A `.gpx` file is `BCFZ`, the unpacked size (32 bits,
little-endian) and a bit stream read from the most significant bit of each
byte. A 0 bit and two bits (least significant first) give 0-3 literal bytes; a
1 bit, a four-bit word size and two numbers of that many bits (least
significant first) copy earlier bytes: `length` of them from `offset` bytes
before the end, never more than `offset`. The result is `BCFS`, a file system
of 4096-byte sectors: sector 0 is unused, and a sector starting with the
number 2 is a file entry with the name at byte 4, the size at byte 0x8C and the
list of the sectors that hold the file at byte 0x94, ended by 0. The next entry
follows the file's last sector. The score is the entry `score.gpif`, read with
`PartConfiguration` as above; the other entries (page layout) are skipped. An uncompressed `BCFS` file
is read the same way. Unpacking stops at 64 MB, and references before the
start or sectors past the end make the file damaged.

The GPIF of Guitar Pro 6 is an older form of the one above: a track has no
instrument set and no staves. Its `GeneralMidi` element says whether it is a
drum kit (`table="Percussion"`) and gives the program, which also names the
instrument as in Guitar Pro 3-5; its tuning, capo and flat spelling are
properties of the track itself; and since it has no transposition, guitars
and basses get the 8vb clef and an octave of written offset, as in every
other version. Drum notes that give a MIDI key keep it. Guitar Pro 6 also
names drums by kit element and variation, a table this reader does not have
yet, so such notes are dropped with a warning.

**Threads.** A GPIF score is parsed once; then each staff is read on its own,
measure by measure, on every core when the score has more than 20 000 notes
(`GuitarProOptions::threads` overrides it). A staff reader only looks the
collections up and writes its own staff, warnings and rhythm cache. Warnings
are merged in the order one reading measure by measure, staff after staff,
gives them, and of several damaged bars the first in that order is reported,
so the song, the warnings and the errors never depend on the number of
threads. Guitar Pro 3-5 streams can only be read in order and stay on one
thread.

**Corpus.** `tests/corpus/gp/generate.py` writes self-made `.gp` and `.gpx`
files (with its own compressor for the container) and `generate_binary.py`
self-made `.gp5`, `.gp4` and `.gp3` files from the same structures. The
readers were also checked locally against Guitar Pro 3, 4 and 8 files that
cannot be shared (read to the last byte, with no validation issue). No Guitar
Pro 5 or 6 file was at hand: Guitar Pro 5 rests on the self-made files and on
what it shares with Guitar Pro 4, and Guitar Pro 6 on the self-made files and
on what its GPIF shares with Guitar Pro 7-8.

## Consequences

- Guitarists can open the files of every Guitar Pro version from 3 to 8
  through one entry point.
- Guitar Pro 6 drum tracks that name their notes by kit element lose them
  until the element table is added; the warning says so.
- Where the meaning of a GPIF value is inferred (bend scale, tempo position,
  accent flags, slide flags), the guess is written down here and in the code;
  a file that shows otherwise becomes a corpus case and a fix.
- Repeats and alternate endings are kept since ADR-0016; jumps (segno,
  coda) and sections are still dropped.
