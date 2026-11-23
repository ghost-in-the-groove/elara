# Corpus sources

Every file in this directory, who made it, its licence and what it exercises
(rules: `README.md`). All files so far were made by Elara contributors from
the public format specifications, by hand or by a script kept next to them,
and are released under CC0 1.0.

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

## MIDI (`midi/`)

Written byte for byte by `midi/generate.py` (run it to rebuild them) from the
Standard MIDI File 1.0 specification; the "played" timing is a fixed
pseudo-random sequence, not a recording.

| File | Exercises |
|---|---|
| `played-piano.mid` | Format 1 with a conductor track (title, copyright, D major); two hands played without quantization (starts off by up to 24 ticks, early and late releases, uneven velocities) split at middle C into two staves; a last note released just after the barline; sustain pedal (reported) |
| `drums.mid` | Channel 10 at 96 ticks per quarter note with short drum-machine notes: crash, closed and open hi-hat, snare and kick on the hands' and feet's voices, pedal hi-hat |
| `band-format0.mid` | Format 0 with three channels becoming three tracks named by program (fingered bass, steel guitar, flute); 3/4 then 6/8; B-flat major spelling; triplet eighths; a tempo change; a lyric (reported) |
