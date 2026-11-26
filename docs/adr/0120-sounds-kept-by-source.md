# 0120. Sounds kept by their source when the bank changes

- Status: Accepted
- Date: 2026-11-26

## Context

The app plays the tracks through a tree of sound banks: the SoundFont, or the
built-in synthesizer, with the guitars' and the basses' SFZ instruments over
it (`SfzBank::with()`, ADR-0066). Changing one part builds a new tree, and
`Player::setSoundBank()` then emptied the sounds it kept, because it compared
only the pointer of the whole tree. Every track got a new sound at the next
load: a SoundFont preset selected again, an SFZ voice set made again, though
the tree made most of them the same as before.

Going back to the built-in sound cleared the three parts one at a time, and
each applied the bank and asked for the player to be prepared again: up to
three intermediate states for one request.

## Decision

- `SoundBank::source(instrument)` says what makes the sound for an
  instrument: the bank itself by default; for an `SfzBank`, the samples it
  shares with every bank `with()` makes of it for the tracks it plays, and the
  other bank's source for the rest. Two banks with the same source for an
  instrument make the same sound for it.
- The player keeps a track's sound when the instrument and its source are the
  same, whatever bank the tree is. `setSoundBank()` no longer empties the
  sounds kept.
- A kept sound holds the bank last asked for it, so that the source it was
  compared by stays alive and no other bank's source can take its address. A
  sound kept through a new bank holds the new one, which lets the bank before
  go.
- `AudioController::useBuiltinSound()` sets the three parts, then applies the
  bank once.

## Consequences

- Changing the guitars' instrument makes again only the sounds of the guitar
  tracks; the basses and the other tracks go on with theirs. The same for the
  basses' instrument, and for a SoundFont kept under a new SFZ instrument.
- A bank that makes different sounds for an instrument over its life, or two
  banks that share a source but make different sounds, would break this. A
  new kind of bank that hands instruments to another bank must override
  `source()`.
- A SoundFont opened again from the same file is a new source: its sounds are
  made again.
