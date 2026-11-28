# 0177. Guitar Pro 6 drums by kit element

- Status: Accepted
- Date: 2026-11-28

## Context

Guitar Pro 7 and 8 give a drum note an articulation of the kit the file itself
describes, with the MIDI key it sounds (ADR-0010). Guitar Pro 6 files carry no
kit: the track only says it is one (`<GeneralMidi table="Percussion">`), and
every drum note names an element of the program's built-in kit and one of its
variations:

```xml
<Property name="Element"><Element>10</Element></Property>
<Property name="Variation"><Variation>2</Variation></Property>
```

The reader had no table for those numbers, so it dropped every such note with
the warning "drum notes without a MIDI key dropped". A Guitar Pro 6 song with
5266 drum notes opened with an empty drum staff. No public specification gives
the numbers.

## Decision

**The elements are the kit's pieces in the order Guitar Pro 6 lists them**, and
each variation sounds the General MIDI percussion key of that piece:

| Element | Piece | Variations and keys |
|---|---|---|
| 0 | Kick | 36 |
| 1 | Snare | hit 38, rim shot 40, side stick 37 |
| 2, 3, 4 | Cowbell low, medium, high | 56 (General MIDI has one cowbell) |
| 5 to 9 | Toms, very low to very high | 41, 43, 45, 48, 50 |
| 10 | Hi-hat | closed 42, half open 46, open 46 |
| 11 | Pedal hi-hat | 44 |
| 12, 13 | Crash medium, high | 49, 57 |
| 14 | Splash | 55 |
| 15 | Ride | middle 51, edge 51, bell 53 |
| 16 | China | 52 |

- A variation General MIDI has no key for (a choked cymbal, a cowbell's tip, a
  ride's edge) sounds as the piece's plain hit. Every element takes variations
  0 to 2.
- The five toms take General MIDI's lowest two, its middle one and its highest
  two, so the drum map (ADR-0035) puts the lowest pair in the floor tom's space
  and the highest pair on the high tom's line.
- A note that gives a MIDI key still keeps it first. An element past 16 or a
  variation past 2 is dropped with the warning "drum notes of unknown kit
  elements dropped".

The order was checked against a Guitar Pro 6 song that cannot be shared, by
where each element falls. Element 0 is on the first beat of 381 measures, and
the crashes (12, 13) mostly sound with it. The snare (1) and the open hi-hat
(10, 2) share the backbeat. The ride (15) plays with kick and snare, and the
toms (5 to 9) sit in fills. Two readings stay less certain because that song
does not settle them: the order of the snare's rim shot and side stick, and of
the ride's edge and bell. A file that shows otherwise becomes a corpus case and
a fix.

## Consequences

- Guitar Pro 6 drum tracks open with their notes, on the staff and in playback.
- The corpus duet's note that names only the snare now reads as one, and its
  warning is gone.
- Half-open and open hi-hats, and a cymbal and its choke, cannot be told apart
  after import; nothing in the model keeps them apart yet.
