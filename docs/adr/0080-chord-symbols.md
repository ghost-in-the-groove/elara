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
for a string not played). Frets count from the capo, like notes' frets.

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

- Guitar Pro 3-5: the name and the frets of the staff's strings; a chord
  whose strings are all not played, or that has no frets, has no diagram.
- Guitar Pro 6-8: the item's name and diagram, `baseFret + 1` as the first
  fret, strings counted from the top.
- Barres, fingerings, show flags and the chord's structure are not kept:
  barres and fingerings can be derived from the frets when diagrams are
  drawn, and the name already says what the structure says. They are
  dropped without a warning.

**Native format.** A beat's `"chord"` is `{"name": "Am", "diagram":
{"firstFret": 1, "frets": [0, 1, 2, 2, 0, -1]}}`, the diagram left out when
there is none. Older files without chords read as before.

## Consequences

- The model, the native format and the Guitar Pro 3-8 importers keep chord
  symbols; `elara-cli dump` and `import --json` show them. Reading and
  writing MusicXML `<harmony>`, drawing names (and diagrams on fretted
  staves) above the staff, typing them at the caret and transposing their
  names with the notes come next, each recorded here as it lands. Until
  they are drawn, a song with chord symbols looks as it did before.
- Transposing a song leaves chord names as they are until transposition
  learns to spell them. Copying, pasting and retuning carry a diagram as it
  is; a diagram whose strings differ from its staff's is not drawn.
- MusicXML export lists them as dropped until it writes `<harmony>`.
- Tests: `core.Beats share chord symbols and compare them by value`,
  `io.Guitar Pro 3-5: chords keep their names and diagrams`,
  `io.Guitar Pro: chords come from the staff's diagram collection`, the
  Guitar Pro 6 duet, `io.Beats naming the same chord share one symbol once
  read`, `io.Chord diagrams outside the neck are rejected`, the native round
  trip and the import snapshots of the corpus.
