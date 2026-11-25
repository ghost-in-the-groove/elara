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
| clave | clef | `Clef` | A staff's clef; a measure can change it until the next change, `Staff::clefAt()` (ADR-0087) |
| compás (unidad) | measure | `Measure` | Not "bar"; one staff's content |
| cabecera de compás | measure header | `MeasureHeader` | Song-wide: signatures, repeats |
| indicación de compás | time signature | `TimeSignature` | |
| armadura | key signature | `KeySignature` | `fifths` + `KeyMode` (major/minor) |
| tónica | tonic | `tonic()` | |
| voz | voice | `Voice` | Up to 4 per staff |
| tiempo / pulso (posición) | beat | `Beat` | Group of notes starting together |
| nota | note | `Note` | |
| silencio | rest | `Beat::isRest()` | A beat without notes; an empty voice is a measure rest |
| cifrado (acorde) | chord symbol | `ChordSymbol`, `Beat::chord` | The harmony named above a beat ("Am7", "C/G"); it does not sound (ADR-0080) |
| texto (de tiempo) | beat text | `Beat::text` | Words written above a beat ("let it ring", "ad lib."); it does not sound (ADR-0081) |
| letra | lyrics | `Beat::lyrics` | The words sung, a syllable per verse on each beat that sings it (ADR-0084) |
| estrofa | verse | | One line of lyrics: verse n + 1 is `Beat::lyrics[n]` (ADR-0084) |
| sílaba | syllable | `Syllable` | The part of a word sung on one beat; a hyphen joins it to the next one of its word (ADR-0084) |
| extensor (de sílaba) | extender | `Syllable::extend` | The line after a syllable held over the notes after it that sing nothing in its verse (a melisma) (ADR-0084) |
| diagrama de acorde | chord diagram | `ChordDiagram` | Where a chord's strings are stopped, from string 1 down, with the fret at the top of the grid (ADR-0080) |
| nota de adorno (apoyatura, mordente) | grace note | `Grace`, `Beat::graces` | Small notes that take no time, leading into the beat that holds them; on the beat or before it (ADR-0079) |
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
| ligadura de expresión | slur | `BeatEffects::slur`, `Role::Slur` | A curve over notes played as one phrase: each beat but the last slurred to the next of its voice (ADR-0086) |
| plica | stem | `Stem` | |
| articulación | articulation | `staccato`, `tenuto`, `Accent` | Staccato, tenuto (both: portato, ADR-0088), accent, marcato |
| barra (de corcheas) | beam | `Beam` | |
| alteración | accidental | `Accidental` | |
| altura | pitch | `Pitch` | step + alter + octave |
| nombre de nota | step | `Step` | C D E F G A B |
| enarmonía | enharmonic | `isEnharmonicWith` | Same sound, different spelling |
| intervalo | interval | `Interval` | Diatonic steps and semitones: a major second is {1, 2} |
| portapapeles | clipboard | `MeasureClip`, `copyMeasures()`, `makePaste()` | Copied measures of some tracks, pasted over measures or into new ones (ADR-0027) |
| transposición | transposition | `Transposition`, `makeTranspose()` | Moving notes by an interval; keys follow when every pitched track moves (ADR-0026) |
| afinación | tuning | `Tuning` | |
| reafinar | retune | `Retuning`, `makeRetune()` | A new tuning or capo for a fretted staff, keeping the pitches or the frets (ADR-0046) |
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
| rasgueo | strum | `Strum` | Down strikes a chord from its lowest string, up from its highest; 0.01 s between strings in playback (ADR-0025) |
| trémolo de púa | tremolo picking | `BeatEffects::tremoloPicking` | The note struck again and again at that note value (ADR-0025) |
| nota muerta | dead note | `deadNote` | |
| armónico natural / artificial | natural / artificial harmonic | `Harmonic` | Fret in tenths |
| ataque (de la cuerda) | touch | `audio::Touch` | How playback strikes a string: normal, palm mute, dead, harmonic (ADR-0022) |
| vibrato | vibrato | `Vibrato` | |
| dinámica | dynamic | `Dynamic`, `setDynamic()`, `dynamicAt()`, `Role::Dynamic` | How loud a note is played, ppp ... fff (mf by default); set at the caret, it holds until the next change (ADR-0047), and a mark under the staff shows where it changes (ADR-0044) |
| regulador | hairpin (crescendo, diminuendo) | `Hairpin`, `BeatEffects::hairpin`, `Role::Hairpin` | A wedge under the staff over beats that get louder or softer; consecutive beats with the same hairpin share one (ADR-0078) |
| calderón | fermata | `Fermata`, `BeatEffects::fermata`, `FermataHold`, `Role::Fermata` | A beat, a note or a rest, held longer than written while every voice waits: short (1.5 times), normal (twice) or long (three times) (ADR-0085) |
| mordente | mordent; upper mordent (inverted mordent) | `Ornament::Mordent`, `Ornament::UpperMordent`, `NoteEffects::ornament`, `semitonesToPreviousStep()` | Quick notes at the start of a note: down to the step below and back, or up to the step above and back (ADR-0091) |
| grupeto | turn; inverted turn | `Ornament::Turn`, `Ornament::InvertedTurn`, `ornamentSteps()` | Quick notes going round a note, from the step above (or below) to the other side and back (ADR-0091) |
| trino | trill | `Trill`, `NoteEffects::trill`, `semitonesToNextStep()`, `Role::Ornament` | A note alternated quickly with another, by default the next step up in the key, written "tr" over the note and a wavy line (ADR-0090) |
| línea de octava | octave line (8va, 8vb, 15ma, 15mb) | `Ottava`, `BeatEffects::ottava`, `ottavaOctaves()`, `Role::OctaveLine` | A dashed line over (or under) beats whose notes sound one or two octaves above (or below) where they are written (ADR-0089) |
| repetición / casilla | repeat / alternate ending (volta) | `MeasureHeader::repeatStart`, `repeatTimes`, `alternateEndings` | `Song::playOrder()` unrolls them (ADR-0016) |
| señal / coda / salto | sign (segno, coda) / jump (D.C., D.S., al Fine, al Coda) | `MeasureHeader::segno`, `coda`, `fine`, `toCoda`, `jump` (`JumpTo`), `jumpThen` (`JumpThen`) | Navigation marks; `Song::playOrder()` follows them (ADR-0049) |
| sección / marcador / ensayo | section (marker, rehearsal mark) | `MeasureHeader::section`, `changeSection()` | The named part of a song (intro, chorus...) that starts at a measure and runs to the next one that names a section (ADR-0062) |
| tempo | tempo | `Tempo`, `TempoChange` | bpm of a beat unit; changes live in `MeasureHeader` |
| cambio gradual de tempo | gradual tempo change | `TempoChange::gradual` | Accelerando or ritardando to the next change's tempo (ADR-0082) |
| metrónomo | metronome | `SequenceOptions::metronome` | A click on every beat, a high wood block on the first; events on `Sequence::metronomeTrack()` (ADR-0024) |
| pulso | pulse | `TimeSignature::pulseLength()` | The beat a player counts: three of the denominator's value in a compound meter (6/8: a dotted quarter) |
| compás compuesto | compound meter | `TimeSignature::isCompound()` | 6/8, 9/8, 12/8, 6/4 |
| velocidad de reproducción | playback speed | `SequenceOptions::speed` | 0.25 to 2 times every written tempo (ADR-0024) |
| bucle A-B | A-B loop | `Player::setLoop()` | Playback that reaches B goes on from A (ADR-0024) |
| claqueta (cuenta previa) | count-in | `CountIn`, `Sequence::countInAt()` | One measure of beats before playback starts, at the tempo there (ADR-0024) |
| afinador | tuner | `Tuner`, `PitchDetector`, `tunerReading()` | YIN pitch detection; the nearest note, its cents and the nearest open string (ADR-0028); smoothed readings from the audio input (ADR-0029) |
| banco de sonidos | sound bank | `audio::SoundBank`, `builtinBank()`, `SoundFont` | Where the tracks get their sound: the built-in synthesizer, or a SoundFont (SF2, SF3; ADR-0043) |
| sonido de pista | track sound | `audio::TrackSound` | One track's sound as it plays, made by a sound bank for its instrument (ADR-0043) |
| preset (de SoundFont) | preset | `SoundFontPreset`, `SoundFont::presetFor()` | One sound of a SoundFont, by bank and program; bank 128 holds drum kits |
| mezclador | mixer | `Mixer` | |
| mezcla de pista | track mix | `TrackMix` | Volume (fader position 0..1), pan (-1..1), mute, solo; `Song::audible()` (ADR-0020) |
