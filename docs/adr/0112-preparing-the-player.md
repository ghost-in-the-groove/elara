# 0112. Preparing the player on a worker

- Status: Accepted
- Date: 2026-11-26

## Context

Each time an edit's sequence was ready, `AudioController` called
`Player::load()` on the interface thread. `load()` converts every event of the
sequence to frames, splits the events by track and makes the mixing buffers.
For a track whose instrument changed, it also asks the sound bank for a new
sound: a SoundFont preset selects samples that FluidSynth may load then, and
an SFZ instrument reads its samples. On a long score, or after changing an
instrument or a sound bank, the window stopped answering for that time. The
study of the optimizations left calls this H03.

Running `load()` itself on a worker would not do. It updates the sounds the
player keeps and the song the audio thread will take next, which the
interface thread also uses, and it reuses sounds the audio thread may be
playing.

## Decision

- `Player::load()` comes in two halves:
  - `preparer()`, on the control thread, copies what preparing needs: the
    sounds the player keeps, which stay shared and are never changed by the
    worker, its bank, its sample rate and its block size.
  - The function it returns builds a `PreparedSong` on any thread, as `load()`
    would. `SoundBank` is already safe to use from several threads.
  - `publish()`, on the control thread, hands the prepared song to the audio
    thread as `load()` did. A song prepared for a player at another sample
    rate is refused.

  `load()` is now `publish(preparer()(song, sequence))`.
- `AudioController` prepares the songs of edits, of a new song and of a new
  sound bank on a worker. They go through a `JobLane` (ADR-0110), so only the
  newest is published, and playback goes on with what it has until then. The
  position and the loop are carried over when the prepared song is published.
- The first Play, and the player made when the device changes its sample rate,
  still load at once. Playback must have its song before it starts, and both
  happen rarely. Each drops what was being prepared for the player before.

## Consequences

- The interface thread no longer converts events, makes buffers or makes
  sounds after an edit, a new song or a new bank. It only copies a few shared
  pointers before the work and swaps one pointer after it.
- An edit is heard a little later during playback, after its preparation,
  instead of the window freezing for that time.
- The first Play still prepares on the interface thread. Making it
  asynchronous means starting the transport when the prepared song arrives,
  which can come later.
- The audio tests check that a song prepared on another thread plays exactly
  as a loaded one, and that a song prepared for another rate is refused.
