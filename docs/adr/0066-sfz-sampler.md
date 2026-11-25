# 0066. SFZ instruments through Elara's own sampler

- Status: Accepted
- Date: 2026-11-25

## Context

The scope (`docs/SCOPE.md`) plans guitar articulations on SFZ instruments,
the open text format of sampled instruments, through sfizz. The free guitar
and bass libraries that fit the licence rules are SFZ: Karoryfer's Black and
Green Guitars, Shinyguitar and Black and Blue Basses are CC0.

Looking at sfizz before using it:

- Its repository was archived in 2026 and has no maintained fork; it is not
  in vcpkg. Building it means carrying its bundled Abseil and a dozen other
  libraries, and every fix, on three compilers.
- It plays one MIDI channel per synthesizer. Elara bends, slides and vibrates
  each note on its own (ADR-0023): the FluidSynth backend gives every note a
  channel of its own (ADR-0043). With sfizz each note would need a whole
  synthesizer, with its own copy of the samples' heads in memory.

What a guitar library needs of SFZ is small: regions picked by key, velocity,
controllers, keyswitch and round robin, an envelope, loops, release samples,
exclusive groups, and controllers at fixed values. The modulation sfizz adds
(LFOs, curves, filters, effects) is what the libraries use for vibrato and
expression, which Elara plays itself from the score.

## Decision

**Elara plays SFZ with a sampler of its own, in `elara_audio`.** It needs
only the standard library, like the built-in synthesizer, so no new target,
dependency or guard rule.

- `parseSfz()` / `readSfz()` (`sfz.hpp`) read the text: the `<control>`,
  `<global>`, `<master>`, `<group>` and `<region>` headers, `#include`,
  `#define`, comments, sample paths with spaces and `default_path`. A region
  folds in the opcodes of the headers above it. Opcodes it does not play are
  kept, each once with their numbers as "N", so `elara-cli sfz` can say what
  a library loses.
- Played opcodes: key and velocity ranges, key centre, tune, transpose, pitch
  key tracking, volume, amplitude, velocity tracking and curve points,
  amplitude key tracking, the DAHDSR amplitude envelope, round robin
  (`seq_length`, `seq_position`), triggers (attack, release with `rt_decay`,
  first, legato), controller ranges and `set_ccN`, keyswitches (`sw_lokey`,
  `sw_hikey`, `sw_last`, `sw_default`), exclusive groups (`group`, `off_by`,
  `off_mode`), loop modes and points, offset and end, and controllers moving
  amplitude (multiplying), volume, tune and the envelope.
- `SfzBank` is a `SoundBank`. It reads every WAV sample once, as mono floats,
  on every core, and shares them with every track it plays. Percussion tracks
  go to another bank: the SoundFont when there is one, or else the built-in
  synthesizer.
- An instrument plays every pitched track, or only the tracks of a General
  MIDI guitar or bass (`SfzTracks`), and leaves the others to the bank behind
  it, so instruments stack: a SoundFont, a bass library in front of it and a
  guitar library in front of both. The app offers one instrument for the
  guitars and one for the basses; `elara-cli` takes `--sfz` as often as needed,
  `guitars=FILE`, `basses=FILE` or `FILE` for every pitched track.
- Its `TrackSound` has 64 voices made in advance. A note plays every region
  that matches it, read with four-point Hermite interpolation at the key's
  pitch and the sample's rate. A glide moves only the voices of its key, a
  straight line in cents (a constant factor on the read step each frame), so
  bends, slides and vibrato are exact per note. Palm mutes and dead notes
  shorten and darken a note and harmonics soften it, as the built-in
  synthesizer does, since libraries map these touches in their own ways.
  Everything happens per frame, so the output never depends on block sizes.
- `elara-cli render-audio --sfz` and `play --sfz` play a song with an SFZ
  instrument, and `elara-cli sfz FILE` reports its regions, key range,
  samples, memory, missing samples and opcodes not played.

## Consequences

- Real libraries load: the Black and Green Guitars twang program reads as 3408
  regions over keys 40 to 86. What it loses is LFO vibrato, the shapes of
  controller curves, pitch envelopes on controllers and pan: the first three
  are mostly idle at the controllers' default values, and a track is mono
  before the mixer places it.
- Elara owns a sampler: a feature a library needs and the sampler lacks is
  ours to add, test first, as the voice and envelope tests do now.
- Samples are held whole in memory as floats: a 500 MB library of 24-bit
  stereo WAV takes about 330 MB. Streaming from disk, FLAC and Ogg samples
  and a choice of SFZ in the app are later tasks.
- Controllers are fixed at their `set_ccN` values; the score does not drive
  them yet (techniques could map to keyswitches and controllers per library).
