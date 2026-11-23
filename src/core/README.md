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
| `duration.hpp` | `NoteValue`, `Tuplet`, `Duration` (value + dots + tuplet) and its sounding `length()`. |

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
| `measure.hpp` | `Note` (sounding `Pitch` + optional `FretPosition`), `Beat` (duration + notes; no notes = rest), `Voice` (beats; `length()`, `startOf()`), `Measure` (four voices). |
| `instrument.hpp` | `InstrumentKind`, `Clef`, `Instrument` (GM program, written offset) and data-only `InstrumentPreset`s: guitars, basses, ukulele, mandolin, banjo, drum kit, piano, voice, violin, flute, trumpet, saxophones. |
| `song.hpp` | `Staff` (clef, tuning, capo, measures; `frettedNote()`), `Track` (instrument + staves, `fromPreset()`), `MeasureHeader`, `SongInfo`, `Song`. |

`Song` owns the number of measures: `insertMeasures`/`removeMeasures` change all
staves at once, and `addTrack` gives a new track as many measures as the song.
Each `MeasureHeader` holds the time signature, the concert key and the tempo
changes of its measure, so they move with it; new measures continue the meter
and key before them. `Song::measureStart()`, `secondsAt()`, `tempoAt()` and
`durationSeconds()` give the timeline (repeats are not unrolled yet).
Beat positions are derived from durations, never stored. How each instrument
family is handled: `docs/instruments.md`.

The time types above are small `constexpr` value types without allocation; keep them
that way, the sequencer and layout engine run them in hot loops
(`python scripts/elara.py bench`).

Public headers live in `include/elara/core/`, sources in `src/`, tests in
`tests/core/`.
