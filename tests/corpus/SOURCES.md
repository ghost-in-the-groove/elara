# Corpus sources

Every file in this directory, who made it, its licence and what it exercises
(rules: `README.md`). All files so far were written by hand by Elara
contributors from the public format specifications and are released under
CC0 1.0.

## MusicXML (`musicxml/`)

Written by hand against the MusicXML 4.0 specification (W3C Music Notation
Community Group).

| File | Exercises |
|---|---|
| `melody.musicxml` | Title, composer and rights; tempo; accidentals; dotted note; key and meter change; whole-measure rest |
| `guitar-tab.musicxml` | Standard staff with an octave clef plus a TAB staff (drop D, capo 2) merged into one fretted staff; string/fret only on the TAB staff; hammer-on, bend and release, natural harmonic, slide, staccato, accent, triplets, tie, dead note, dynamic |
| `piano-voices.musicxml` | Two staves; voices numbered 1-4 and 5-8; chord; `forward` and `backup`; meter and tempo change; grace note, lyric, repeat, chord symbol and text direction (reported as warnings) |
| `drums.musicxml` | Percussion clef; unpitched notes mapped to General MIDI keys through `midi-unpitched`; chord of two drums |
| `transposing.musicxml` | Pickup measures; B-flat clarinet with `transpose` and a written key; bass with an octave clef and its own `divisions` |
