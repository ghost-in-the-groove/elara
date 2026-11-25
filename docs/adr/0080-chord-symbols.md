# 0080. Chord symbols

- Status: Accepted
- Date: 2026-11-25

## Context

A chord symbol names the harmony above a beat ("Am7", "C/G"); on fretted
instruments it often comes with a chord diagram, a small grid showing where
each string is stopped. Songbooks and tablature write them on almost every
measure, and they were the most frequent loss when importing songs: every
importer dropped them with a warning.

- Guitar Pro 3-5 files store a chord per beat: a name and, unless the chord
  is written in the oldest format without frets, a first fret and one fret
  per string from the highest (`-1` for a string not played), with barres,
  fingerings and the chord's structure (root, kind, extensions) that the
  name already spells.
- Guitar Pro 6-8 files keep a staff's (or, in version 6, a track's) chords in
  a `DiagramCollection` of items with an `id` and a `name`, each with a
  `<Diagram baseFret="...">` of `<Fret string="..." fret="..."/>` (string 0
  is the lowest; a string with no `<Fret>` is not played), fingerings and
  show flags. A beat refers to one by `<Chord>id</Chord>`.
- MusicXML writes `<harmony>` before the note it belongs to: a root, a kind
  (`minor-seventh`, with the text to print), an optional bass and an
  optional `<frame>` diagram.

## Decision

**A chord symbol belongs to a beat.** `Beat::chord` holds at most one
`ChordSymbol`: its `name` as written, and an optional `ChordDiagram` with
the fret at the top of its grid (`firstFret`, 1 at the nut) and one fret per
string from string 1, the top line of the tablature (`ChordDiagram::kMuted`
for a string not played). Frets count from the capo, like notes' frets. A
diagram has its staff's strings; on a staff without a tuning (a lead sheet's
melody, a piano track that shows guitar chords) it has as many as the file
gives, and drum staves keep the names only.

A chord symbol is not sound: playback, audio rendering and MIDI export
ignore it, and a beat with a chord and no notes is a rest.

**Beats share their chord symbols.** `Beat::chord` is a
`SharedOptional<ChordSymbol>`: a pointer to a symbol nobody changes, which
a change replaces as a whole. A beat without a chord costs a null pointer
(16 bytes on every beat, not the 70 or more an inline symbol would add),
copying a beat that has one costs a reference count, and the beats of a
Guitar Pro 6-8 file that refer to one diagram point to one symbol. Beats
compare by the symbols' values.

**Reading.**

- Guitar Pro 3-5: the name and the frets of the track's strings; a chord
  whose strings are all not played, or that has no frets, has no diagram.
- Guitar Pro 6-8: the item's name and diagram, `baseFret + 1` as the first
  fret, strings counted from the top.
- Barres, fingerings, show flags and the chord's structure are not kept:
  barres and fingerings can be derived from the frets when diagrams are
  drawn, and the name already says what the structure says. They are
  dropped without a warning.

