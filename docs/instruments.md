# Instruments

How Elara models instruments, and what each family needs. The model is
described in ADR-0004; this page is the plan for supporting each family.

## Principle: instruments are data

Adding an instrument should not need new code paths. An instrument is a preset:

| Field | Example (electric bass) |
|---|---|
| name | "Electric Bass (finger)" |
| kind | `Fretted`, `Percussion`, `Keyboard`, `Pitched` |
| staves and clefs | one staff, bass clef sounding an octave lower |
| tuning (fretted only) | `bass` preset, `E1 A1 D2 G2` |
| written offset | +12 semitones (written an octave above the sound) |
| General MIDI program | 33 (0-based) |
| default views | notation + tablature |

Code only differs by **kind**: fretted staves have a tuning and can show
tablature; percussion staves use a drum map; keyboards default to two staves.

## Families

### Guitars and other fretted instruments

- Sounding pitches come from the tuning (`Tuning` presets: standard, drop D,
  DADGAD, open G/D, 7 and 8 strings, half step down).
- Notation uses the treble clef with an 8 below (written an octave higher than
  it sounds): written offset +12.
- Tablature is a view of the same staff: string and fret come from each note's
  `FretPosition`.
- Ukulele, mandolin and banjo are the same kind with other tunings. Ukulele and
  banjo are re-entrant (a lower-numbered string is not always the highest note),
  which the model allows.
- Techniques (bends, slides, hammer-ons, harmonics, palm mute) are note effects
  (milestone 1, effects item).

### Bass

- 4, 5 and 6 strings: `bass`, `bass-5`, `bass-6`, `bass-drop-d` presets.
- Bass clef with an 8 below: written offset +12, like the guitar.
- Specific techniques to model as effects: slap, pop, tapping, ghost notes.
- GM programs: 32 acoustic, 33 finger, 34 pick, 35 fretless, 36 and 37 slap.

### Drum kit and percussion

- One percussion staff (percussion clef, five lines) or a one-line staff for a
  single instrument.
- A note's pitch is the **General MIDI percussion key** (35 to 81). The staff
  position, notehead and default stem direction come from a **drum map** that
  can be edited per track. Playback uses MIDI channel 10.
- Conventional drum map (treble-clef positions, so the staff reads like a
  normal five-line staff):

| Kit piece | GM key | Staff position | Notehead |
|---|---|---|---|
| Bass drum | 36 (35) | F4, first space | normal |
| Floor tom | 43 (41) | A4, second space | normal |
| Snare | 38 (40) | C5, third space | normal |
| Side stick | 37 | C5 | circled x |
| Mid tom | 47 (45) | D5, fourth line | normal |
| High tom | 50 (48) | E5, fourth space | normal |
| Ride cymbal | 51 (59) | F5, top line | x |
| Ride bell | 53 | F5 | diamond |
| Closed hi-hat | 42 | G5, space above | x |
| Open hi-hat | 46 | G5 + "o" above | x |
| Crash cymbal | 49 (57) | A5, ledger line above | x |
| Hi-hat pedal | 44 | D4, space below | x |

- Voices: hands in voice 1 (stems up), feet in voice 2 (stems down). This is a
  default of the drum map, not a rule of the model.
- Later: a drum-tablature view (one text line per kit piece) for players who
  read it.

### Keyboards

- Piano: one track with two staves (treble and bass clef); notes can cross
  staves. No tablature. Pedal marks are effects.
- GM program 0 (acoustic grand) and the 0-7 family.

### Other pitched instruments (voice, strings, winds, brass)

- One staff, the usual clef for the range, no tablature.
- Transposing instruments set the written offset: B-flat trumpet and clarinet +2,
  alto saxophone +9, tenor saxophone +14, horn in F +7. Notes keep the sounding
  pitch; the score can show concert or written pitch.
- Voice adds lyrics (later milestone).

## Order of work

1. Model: `Instrument` (kind, GM program, written offset), clef per staff,
   fretted staves with tuning and capo (milestone 1).
2. Importers map Guitar Pro and MusicXML instruments onto presets (milestone 2).
3. Notation: tablature, standard staff with written offset, percussion staff with
   the drum map (milestone 3).
4. Playback: GM programs through a SoundFont, drum kit on channel 10
   (milestone 4).
5. Editor: instrument picker with presets, drum map editor (milestone 5).
