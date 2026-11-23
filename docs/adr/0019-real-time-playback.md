# 0019. Real-time playback

- Status: Accepted
- Date: 2026-11-23

## Context

The sequencer (ADR-0017) builds a song as it is played, off the audio thread,
and left open how a new sequence reaches the audio thread. The built-in
synthesizer (ADR-0018) renders it offline. Playing it live adds two threads
that must never wait for each other: the main thread, which loads songs and
moves the transport, and the audio thread, which has one buffer of time to
fill and may not allocate, lock or block (ADR-0002).

## Decision

**`audio::Player`** in `elara_audio` plays a sequence through one built-in
`Synth` per track, with the same dispatch and mix as the offline renderer: a
player started at the beginning gives exactly what `OfflineRenderer` gives,
sample for sample, whatever the device's buffer size.

**Handing a song over.** `load()` builds everything the audio thread needs
on the main thread: synths, each track's events in frames, mixing buffers.
It publishes the result through an atomic pointer. At the start of its next
callback the audio thread swaps it in at the same position. It parks the
song it replaces in a second atomic pointer, which the main thread frees in
`tidy()` (load() calls it too). The audio thread takes a new song only once
that slot is empty, so it never frees memory and never needs a queue. A song
loaded twice before the audio thread runs replaces the first, which the main
thread frees.

**Transport.** `play()`, `pause()` and `seek()` store atomics that the audio
thread reads once per callback; `position()` reads the frame the audio thread
last published (or the frame asked for by a pending seek). Pausing releases
the sounding notes, which ring out. Seeking silences everything and goes on
from the new point without striking notes that began earlier. At the end the
player stops; `play()` then starts again from the beginning.

**The device.** `audio::AudioSource` is the plain C++ interface a device
plays; `Player` implements it. `SystemAudio::setSource()` installs one. It
removes the JUCE callback while it does so, which waits for a callback in
progress, so the previous source can be destroyed as soon as it returns.

**Headless.** Unit tests drive `render()` directly, as an audio callback
would: equality with the offline renderer, pausing, seeking, the end, a
song loaded while playing, and control calls from another thread while
rendering. `elara-cli play FILE [--from S] [--seconds S]` plays on the
default device; CI has none, so it is tested only up to opening the file.

## Consequences

- The app can play songs with a transport and a cursor that follows
  `position()`, without any locking in the audio path.
- Loading a song while it plays cuts the sounding notes, because the new song
  comes with new synths. An editor that reloads after every edit may want to
  keep the synths and swap only the events; that can be added behind
  `load()`.
- The mixer (volume, pan, mute, solo) goes in the same place as the mix
  today, as more atomics read once per callback.
