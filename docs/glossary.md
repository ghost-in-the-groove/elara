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
| tablatura | tablature | view of a fretted `Staff` | Not a separate type (ADR-0004); `StaffView::Tablature` in layout, `TrackView` for whole tracks |
| sistema | system | `layoutSystem()`, `SystemBox` | Staves laid out together, notes that start together aligned (ADR-0013); a line of a page (ADR-0014) |
| página | page | `layoutPages()`, `PageLayoutOptions` | Systems stacked between margins, with the title on the first (ADR-0014) |
| salto de línea | line break | `LineBreaker` | Where one system ends and the next starts, chosen to space the systems evenly (ADR-0014) |
| justificar | justify | stretch | Stretch or tighten a system's spacing so that it fills the width (ADR-0014) |
| muelle | spring | `Spring` | The space after a column of notes: an ideal length and the least its ink needs (ADR-0014) |
| créditos | credits | `Role::Credit` | Title, subtitle, artist, words and music by, copyright and page numbers |
| lista de visualización | display list | `DisplayList` | What layout produces: glyphs, lines, beams, text and curves in staff spaces (ADR-0012) |
| carril de técnicas | technique lane | `Lane` | Band above a staff for palm mute, let ring, vibrato... (ADR-0013) |
| clave | clef | `Clef` | |
| compás (unidad) | measure | `Measure` | Not "bar"; one staff's content |
| cabecera de compás | measure header | `MeasureHeader` | Song-wide: signatures, repeats |
| indicación de compás | time signature | `TimeSignature` | |
| armadura | key signature | `KeySignature` | `fifths` + `KeyMode` (major/minor) |
| tónica | tonic | `tonic()` | |
| voz | voice | `Voice` | Up to 4 per staff |
| tiempo / pulso (posición) | beat | `Beat` | Group of notes starting together |
| nota | note | `Note` | |
| silencio | rest | `Beat::isRest()` | A beat without notes; an empty voice is a measure rest |
| compás incompleto / sobrecargado | incomplete / overfull voice | `VoiceFill` | Shorter / longer than its time signature |
| incidencia | issue | `Issue`, `IssueKind` | Inconsistency found by `validate()` |
| documento | document | `Document` | A song being edited: song + undo/redo history + save point |
| orden / acción de edición | command | `Command` | One reversible edit (`apply`/`revert`), ADR-0005 |
| deshacer / rehacer | undo / redo | `undo()`, `redo()` | |
| cursor de edición | caret | `Caret` | Beat and string where typing enters notes; the free slot follows the last beat of a short measure (ADR-0021) |
| punto de guardado | save point | `markSaved()`, `isModified()` | |
| archivo nativo | native file | `.elara`, `saveNative()`, `loadNative()` | Zip with `song.json` (ADR-0006) |
| importación / aviso | import / warning | `ImportResult`, `warnings` | A foreign file read into the model; each warning is a feature it had that the model cannot hold yet (ADR-0007) |
| exportación | export | `ExportResult`, `exportMusicXml()` | The song written in a foreign format; each warning is a feature that format cannot hold (ADR-0008) |
| GPIF | GPIF | `importGpif()` | The XML score inside Guitar Pro 6-8 files: master bars, bars, voices, beats and notes linked by id (ADR-0010). Guitar Pro 3-5 files are a binary stream instead |
| nota sonora | sounding note | `SoundingNote`, `soundingNotes()` | A note as it sounds: start and length in whole notes from the start of the song, tied notes joined, velocity from the dynamic (ADR-0009) |
| secuencia / compás tocado | sequence / played measure | `audio::Sequence`, `PlayedMeasure`, `PlayedNote` | The song as it is played: measures in play order with repeats unrolled, notes and note events in seconds (ADR-0017) |
| evento de nota | note event | `NoteEvent`, `EventKind` | What a synthesizer is told: a key struck (`NoteOn`) or released (`NoteOff`) on a track at a time |
| timbre | timbre | `audio::Timbre`, `timbreFor()` | The sound the built-in synthesizer gives a track, picked from its instrument (ADR-0018) |
| reproductor / transporte | player / transport | `audio::Player`: `play()`, `pause()`, `seek()`, `position()` | Plays a song in real time; the transport is where it is and whether it moves (ADR-0019) |
| cursor de reproducción | playback cursor | `notation::cursorAt`, `ScoreDocument::cursorAt` | The line across a system that shows what plays; it stands on each column of notes as it starts |
| registro de eventos | event log | `LoggedEvent`, `OfflineRenderer::log()` | The note events a render dispatched, each with its sample frame; what playback tests compare |
| cuantización / rejilla | quantization / grid | MIDI import | Placing played or exported MIDI notes on a rhythm grid chosen per beat before writing them as notes, rests, ties and tuplets (ADR-0009) |
| figura / duración | duration, note value | `Duration`, `NoteValue` | Length is a `Fraction` of a whole note |
| posición temporal | time position | `Fraction` | Whole notes from the song or measure start |
| puntillo | dot | `dots` | |
| grupo irregular / tresillo | tuplet / triplet | `Tuplet` | |
| ligadura de prolongación | tie | `tiedFromPrevious` | Same pitch, no new attack |
| ligadura de expresión | slur | `Slur` | |
| plica | stem | `Stem` | |
| articulación | articulation | `staccato`, `Accent` | Staccato, accent, marcato |
| barra (de corcheas) | beam | `Beam` | |
| alteración | accidental | `Accidental` | |
| altura | pitch | `Pitch` | step + alter + octave |
| nombre de nota | step | `Step` | C D E F G A B |
| enarmonía | enharmonic | `isEnharmonicWith` | Same sound, different spelling |
| intervalo | interval | `Interval` | Diatonic steps and semitones: a major second is {1, 2} |
| portapapeles | clipboard | `MeasureClip`, `copyMeasures()`, `makePaste()` | Copied measures of some tracks, pasted over measures or into new ones (ADR-0027) |
| transposición | transposition | `Transposition`, `makeTranspose()` | Moving notes by an interval; keys follow when every pitched track moves (ADR-0026) |
| afinación | tuning | `Tuning` | |
| cuerda | string | `string` (index) | 1 = highest |
| traste | fret | `fret` | Relative to the capo |
| posición (cuerda/traste) | fret position | `FretPosition` | |
| cejilla | capo | `capo` | |
| bend | bend | `BendCurve` | Points of (percent of the note, cents) |
| ligado ascendente / descendente | hammer-on / pull-off | `legatoFromPrevious` | Up = hammer-on, down = pull-off |
| deslizamiento | slide | `SlideIn`, `SlideOut` | |
| palm mute | palm mute | `palmMute` | `P.M.` |
| dejar sonar | let ring | `letRing` | |
| púa abajo / arriba | pick stroke | `PickStroke` | Down / up |
| nota muerta | dead note | `deadNote` | |
| armónico natural / artificial | natural / artificial harmonic | `Harmonic` | Fret in tenths |
| ataque (de la cuerda) | touch | `audio::Touch` | How playback strikes a string: normal, palm mute, dead, harmonic (ADR-0022) |
| vibrato | vibrato | `Vibrato` | |
| dinámica | dynamic | `Dynamic`, `Role::Dynamic` | How loud a note is played, ppp ... fff (mf by default); a mark under the staff where it changes (ADR-0044) |
| repetición / casilla | repeat / alternate ending (volta) | `MeasureHeader::repeatStart`, `repeatTimes`, `alternateEndings` | `Song::playOrder()` unrolls them (ADR-0016) |
| tempo | tempo | `Tempo`, `TempoChange` | bpm of a beat unit; changes live in `MeasureHeader` |
| metrónomo | metronome | `Metronome` | |
| bucle A-B | A-B loop | `LoopRange` | |
| afinador | tuner | `Tuner`, `PitchDetector`, `tunerReading()` | YIN pitch detection; the nearest note, its cents and the nearest open string (ADR-0028); smoothed readings from the audio input (ADR-0029) |
| banco de sonidos | sound bank | `SoundBank` | SF2/SF3/SFZ |
| mezclador | mixer | `Mixer` | |
| mezcla de pista | track mix | `TrackMix` | Volume (fader position 0..1), pan (-1..1), mute, solo; `Song::audible()` (ADR-0020) |
