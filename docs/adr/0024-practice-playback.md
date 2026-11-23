# 0024. Practice playback: speed, metronome, count-in, loop

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 6 asks for the tools a player practises with: a slower or faster
speed (25 % to 200 %), a metronome, a count-in before playback starts, and an
A-B loop over a passage. Each could live in the sequencer, which turns the
song into timed events (ADR-0017), or in the player, which owns the transport
(ADR-0019). The offline renderer (ADR-0018) must keep playing exactly what
the player plays, because the tests check one against the other.

## Decision

**Speed scales the tempo.** `SequenceOptions::speed` multiplies every written
tempo, from `kMinSpeed` (0.25) to `kMaxSpeed` (2), clamped. A slower sequence
is a new sequence: measures, notes and events are all further apart, so the
player, the renderer and the score cursor need no idea of speed. What the
articulations time in seconds keeps its pace (a 5.5 Hz vibrato, a 0.1 s slide
in), as it does when a player slows down.

**The metronome is a track of events.** With `SequenceOptions::metronome`,
the sequencer adds a click on every beat of every played measure, on
`Sequence::metronomeTrack()`, one past the song's last track. A beat is the
time signature's pulse (`TimeSignature::pulseLength()`): the denominator's
value, or three of them in a compound meter, so 6/8 clicks two dotted
quarters. The first beat of a measure is a high wood block (General MIDI 76,
velocity 100), the others a low one a fifth below (77, velocity 80); each is
held 0.05 s, or until the next click. Clicks follow tempo changes inside a
measure, repeats and the speed, because they are timed like notes.

The renderer and the player play that track through a drum synth at a fixed
level in the centre (`kMetronomeLevel`). It is left out of the headroom and
the track mix: turning it on does not make the song quieter, and muting
every track leaves the clicks.

**Count-in and loop belong to the player.** Both depend on where playback
starts, which changes with every click on the score, while a sequence is built
once per edit.

- `Sequence::countInAt(seconds)` says what a count-in from there is: the
  beats of one measure of the measure played there, at the tempo there (a
  `CountIn` of so many clicks so many seconds apart). `Player::play(countIn)`
  clicks them on its own wood-block synth before it starts, with the position
  held still; `isCountingIn()` says when. A pause ends a count-in, and a play
  without one starts at once.
- `Player::setLoop(from, to)` makes playback that reaches `to` go on from
  `from`, silencing what sounds as a seek does. Playback that starts after
  `to` runs on to the end, and `to <= from` ends the loop. The two frames are
  packed into one 64-bit atomic, so the audio thread never sees half a loop.

The offline renderer has neither: it plays the song from its start to its
end, as an export does.

## Consequences

- Changing the speed rebuilds the sequence on the control thread (28 ms for
  the 160k-note benchmark song) and loads it; the app keeps the place by
  scaling the position by the ratio of the speeds.
- The metronome adds two events per beat. The song's own events, notes and
  measures are the same with it as without it.
- `TimeSignature::isCompound()` and `pulseLength()` are now the one place that
  decides how a meter is counted; validation's rests and notation's beams use
  them.
- `elara-cli sequence`, `render-audio` and `play` take `--speed PERCENT` and
  `--metronome`; `sequence --count-in-at SECONDS` prints a count-in, and
  `play` takes `--count-in` and `--loop FROM TO`. The player's count-in and
  loop are checked by unit tests that render it block by block.
