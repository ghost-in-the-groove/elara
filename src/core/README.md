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

## Pitch

| Header | What it gives you |
|---|---|
| `pitch.hpp` | `Step`, `Pitch` (step + alter + octave, scientific notation: C4 = MIDI 60), `midiNumber()`, `fromMidi()` with a `SpellingPreference`, text round trip (`toString()`/`parse()`, e.g. "F#3", "Bb2") and equal-tempered `frequency()`. |

A `Pitch` keeps its spelling: B#3, C4 and Dbb4 are different pitches that sound
the same (`isEnharmonicWith`). Spelling from context (key signature) comes with
`KeySignature`.

All of these are small `constexpr` value types without allocation; keep them
that way, the sequencer and layout engine run them in hot loops
(`python scripts/elara.py bench`).

Public headers live in `include/elara/core/`, sources in `src/`, tests in
`tests/core/`.
