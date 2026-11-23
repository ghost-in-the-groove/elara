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
| `tempo.hpp` | `Tempo` (bpm of a beat unit, e.g. dotted quarter = 60) and `TempoChange` (a tempo at an offset inside a measure). |

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
| `effects.hpp` | `NoteEffects` (tie and hammer-on/pull-off from the previous note, slides in/out, `BendCurve` in cents, `Harmonic`, vibrato, palm mute, dead and ghost notes, let ring, staccato, accent, `Dynamic`) and `BeatEffects` (pick stroke, strum, tremolo picking, whammy bar, bass tap/slap/pop); `naturalHarmonicSemitones()`, `dynamicVelocity()`. |
| `measure.hpp` | `Note` (`Pitch` + optional `FretPosition` + effects), `Beat` (duration + notes; no notes = rest), `Voice` (beats; `length()`, `startOf()`), `Measure` (four voices). |
| `instrument.hpp` | `InstrumentKind`, `Clef`, `Instrument` (GM program, written offset) and data-only `InstrumentPreset`s: guitars, basses, ukulele, mandolin, banjo, drum kit, piano, voice, violin, flute, trumpet, saxophones. |
| `song.hpp` | `Staff` (clef, tuning, capo, measures; `frettedNote()`), `Track` (instrument + staves, `fromPreset()`), `MeasureHeader`, `SongInfo`, `Song`. |
| `document.hpp` | `Document` (a song being edited: undo/redo history with a limit, save point, `isModified()`) and the `Command` interface (ADR-0005). |
| `commands.hpp` | The edits: `InsertMeasures`, `RemoveMeasures`, `InsertTrack`, `RemoveTrack`, `ReplaceMeasure` (any note-level edit, named after it), `SetMeasureHeader`, `SetSongInfo`, `SetStartTempo` and `CommandGroup` (one undo step). |
| `entry.hpp` | Keyboard entry (ADR-0021): a `Caret` on a beat and a string, `moveCaret()` for the arrow keys, `clampCaret()`, and the edits typing makes (`enterFret()`, `setBeatDuration()`, `deleteAtCaret()`, `deleteBeat()`, `insertRest()`), each returned as a command plus the caret after it. Short measures have a free slot after their last beat, where typing adds a beat. |
| `performance.hpp` | `soundingNotes(song)`: every note as it sounds (start and length in whole notes from the start of the song, ties joined, velocity from the dynamic), ordered by time, each voice read on its own thread; what MIDI export writes and the sequencer builds on (ADR-0009, ADR-0017). |
| `validation.hpp` | `voiceFill()` (empty, incomplete, complete, overfull), `fillWithRests()` and `validate(song)`, which lists every `Issue`: voices that do not fill their measure, fret positions on missing strings, beyond the neck, sounding another pitch or on a staff without tuning, two notes on one string, ties with nothing to tie from. |

## Utilities

| Header | What it gives you |
|---|---|
| `parallel.hpp` | `forEachIndex()`: independent jobs on several threads (or the calling one), exceptions rethrown in order; `threadsFor()`: how many threads a job count and amount of work deserve. Used by import, export and notation layout. |

`Song` owns the number of measures: `insertMeasures`/`removeMeasures` change all
staves at once, and `addTrack` gives a new track as many measures as the song.
Each `MeasureHeader` holds the time signature, the concert key and the tempo
changes of its measure, so they move with it; new measures continue the meter
and key before them. `Song::measureStart()`, `secondsAt()`, `tempoAt()` and
`durationSeconds()` give the timeline of the measures as written; the
header's repeats and alternate endings say how they are played, and
`playOrder()` lists the measures in that order (ADR-0016).
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
