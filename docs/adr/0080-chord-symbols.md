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
beats is reported as dropped, and so is one written as a Roman numeral or a
function. Its name is the root, the kind's `text` (or the usual suffix of its
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

## Consequences

- The model, the native format, the Guitar Pro 3-8 importers and MusicXML
  both ways keep chord symbols, layout draws them, and `elara-cli dump` and
  `import --json` show them. Typing them at the caret and transposing their
  names with the notes come next, each recorded here as it lands.
- A line with chord symbols is taller by their row (about 2.5 staff spaces
  for names, 6 with diagrams), and measures with many of them on short notes
  grow to keep their names apart.
- Transposing a song leaves chord names as they are until transposition
  learns to spell them. Copying, pasting and retuning carry a diagram as it
  is: it is drawn with its own strings.
- Tests: `core.Beats share chord symbols and compare them by value`,
  `io.Guitar Pro 3-5: chords keep their names and diagrams`,
  `io.Guitar Pro: chords come from the staff's diagram collection`, the
  Guitar Pro 6 duet, `io.Beats naming the same chord share one symbol once
  read`, `io.Chord diagrams outside the neck are rejected`, the native round
  trip, `io.MusicXML: chord symbols are named by root, kind, degrees and
  bass`, `io.MusicXML export: chord symbols keep their names and diagrams`,
  the import snapshots of the corpus, the `notation.Chord symbols: ...`
  layout tests (names, diagrams, frets up the neck, tablature, technique
  lanes, spacing, measures of rests) and the render snapshots of the corpus.
