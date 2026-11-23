# Glossary (Spanish -> English code names)

Use the **Code name** column for types, functions and files so the whole code
base speaks one vocabulary. American terms follow MusicXML.

| Español | English | Code name | Notes |
|---|---|---|---|
| canción / partitura | song / score | `Song` | Whole document |
| datos de la canción | song info | `SongInfo` | Title, artist, album... |
| pista | track | `Track` | One instrument |
| instrumento | instrument | `Instrument`, `InstrumentPreset` | Kind, GM program, written offset |
| pentagrama | staff | `Staff` | Plural `staves` |
| tablatura | tablature | view of a fretted `Staff` | Not a separate type (ADR-0004) |
| clave | clef | `Clef` | |
| compás (unidad) | measure | `Measure` | Not "bar"; one staff's content |
| cabecera de compás | measure header | `MeasureHeader` | Song-wide: signatures, repeats |
| indicación de compás | time signature | `TimeSignature` | |
| armadura | key signature | `KeySignature` | |
| voz | voice | `Voice` | Up to 4 per staff |
| tiempo / pulso (posición) | beat | `Beat` | Group of notes starting together |
| nota | note | `Note` | |
| silencio | rest | `Rest` | |
| figura / duración | duration, note value | `Duration`, `NoteValue` | Length is a `Fraction` of a whole note |
| posición temporal | time position | `Fraction` | Whole notes from the song or measure start |
| puntillo | dot | `dots` | |
| grupo irregular / tresillo | tuplet / triplet | `Tuplet` | |
| ligadura de prolongación | tie | `Tie` | Same pitch |
| ligadura de expresión | slur | `Slur` | |
| plica | stem | `Stem` | |
| barra (de corcheas) | beam | `Beam` | |
| alteración | accidental | `Accidental` | |
| altura | pitch | `Pitch` | step + alter + octave |
| nombre de nota | step | `Step` | C D E F G A B |
| enarmonía | enharmonic | `isEnharmonicWith` | Same sound, different spelling |
| afinación | tuning | `Tuning` | |
| cuerda | string | `string` (index) | 1 = highest |
| traste | fret | `fret` | Relative to the capo |
| posición (cuerda/traste) | fret position | `FretPosition` | |
| cejilla | capo | `capo` | |
| bend | bend | `Bend` | Curve of (time, pitch) points |
| ligado ascendente / descendente | hammer-on / pull-off | `Legato` | |
| deslizamiento | slide | `Slide` | |
| palm mute | palm mute | `PalmMute` | |
| nota muerta | dead note | `deadNote` | |
| armónico natural / artificial | natural / artificial harmonic | `Harmonic` | |
| vibrato | vibrato | `Vibrato` | |
| dinámica | dynamic | `Dynamic` | pp ... ff |
| repetición / casilla | repeat / volta | `Repeat`, `Volta` | |
| tempo | tempo | `Tempo` | BPM map |
| metrónomo | metronome | `Metronome` | |
| bucle A-B | A-B loop | `LoopRange` | |
| afinador | tuner | `Tuner` | |
| banco de sonidos | sound bank | `SoundBank` | SF2/SF3/SFZ |
| mezclador | mixer | `Mixer` | |
