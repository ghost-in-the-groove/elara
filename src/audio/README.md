# elara_audio / elara_audio_juce

Two targets with different rules:

| Target | Files | May depend on | Must not include |
|---|---|---|---|
| `elara_audio` | `include/`, `src/` | std, `elara_core` | Qt, JUCE |
| `elara_audio_juce` | `backend_juce/` | std, `elara_core`, `elara_audio`, JUCE | Qt; JUCE types in its public header |

`elara_audio` holds code that is easy to test without a sound card:

- the sequencer (`Sequence`: a song's measures in play order, its notes and
  note events in seconds, ADR-0017);
- the built-in synthesizer (`Synth`: one track in one `Timbre`, picked from the
  instrument by `timbreFor()`) and `OfflineRenderer`, which plays a sequence
  through one synth per track into stereo, with an event log (ADR-0018);
- the tone generator, fade envelope and the WAV writers (whole buffers, or
  block by block with `WavFileWriter`).

`elara_audio_juce` opens the
system audio device through JUCE (ADR-0002) and exposes a plain C++ API
(`elara::audio::SystemAudio`, `listOutputDevices()`).

## Real-time rule

Code that runs inside the audio callback (`ToneGenerator`, `fadeGain`,
`Synth` after construction, the callback in `system_audio.cpp`) must not
allocate, lock, log, throw or do I/O.
Exchange data with the audio thread through atomics or lock-free queues only.

## Verifying without listening

`elara-cli render-tone --json --out tone.wav` renders the same DSP offline and
reports frames and peak level; `elara-cli sequence FILE --json --events` prints
what the sequencer would play; `elara-cli render-audio FILE --json --events
[--out song.wav]` plays it through the built-in synthesizer and prints the
level, each track's timbre and every event with the frame it was dispatched
at. Tests in `tests/audio/` check pitch, envelopes, voice stealing and event
frames; tests in `tests/cli/` run the commands on corpus files.
