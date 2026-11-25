# 0007. MusicXML import

- Status: Accepted
- Date: 2026-11-23

## Context

MusicXML is the open exchange format of notation software (MuseScore, Finale,
Sibelius, Dorico, Guitar Pro all export it). Its specification is public (W3C
Music Notation Community Group), so an importer can be written from the
specification alone (CONTRIBUTING.md, golden rule 6). It is the first importer
and fixes how importers report what they cannot represent.

MusicXML describes *notation*: written pitches, per-part attributes, voices
numbered across staves, `backup`/`forward` cursors, guitar tablature as a
second staff. Elara's model (ADR-0004) stores sounding pitches, song-wide
measure headers, at most four voices per staff, and tablature as a view of a
fretted staff.

## Decision

**Parser.** pugixml (MIT, vcpkg): a small, fast DOM parser with no
dependencies. Compressed `.mxl` files are zip archives read with miniz
(ADR-0006): `META-INF/container.xml` names the root file.

**Scope.** `score-partwise` documents (the form every exporter writes);
`score-timewise` is refused with a clear message.

**Mapping.**

- One `part` becomes one `Track`. Its `staves` become staves, except that a
  `TAB` staff (clef sign `TAB`) is merged into the standard staff of a guitar
  part: its `staff-tuning` and `capo` give the staff's `Tuning`, its notes are
  the same notes. A part with only a tablature staff becomes one fretted staff.
- Measure headers (time and key signature, tempo from `sound/@tempo`) come from
  the first part, since they are song-wide in Elara. Words such as "rit." or
  "accel." with a tempo after them make the change there gradual (ADR-0082).
- Pitches are converted to sounding pitch with the part's `transpose`
  (`chromatic` + 12 x `octave-change`) and spelled with its `diatonic` steps;
  `clef-octave-change` does not change the sound. The instrument's
  `writtenOffset` makes notes sit where the source shows them: minus the
  transposition, plus 12 for an octave clef (`G` or `F` with
  `clef-octave-change` -1, mapped to `treble-8vb` / `bass-8vb`). A guitar
  written with an 8vb treble clef thus gets the electric-guitar preset's
  values (treble 8vb, written offset +12), and a B-flat clarinet +2.
- On a fretted staff string and fret win over the written pitch, since the
  tablature is what the player reads: a pitch that disagrees is corrected (and
  reported, except for natural harmonics, which are stored at their fret).
- Durations come from `type`, `dot` and `time-modification`; the `duration`
  element is used for the cursor and when `type` is missing.
- Voices are numbered per part in MusicXML (MuseScore uses 1-4 for staff 1 and
  5-8 for staff 2); each staff's voices are renumbered 0-3 in order of
  appearance. A voice that starts late (`forward`, `backup`) is padded with
  rests, and a voice that stops early is filled with rests (`fillWithRests`).
- The model has no partial measures: a pickup (`implicit="yes"` first
  measure) gets leading rests so its notes end the measure; both are reported.
- Guitar notations: `string`/`fret`, `hammer-on`/`pull-off` (stop) as legato,
  `bend` (with `release`, `pre-bend`), `harmonic`, `slide`/`glissando`, ties,
  `staccato`, `accent`, `strong-accent`, `scoop`/`plop` (slide in from below or
  above), `doit`/`falloff` (slide off upwards or downwards), `wavy-line` (wide
  vibrato with the SMuFL glyph `guitarWideVibratoStroke`), `x` noteheads as
  dead notes, and `dynamics` as the dynamic of the following notes. Percussion
  uses `midi-unpitched` from the part's instruments.
- A pitched part's first `score-instrument` names the instrument; the part
  name is the track name.
- Keys are moved by the whole transposition interval (`diatonic` steps and
  `chromatic` semitones), so a written E-flat major for an alto saxophone is
  concert G-flat major, not F-sharp major.
- Song info: `work-title`, `movement-title`, `creator` (`composer`,
  `lyricist`, `artist`, `transcriber`), `rights`, `miscellaneous-field`
  (`album`, `notes`) and, for what those leave empty, `credit`s.

**Warnings, not failures.** What the model cannot hold yet (grace notes,
lyrics, chord symbols, text, more than four voices...) is dropped and
listed in `ImportResult::warnings`, one line per part and feature with the
first measure and a count, so users and tests see what was lost. Only an
unreadable document is an error. Numbers are clamped where they are read, and
a fuzz test checks that damaged files never crash the importer and always
give a song the native format accepts.

**Performance.** Parts after the first are read on several threads: the first
part writes the song-wide measure headers, the others only read them and the
(immutable) DOM. Warnings are collected per part and merged in score order,
so the result never depends on the number of threads (tested). Documents
under 1 MiB use one thread; `MusicXmlOptions::threads` overrides it.

## Consequences

- Files from any notation program can be opened once the CLI and app call the
  importer; each dropped feature is visible and becomes a model task.
- Exporters disagree on details (voice numbering, tablature staves, where
  dynamics sit); the self-made corpus (`tests/corpus/musicxml/`) pins the cases
  we handle, and new real-world quirks get a corpus file each.
- MusicXML export (ADR-0008) is the reverse mapping, and round-trip tests
  keep both directions in step.
- `elara-cli import <file> --json` prints the song, the warnings and the
  validation issues, and `--out` saves it as `.elara`.
