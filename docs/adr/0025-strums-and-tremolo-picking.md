# 0025. Strums and tremolo picking in playback

- Status: Accepted
- Date: 2026-11-23

## Context

A beat can be strummed down or up, and tremolo-picked at an eighth, a
sixteenth or a thirty-second. Both were imported and drawn, but played as
plain notes (ADR-0023 left them out). Neither changes how a string sounds,
only when it is struck, so both belong in the sequencer (ADR-0017), which
already turns every written note into played notes and events.

## Decision

**A strum spreads a chord's attacks.** A downstroke strikes the beat's notes
from the lowest string up, an upstroke from the highest string down. Without
fret positions, pitch stands in for strings. Each string sounds `kStrumStep`
(0.01 s) after the one before, but never later than half the note, so the
strings of a short chord all sound. A strum takes the same time at any tempo,
because its speed is the hand's, as with slides (ADR-0023). Every note keeps
its written end, so a strummed chord is released together. Drums are not
strummed.

**Tremolo picking strikes the note again.** A tremolo-picked note is struck
as many times as its tremolo value fits in its played length (half the note
when staccato). That count is rounded to the nearest whole number, and is at
least one. The attacks are spread evenly over the note's played seconds. Each
attack is a played note of its own, so the existing rule for a key struck
while it sounds gives the release before each attack. A tremolo-picked drum
beat is a roll.

**One pitch line over all the attacks.** The bend, whammy bar, slides and
vibrato of a tremolo-picked note span the whole written note. Each attack
plays its part of that line and starts at the pitch the attack before it had
reached. A hammer-on or pull-off into the note joins its first attack only.
A slide out of the note goes to the next note from its last attack.

**Attacks back in order.** Strums and repeats move attacks out of written
order. When a song has either, the played notes are sorted again by start,
then track, staff, voice and key. Songs without them skip that sort.

## Consequences

- Strummed and tremolo-picked songs have more notes and events: a whole note
  tremolo-picked at sixteenths is struck 16 times, with a release before each
  attack after the first.
- The attacks of a tremolo-picked note are evenly spaced in seconds, so a
  tempo change inside the note does not change their spacing.
- Pick strokes (`PickStroke`) on single notes still sound alike: the
  built-in synthesizer makes no difference between a down and an up stroke.
- `elara-cli sequence --events` shows the spread and repeated attacks. The
  `cli.sequence_strums` test checks them on a test file.
