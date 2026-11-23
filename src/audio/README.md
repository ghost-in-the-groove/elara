# elara_audio / elara_audio_juce

Two targets with different rules:

| Target | Files | May depend on | Must not include |
|---|---|---|---|
| `elara_audio` | `include/`, `src/` | std, `elara_core` | Qt, JUCE |
| `elara_audio_juce` | `backend_juce/` | std, `elara_core`, `elara_audio`, JUCE | Qt; JUCE types in its public header |

`elara_audio` holds code that is easy to test without a sound card:

- the sequencer (`Sequence`: a song's measures in play order, its notes and
  note events in seconds, ADR-0017), with accents, ghost notes, staccato, let
  ring and harmonics applied (ADR-0022), and bends, slides, hammer-ons and
  vibrato sent as pitch events (ADR-0023);
- the built-in synthesizer (`Synth`: one track in one `Timbre`, picked from the
  instrument by `timbreFor()`; plucked timbres also play each note's `Touch`:
  palm mute, dead note or harmonic, ADR-0022; `glide()` moves a sounding
  voice's pitch, ADR-0023) and `OfflineRenderer`, which plays a sequence
  through one synth per track into stereo, with an event log (ADR-0018). Each
  track sits in the stereo mix at its volume and pan, and mute and solo
  silence tracks (ADR-0020);
- `Player`, the same in real time: songs are handed to the audio thread through
  atomic pointers and the transport is a set of atomics (ADR-0019), and
  `updateMix()` changes the track levels while it plays. It is an
  `AudioSource`, the interface a device plays;
- the tone generator, fade envelope and the WAV writers (whole buffers, or
  block by block with `WavFileWriter`).

`elara_audio_juce` opens the
system audio device through JUCE (ADR-0002) and exposes a plain C++ API
(`elara::audio::SystemAudio`, `listOutputDevices()`); `SystemAudio::setSource()`
plays an `AudioSource` such as a `Player`.

## Real-time rule

Code that runs inside the audio callback (`ToneGenerator`, `fadeGain`,
`Synth` after construction, `Player::render()`, the callback in
`system_audio.cpp`) must not allocate, lock, log, throw or do I/O.
Exchange data with the audio thread through atomics or lock-free queues only.

## Verifying without listening

`elara-cli render-tone --json --out tone.wav` renders the same DSP offline and
reports frames and peak level; `elara-cli sequence FILE --json --events` prints
what the sequencer would play; `elara-cli render-audio FILE --json --events
[--out song.wav]` plays it through the built-in synthesizer and prints the
level, each track's timbre and every event with the frame it was dispatched
at. `elara-cli play FILE` plays it on the default device. Tests in
`tests/audio/` check pitch, glides, envelopes, touches, voice stealing, event frames and the
player's transport by calling `render()` as an audio callback would; tests in
`tests/cli/` run the commands on corpus files.
