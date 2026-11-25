# 0077. Techniques of strings go on fretted staves only

- Status: Accepted
- Date: 2026-11-25

## Context

Typing toggles twelve note effects (ADR-0037) and the app's effects panel
shows them all, with rows for bends, slides, harmonics, the whammy bar and
tapping, slap or pop (ADR-0038, ADR-0055 to ADR-0060). Some of them only mean
something on a string held against a fret: a hammer-on or pull-off, a palm
mute, a dead note, a natural harmonic. On a piano or a flute the panel still
offered them, the keys still set them, and only harmonics were refused. The
panel was twice as long as a keyboard player needs, and a stray key wrote
marks that no keyboard plays.

## Decision

**Hammer-ons and pull-offs, palm mutes, dead notes and natural harmonics
(`core::needsStrings()`) go on notes of fretted staves only.**

- `toggleEffect()` throws `std::logic_error` when one of them would go on a
  staff without a tuning, and the app says which effect needs strings. One
  that a file already put on such a note can still be taken off.
- On a staff without strings the effects panel hides their switches, the
  harmonic kinds, the whammy bar and tapping, slap and pop, and calls the
  strums rolls (upwards for a downstroke, downwards for an upstroke).
- Bends, slides, vibrato, let ring and the rest stay on every staff: a
  synthesizer bends, a violin slides, a voice has vibrato.

## Consequences

- The panel on a keyboard, wind or voice track lists what those instruments
  play, and the keys of string techniques do nothing there but say why.
- Imported files keep whatever effects they have; only typing is limited.
- A future instrument kind with strings but no frets (a harp) would need the
  rule to look at strings rather than at a tuning.
