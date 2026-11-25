# elara_core

The single musical model that tablature, standard notation, piano roll and the
DAW timeline all read and edit (see `docs/architecture.md`).

- **May depend on:** the C++ standard library only.
- **Must not include:** Qt (`<Q...>`), JUCE (`<juce_...>`), or any other Elara module.
- **Status:** milestone 1 in progress (see `docs/roadmap.md`).

## Time

| Header | What it gives you |
|---|---|
| `fraction.hpp` | `Fraction`: exact, always-normalised rational. Every duration and time position is a `Fraction` of a whole note, so tuplets never drift. |
| `duration.hpp` | `NoteValue`, `Tuplet`, `Duration` (value + dots + tuplet), its sounding `length()` and text form (`"4"`, `"8."`, `"8*3:2"`, `"breve"`). |

Positions are measured in whole notes from a reference point (the start of the
song or of a measure) and are plain `Fraction`s.

| Header | What it gives you |
|---|---|
| `signature.hpp` | `TimeSignature` (measure and beat length, "6/8" text) and `KeySignature` (circle-of-fifths position + `KeyMode`; tonic, accidentals, "Bb minor" text, `spell()` of a MIDI number in context: diatonic first, raised leading tone in minor, then naturals, then sharps or flats by key). |
| `tempo.hpp` | `Tempo` (bpm of a beat unit, e.g. dotted quarter = 60) and `TempoChange` (a tempo at an offset inside a measure, `gradual` when it moves to the next change's; ADR-0082); `rampSeconds()` times a stretch of such a ramp. |

## Pitch

| Header | What it gives you |
|---|---|
| `pitch.hpp` | `Step`, `Pitch` (step + alter + octave, scientific notation: C4 = MIDI 60), `midiNumber()`, `fromMidi()` with a `SpellingPreference`, text round trip (`toString()`/`parse()`, e.g. "F#3", "Bb2") and equal-tempered `frequency()`. |

A `Pitch` keeps its spelling: B#3, C4 and Dbb4 are different pitches that sound
the same (`isEnharmonicWith`). Spelling from context (key signature) comes with
`KeySignature`.

## Fretted instruments

| Header | What it gives you |
|---|---|
| `tuning.hpp` | `Tuning` (1 to 12 open strings, string 1 = top tab line), `midiAt(string, fret, capo)`, `positionsFor(pitch, capo, maxFret)` returning `FretPosition`s, text form ("E2 A2 D3 G3 B3 E4", lowest tab line first) and named presets (guitar, drop D, DADGAD, 7/8-string, bass 4/5/6, ukulele, mandolin, banjo). |

Tablature frets are written relative to the capo: with a capo at 2, fret 0 on the
sixth string of a guitar sounds F#2.

## Song structure

`Song > Track > Staff > Measure > Voice > Beat > Note` (ADR-0004).

| Header | What it gives you |
|---|---|
| `effects.hpp` | `NoteEffects` (tie and hammer-on/pull-off from the previous note, slides in/out, `BendCurve` in cents, `Harmonic`, vibrato, palm mute, dead and ghost notes, let ring, staccato, accent, `Dynamic`) and `BeatEffects` (pick stroke, strum, tremolo picking, whammy bar, bass tap/slap/pop, `Hairpin`: consecutive beats with the same crescendo or diminuendo share one wedge, ADR-0078); `naturalHarmonicSemitones()`, `dynamicVelocity()`; `BendForm` with `bendForm()`, read from a curve's shape, and `bendCurve()` (ADR-0055); `whammyForm()`, `whammyCents()` and `whammyCurve()`, the same forms going down or up for the whammy bar (ADR-0060). |
| `measure.hpp` | `Note` (`Pitch` + optional `FretPosition` + effects), `Beat` (duration + notes, no notes = rest; the `Grace` notes leading into it, which take no time, ADR-0079; the `ChordSymbol` named above it, a name and an optional `ChordDiagram`, which does not sound, ADR-0080), `Voice` (beats; `length()`, `startOf()`), `Measure` (four voices). |
| `instrument.hpp` | `InstrumentKind`, `Clef`, `Instrument` (GM program, written offset) and data-only `InstrumentPreset`s: guitars, basses, ukulele, mandolin, banjo, drum kit, piano, voice, violin, flute, trumpet, saxophones. `drumPieces()`: the kit pieces of drum entry, top of the staff first (ADR-0035). |
| `song.hpp` | `Staff` (clef, tuning, capo, measures shared with the staff's copies until one changes, ADR-0051; `frettedNote()`, `setTuning()`), `Track` (instrument + staves, `fromPreset()`, its `TrackMix`: volume, pan, mute, solo, ADR-0020), `MeasureHeader`, `SongInfo` (`Credit` names its lines, `creditOf()` reads or writes one; ADR-0059), `Song` (`audible()`: whether mute and solo let a track sound, as `TrackMix::audible()` decides). |
| `document.hpp` | `Document` (a song being edited: undo/redo history with a limit, save point, `isModified()`) and the `Command` interface (ADR-0005). |
| `commands.hpp` | The edits: `InsertMeasures`, `RemoveMeasures`, `InsertTrack`, `RemoveTrack`, `ReplaceMeasure` (any note-level edit, named after it), `SetMeasureHeader`, `SetSongInfo`, `SetStartTempo`, `SetTrackMix`, `SetStaffTuning` and `CommandGroup` (one undo step). `changeTimeSignature()` and `changeKeySignature()` change a signature from a measure until the next change, and `changeTempo()` sets or removes the tempo that starts a measure (ADR-0042); `changeSection()` names the section that starts at a measure (ADR-0062). `changeCredit()` sets one line of the credits (ADR-0059). |
| `clipboard.hpp` | `MeasureClip` (copied headers and, per track and staff, measures with their tuning and capo), `copyMeasures()` and `makePaste()`: one undo step that pastes over measures or into new ones (`PasteMode`), placing fretted notes on the target's strings (ADR-0027). |
| `entry.hpp` | Keyboard entry (ADR-0021): a `Caret` on a beat and a string, `moveCaret()` for the arrow keys, `clampCaret()`, and the edits typing makes (`enterFret()`, `enterPitch()` for note names in the nearest octave, `shiftPitch()`, `setBeatDuration()`, `deleteAtCaret()`, `deleteBeat()`, `insertRest()`), each returned as a command plus the caret after it. Short measures have a free slot after their last beat, where typing adds a beat (ADR-0034 for note names). On a percussion staff the caret's string is a drum lane and `toggleDrum()` adds or removes the lane's kit piece (ADR-0035). `toggleEffect()` toggles a note effect (tie, legato, slide, bend, vibrato, palm mute, dead note, let ring, harmonic, ghost, accent, staccato) on the caret's notes, and `hasEffectAtCaret()` says whether they all have it (ADR-0037, ADR-0038); the techniques of strings (`needsStrings()`: legato, palm mute, dead note, harmonic) go on fretted staves only (ADR-0077). `setBend()` sets a bend of any curve on them and `bendAtCaret()` reads it (ADR-0055). `setBeatEffects()` sets the caret beat's strum, pick stroke, tremolo picking, whammy bar and bass technique, and `beatEffectsAt()` reads them (ADR-0056). `addGraceNote()` adds a grace note some semitones from the caret's note, `removeGraceNotes()` takes the beat's off and `graceNotesAt()` counts them (ADR-0079). `setSlides()`, `setVibrato()`, `setAccent()` and `setHarmonic()` set those effects of the caret's notes to any of their kinds, and `noteEffectsAt()` reads them (ADR-0058). `setDynamic()` sets a dynamic from the caret's beat to the next change, over every voice, and `dynamicAt()` is the dynamic in force there, which new notes take (ADR-0047). |
| `transpose.hpp` | `Interval` (diatonic steps + semitones; `fromSemitones()` through the key with the fewest accidentals), `transposed()` for a `Pitch` or a `KeySignature`, `transposedChordName()` and `transposedDiagram()` for chord symbols (ADR-0080), and `makeTranspose()`: one undo step that moves measures of some or all tracks, their keys when every pitched track moves, their chord symbols, and fretted notes to strings that can play them (ADR-0026). |
| `chord_name.hpp` | `ChordPitch` and `splitChordName()`: a chord symbol's name as its root, suffix and bass ("Bb7/F"), for writers and transposition (ADR-0080). |
| `retune.hpp` | `Retuning` and `makeRetune()`: one undo step that gives a fretted staff another tuning and capo, keeping its notes' pitches (frets move, as in transposition) or their frets (pitches move, spelled in the key; `Retune`) (ADR-0046). |
| `performance.hpp` | `soundingNotes(song)`: every note as it sounds (start and length in whole notes from the start of the song, ties joined, velocity from the dynamic and ramped under hairpins (ADR-0078), grace notes borrowing time from the beat before or their own (ADR-0079), and the measure, beat, grace and note of its attack, where playback finds its articulations; `noteOf()` finds that note), ordered by time, each voice read on its own thread; what MIDI export writes and the sequencer builds on (ADR-0009, ADR-0017). |
| `validation.hpp` | `voiceFill()` (empty, incomplete, complete, overfull), `fillWithRests()` and `validate(song)`, which lists every `Issue`: voices that do not fill their measure, fret positions on missing strings, beyond the neck, sounding another pitch or on a staff without tuning, two notes on one string, ties with nothing to tie from. |

## Utilities

| Header | What it gives you |
|---|---|
| `shared_value.hpp` | `SharedValue<T>`: a value its copies share until one of them changes it (`edit()` copies it first when shared), safe to copy and read on other threads (ADR-0051); `SharedOptional<T>`: an optional value its copies share and nobody changes, compared by value, so a beat without one costs a null pointer (ADR-0080) |
| `parallel.hpp` | `forEachIndex()`: independent jobs on the calling thread and the threads of `WorkerPool::shared()`, kept waiting so a parallel call starts no threads (ADR-0053); the first exception is rethrown and nested calls are allowed. `threadsFor()`: how many threads a job count and amount of work deserve. Used by import, export, sequencing and notation layout. |

`Song` owns the number of measures: `insertMeasures`/`removeMeasures` change all
staves at once, and `addTrack` gives a new track as many measures as the song.
Each `MeasureHeader` holds the time signature, the concert key and the tempo
changes of its measure, so they move with it; new measures continue the meter
and key before them. `Song::measureStart()`, `secondsAt()`, `tempoAt()` and
`durationSeconds()` give the timeline of the measures as written, gradual
tempo changes included (ADR-0082); the
header's repeats and alternate endings (ADR-0016), signs and jumps
(ADR-0049) say how they are played, and `playOrder()` lists the measures in
that order. A header's `section` names the part of the song that starts there
("Intro", "Chorus"; ADR-0062).
Beat positions are derived from durations, never stored. How each instrument
family is handled: `docs/instruments.md`.

Editing may leave a voice short or long; `validate()` reports it instead of
refusing the edit. An empty voice counts as a measure rest. `fillWithRests()`
completes a short voice the way engravers do: first the current beat (an
unfinished tuplet with rests of the same tuplet), then the largest rests that
start on a multiple of their own length, so beats stay visible (4/4 after a
quarter: quarter + half, never a dotted half; 6/8 uses dotted rests by beat).
It changes nothing and returns false when the gap cannot be written that way.

Build a song with the `Song` API (importers, tests); once it is open for
editing, wrap it in a `Document` and change it only through commands. Commands
swap or move data with the song instead of copying it, so undo costs memory in
proportion to what was edited (ADR-0005). A new kind of edit needs a command
whose `revert()` restores the song exactly; the random-edit test in
`tests/core/test_document.cpp` checks that with `Song ==`.

The time types above are small `constexpr` value types without allocation; keep them
that way, the sequencer and layout engine run them in hot loops
(`python scripts/elara.py bench`).

Public headers live in `include/elara/core/`, sources in `src/`, tests in
`tests/core/`.
