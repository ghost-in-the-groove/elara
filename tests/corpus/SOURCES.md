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

## Guitar Pro 6-8 (`gp/`)

Written by `gp/generate.py` (run it to rebuild them): a zip archive (`.gp`) or
a compressed Guitar Pro 6 container (`.gpx`) with the score as GPIF XML, every
element written by hand following the structure of files saved by Guitar Pro
(ADR-0010).

| File | Exercises |
|---|---|
| `guitar-techniques.gp` | Electric guitar in drop D with a capo; palm mute, hammer-on in a triplet, bend with vibrato, natural harmonic with let ring, tie, dead note with an accent, shift slide, staccato, pick stroke, dynamics; a tempo change in the middle of a bar; 4/4 then 3/4; a repeat, a grace note, chord diagrams on the staff (one named by two beats, one by none) and a beat naming a missing one (reported) |
| `band.gp` | Four-string bass, two-staff piano with a second voice, a drum kit whose notes name its articulations, a B-flat clarinet given by concert pitches only; F major; a gradual tempo change in dotted quarters, lyrics and a short voice (reported); the piano and the clarinet carry the default tunings Guitar Pro gives every staff, and its part configuration shows them without tablature; a guitar chord diagram over the piano |
| `duet.gpx` | Guitar Pro 6: a container with three files, compressed with literals and back references; steel-string guitar in E-flat tuning (spelled with flats) with a capo, both on the track; a drum kit from the General MIDI table whose notes give their MIDI keys; E-flat major; a tempo without a beat unit; let ring; a chord diagram on the track; a drum note that names only a kit element (reported) |
| `double-coda.gp` | Signs and jumps (ADR-0049): segno, segno segno, coda and double coda signs, "Da Coda" and "Da Double Coda" on one bar, "Da Segno Segno al Double Coda"; played 1 2 3 4 5 3 4 7; an "Intro" section (letter A) and a section with only the letter B |

## Guitar Pro 3-5 (`gp/`)

Written field by field by `gp/generate_binary.py` (run it to rebuild them),
following the structure of the binary format (ADR-0010); unused fields hold
neutral values.

| File | Exercises |
|---|---|
| `techniques.gp5` | Guitar Pro 5.10: overdriven guitar with a capo; palm mute, hammer-on in a triplet, full bend with vibrato, natural harmonic with let ring, a tie that does not repeat its fret, dead note with a heavy accent, dynamics, a second voice, shift slide with staccato and a down pick stroke, upward strum, tremolo picking, slide out, tapped harmonic, whammy-bar dip; a tempo change in the middle of a measure; G then D major; 4/4, 3/4; an "Intro" marker, a repeat; a chord diagram, a grace note; text (reported) |
| `quartet.gp4` | Guitar Pro 4.06: bass with slap, pop, slide in and a harmonic; piano and flute programs that keep only their pitches; drums on channel 10; F major; a tempo change from a mix table, a tie, a bend, vibrato, a chord diagram with a string not played; triplet feel, lyrics, a volume change and a trill (reported) |
| `melody.gp3` | Guitar Pro 3.00: a Windows-1252 title; nylon guitar with a per-beat natural harmonic, hammer-on, slide, tapped quintuplet, wide vibrato with let ring, whammy-bar dip and a downward strum; A minor, 4/4 then 6/8; chord diagrams in both formats; text (reported) |
| `dal-segno.gp5` | Guitar Pro 5.10 directions (ADR-0049): a segno, "Da Coda", "Da Segno al Coda" and a coda sign; played 1 2 3 4 2 3 5 6 |
| `piano.gp5` | Guitar Pro 5.10 keyboards on a grand staff (ADR-0075): a piano with an accented upward strum, a melody and a left hand in the second voice, split at middle C; an electric piano whose left hand is always the second voice |

## MIDI (`midi/`)

Written byte for byte by `midi/generate.py` (run it to rebuild them) from the
Standard MIDI File 1.0 specification; the "played" timing is a fixed
pseudo-random sequence, not a recording.

| File | Exercises |
|---|---|
| `played-piano.mid` | Format 1 with a conductor track (title, copyright, D major); two hands played without quantization (starts off by up to 24 ticks, early and late releases, uneven velocities) split at middle C into two staves; a last note released just after the barline; sustain pedal (reported) |
| `drums.mid` | Channel 10 at 96 ticks per quarter note with short drum-machine notes: crash, closed and open hi-hat, snare and kick on the hands' and feet's voices, pedal hi-hat |
| `band-format0.mid` | Format 0 with three channels becoming three tracks named by program (fingered bass, steel guitar, flute); 3/4 then 6/8; B-flat major spelling; triplet eighths; a tempo change; a lyric (reported) |
