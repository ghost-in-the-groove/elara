# elara_audio / elara_audio_juce

Two targets with different rules:

| Target | Files | May depend on | Must not include |
|---|---|---|---|
| `elara_audio` | `include/`, `src/` | std, `elara_core` | Qt, JUCE |
| `elara_audio_juce` | `backend_juce/` | std, `elara_core`, `elara_audio`, JUCE | Qt; JUCE types in its public header |

`elara_audio` holds code that is easy to test without a sound card: the tone
generator, fade envelope and the WAV encoder. `elara_audio_juce` opens the
system audio device through JUCE (ADR-0002) and exposes a plain C++ API
(`elara::audio::SystemAudio`, `listOutputDevices()`).

## Real-time rule

Code that runs inside the audio callback (`ToneGenerator`, `fadeGain`, the
callback in `system_audio.cpp`) must not allocate, lock, log, throw or do I/O.
Exchange data with the audio thread through atomics or lock-free queues only.

## Verifying without listening

`elara-cli render-tone --json --out tone.wav` renders the same DSP offline and
reports frames and peak level; tests in `tests/audio/` and `tests/cli/` check it.