**MusicXML.** A `<harmony>` goes on the beat of its staff that starts where
it does (after its `<offset>`), the voices looked at in order; one between
beats splits the beat sounding across it into tied beats (ADR-0096), and is
reported as dropped only where none can split; one written as a Roman
numeral or a function is dropped too. Its name is the root, the kind's `text` (or the usual suffix of its
kind: `minor-seventh` is "m7"), its degrees ("add9", "b5", "no3") and "/" and
the bass; `kind` none is "N.C." or its text. A `<frame>` is the diagram,
string 1 the highest and strings without a `<frame-note>` not played.
Writing splits a name into a root, a suffix and a bass: the suffix is
written as the kind's `text` (so the name comes back as written) with the
kind that suffix means, or `other`; a name that does not start with a pitch
("N.C.", a diagram's empty name) is a hidden root with kind `none` and the
name as its text. A diagram is a `<frame>` with `print-frame="yes"`.

**Native format.** A beat's `"chord"` is `{"name": "Am", "diagram":
{"firstFret": 1, "frets": [0, 1, 2, 2, 0, -1]}}`, the diagram left out when
there is none. Older files without chords read as before.

**Drawing.** A chord symbol is drawn above its staff over its beat's column:
its name centred and, on a staff with frets, its diagram under the name. A
diagram shows its strings (string 1 on the right) and at least four frets,
the nut thick when it starts at the first fret and otherwise that fret's
number left of the grid; a dot (SMuFL `fretboardFilledCircle`) where a string
is stopped, `fretboardO` above an open string and `fretboardX` above one not
played. The chord symbols of a line hang from one line above the technique
lanes, their names on one baseline. They are drawn once: over the standard
notation of their staff when the system shows it, otherwise over its
tablature. They keep apart from each other and from the barlines, never from
the notes under them: the springs between two of them share the room they
need by their ideal spaces, and one on a beat where no notes start (a
measure of rests) gets a column of its own. Barres and fingerings are not
drawn: a dot on each stopped string says what a barre says.

**Editing.** `setChordSymbol()` names the caret's beat, a rest as well as a
note, as one undo step; an empty name takes the symbol away. On a staff with
frets the symbol gets the diagram the beat's frets make when two or more of
its notes have one (strings without a note not played, the grid from the nut
unless a fret lies past the fourth); otherwise it keeps the diagram it had,
so renaming an imported chord keeps its shape. The command line types
`chord <name>` and `chord none`. In the app, Ctrl+K or the entry bar's chord
button opens a popup under the caret's beat (`WordsPopup`, which beat text
shares, ADR-0081): a name typed and Enter, or one of
the chords the song already names, sets it; the bin takes it away.

**Transposing.** Chord symbols move with their beats' notes, by the same
interval (ADR-0026): the root and the bass keep the degree of their spelling,
so G7 up a whole tone in C major is A7 and Bb7/F is C7/G, and are written with
one sharp or flat at most: a root that would need two takes the step next to
it and its bass follows, so Gb/Bb up a diminished fifth is C/E, not Dbb/Fb. The suffix stays as it is; a name that does not start with a pitch
("N.C.") does not move. A diagram moves along the neck by as many frets, open
strings included, and frames itself again (from the nut unless a fret lies
past the fourth); when a fret would fall below the nut or past the last one
above the capo, it shows the same chord an octave up, else an octave down,
and is dropped when neither fits. Beats that shared a symbol still do.

## Consequences

- The model, the native format, the Guitar Pro 3-8 importers and MusicXML
  both ways keep chord symbols, layout draws them, the caret types them,
  transposition moves them, and `elara-cli dump` and `import --json` show
  them. Splitting a name into root, suffix and bass lives in `elara_core`
  (`chord_name.hpp`), shared by the MusicXML writer and transposition.
- A line with chord symbols is taller by their row (about 2.5 staff spaces
  for names, 6 with diagrams), and measures with many of them on short notes
  grow to keep their names apart.
- Copying, pasting and retuning carry a diagram as it is: it is drawn with
  its own strings. A transposed diagram follows its frets, not the strings the
  notes under it may have moved to.
- Tests: `core.Beats share chord symbols and compare them by value`,
  `io.Guitar Pro 3-5: chords keep their names and diagrams`,
  `io.Guitar Pro: chords come from the staff's diagram collection`, the
  Guitar Pro 6 duet, `io.Beats naming the same chord share one symbol once
  read`, `io.Chord diagrams outside the neck are rejected`, the native round
  trip, `io.MusicXML: chord symbols are named by root, kind, degrees and
  bass`, `io.MusicXML export: chord symbols keep their names and diagrams`,
  the import snapshots of the corpus, the `notation.Chord symbols: ...`
  layout tests (names, diagrams, frets up the neck, tablature, technique
  lanes, spacing, measures of rests), the render snapshots of the corpus,
  `core.Entry: chord symbols name the caret's beat, with the diagram its
  frets make`, `core.Entry: chord symbols on staves without frets have no
  diagram`, `cli.edit_names_chords` with `cli.dump_chords`, the app's smoke
  test, which names the caret's beat and takes the name away, `core.A
  transposed chord name moves its root and its bass`, `core.A transposed chord
  diagram moves along the neck`, `core.Chord symbols move with the notes`, and
  `cli.edit_transposes_chords` with `cli.dump_transposed_chords`.
