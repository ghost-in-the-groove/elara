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

## Guitar Pro 7-8 (`gp/`)

Written by `gp/generate.py` (run it to rebuild them): a zip archive with the
score as GPIF XML, every element written by hand following the structure of
files saved by Guitar Pro 8 (ADR-0010).

| File | Exercises |
|---|---|
| `guitar-techniques.gp` | Electric guitar in drop D with a capo; palm mute, hammer-on in a triplet, bend with vibrato, natural harmonic with let ring, tie, dead note with an accent, shift slide, staccato, pick stroke, dynamics; a tempo change in the middle of a bar; 4/4 then 3/4; a repeat and a grace note (reported) |
| `band.gp` | Four-string bass, two-staff piano with a second voice, a drum kit whose notes name its articulations, a B-flat clarinet given by concert pitches only; F major; a gradual tempo change in dotted quarters, lyrics and a short voice (reported) |

## Guitar Pro 3-5 (`gp/`)

Written field by field by `gp/generate_binary.py` (run it to rebuild them),
following the structure of the binary format (ADR-0010); unused fields hold
neutral values.

| File | Exercises |
|---|---|
| `techniques.gp5` | Guitar Pro 5.10: overdriven guitar with a capo; palm mute, hammer-on in a triplet, full bend with vibrato, natural harmonic with let ring, a tie that does not repeat its fret, dead note with a heavy accent, dynamics, a second voice, shift slide with staccato and a down pick stroke, upward strum, tremolo picking, slide out, tapped harmonic, whammy-bar dip; a tempo change in the middle of a measure; G then D major; 4/4, 3/4; a marker, a repeat, a chord diagram, a grace note and text (reported) |
| `quartet.gp4` | Guitar Pro 4.06: bass with slap, pop, slide in and a harmonic; piano and flute programs that keep only their pitches; drums on channel 10; F major; a tempo change from a mix table, a tie, a bend, vibrato; triplet feel, lyrics, a volume change and a trill (reported) |
| `melody.gp3` | Guitar Pro 3.00: a Windows-1252 title; nylon guitar with a per-beat natural harmonic, hammer-on, slide, tapped quintuplet, wide vibrato with let ring, whammy-bar dip and a downward strum; A minor, 4/4 then 6/8; chord diagrams in both formats and text (reported) |

## MIDI (`midi/`)

Written byte for byte by `midi/generate.py` (run it to rebuild them) from the
Standard MIDI File 1.0 specification; the "played" timing is a fixed
pseudo-random sequence, not a recording.

| File | Exercises |
|---|---|
| `played-piano.mid` | Format 1 with a conductor track (title, copyright, D major); two hands played without quantization (starts off by up to 24 ticks, early and late releases, uneven velocities) split at middle C into two staves; a last note released just after the barline; sustain pedal (reported) |
| `drums.mid` | Channel 10 at 96 ticks per quarter note with short drum-machine notes: crash, closed and open hi-hat, snare and kick on the hands' and feet's voices, pedal hi-hat |
| `band-format0.mid` | Format 0 with three channels becoming three tracks named by program (fingered bass, steel guitar, flute); 3/4 then 6/8; B-flat major spelling; triplet eighths; a tempo change; a lyric (reported) |
