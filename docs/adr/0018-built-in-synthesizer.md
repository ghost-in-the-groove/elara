# 0018. The built-in synthesizer and offline rendering

- Status: Accepted
- Date: 2026-11-23

## Context

The sequencer (ADR-0017) says which keys go down and up, and when. Something
has to turn that into sound. The roadmap names FluidSynth with the MuseScore
General sound bank and sfizz with a guitar SFZ library. Both are large: a
library with its own dependencies, and tens of megabytes of samples that
neither the repository nor CI should carry. Tests must check what playback
does without speakers (ADR-0003), on every platform, with results that do not
depend on a downloaded file.

## Decision

**A small synthesizer of our own comes first.** `audio::Synth` lives in
`elara_audio`, plain C++ with no dependency. It is the sound Elara always has,
and the reference that tests and `render-audio` use. Sample-based engines come
later as other sound sources that take the same per-track calls: strike a
key, release a key, render the next frames.

**Timbres.** One `Synth` plays one track. Its timbre comes from the track's
instrument: the General MIDI program family picks one of a handful of sounds,
and percussion tracks get drums.

- Plucked strings (Karplus-Strong) for guitars and basses; overdriven guitars
  go through a soft clipper.
- Decaying partials for pianos and mallets; steady partials for organs.
- A filtered sawtooth for bowed strings, winds and synthesizers, with a slower
  attack for the bowed ones.
- Drums: one sound per General MIDI percussion key (kick, snare, toms,
  hi-hats, cymbals, others), made of a swept tone and filtered noise. A closed
  hi-hat chokes an open one.

These are stand-ins that make tablature audible, not instrument models. They
are kept cheap so that a 20-track song plays on one core.

**Real time.** After construction a `Synth` never allocates, locks or throws:
voices, delay lines and scratch buffers are sized up front. When every voice
sounds, a new note takes the quietest released voice, or else the oldest one.
A voice ends once it falls below -80 dB; that test is made sample by sample
and phasors are renormalized by the voice's own age, so the output does not
depend on how the frames are split into calls.

**Offline rendering.** `audio::OfflineRenderer` plays a `Sequence` through
one `Synth` per track into interleaved stereo, call by call, so a whole song
never has to sit in memory and `render-audio` streams it to a WAV file.

- An event at t seconds is dispatched at frame round(t × rate), between
  samples, and splits the block there.
- Tracks render on separate threads into their own buffers and are mixed in
  track order, so the output is the same on any number of threads and for any
  call size.
- Until the mixer exists, every track has the same level, 1/√n for n sounding
  tracks, and a soft knee above 0.75 keeps the sum from clipping.
- A tail (2 s by default) after the last measure lets releases ring out.
- With logging on, the renderer records every event it dispatched with its
  frame: the event log that tests compare.

**Headless.** `elara-cli render-audio FILE [--out WAV] [--events]` renders
offline and reports frames, peak level and timbres, and with `--events` the
event log, in text or `--json`.

## Consequences

- Playback is testable on CI: event logs give exact frames, and the audio can
  be checked for pitch, level and silence without listening.
- The sound is basic. FluidSynth and sfizz are still the plan for realistic
  instruments; when they come, they sit beside the built-in synthesizer, which
  stays for tests and for machines without a sound bank.
- The real-time player (playback in the app) reuses the same per-track
  synthesizers and dispatch; what is left to decide there is the handover of a
  new sequence to the audio thread.
