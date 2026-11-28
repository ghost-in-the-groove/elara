# 0148. The first Play is prepared on a worker

- Status: Accepted
- Date: 2026-11-28

## Context

ADR-0112 moved the preparation of the player to a worker after edits, a new
song and a new sound bank, but left two cases on the interface thread: the
first Play, which makes the player, and the player made when the device
restarts at another sample rate. Both were thought rare and quick.

They are rare, but not quick. Preparing a song makes a sound for each track,
and a SoundFont's sound reads and decodes the samples of its presets as it is
made: FluidSynth loads samples when a preset is selected, and an SF3's are
compressed. Measured in the release build on a Ryzen 9 5900X, a player for a
band of six tracks (guitar, bass, drums, piano, violin, voice) through
MuseScore General took about 1.07 s to prepare; twenty guitars, which share
one preset, 44 ms; the built-in synthesizer under 4 ms. So the first press
of Play froze the window for about a second with the SoundFont that comes
with the packages, and a device switching rates did the same.

## Decision

- The first Play makes the player, gives it to the device and prepares the
  song on the worker that prepares edited songs (ADR-0112). Until the song is
  published the player is silent and the transport does not play; playback
  starts when it arrives.
- Whenever the player does not have the document's song (the first Play,
  another song opened), Play waits for it the same way, and a second press
  meanwhile takes the first back, as while the device opens (ADR-0122). The
  song that was open before never plays in its place.
- Meanwhile the transport keeps its position itself, as before the device
  opens: a seek is remembered and an edit carries it to the same place in the
  music. An edit does not restart the preparation, which would make every
  sound again; once the song is published, the edited one is prepared with
  those sounds kept.
- A device that restarts at another rate gets its new player on a worker of
  its own lane. The old player plays on at the old rate until the new one is
  ready, then the new one takes over from the same place, as before. If the
  device, the song or the sound bank changed meanwhile, a player is prepared
  again for what is current; a song still being prepared for the old player
  is prepared again for the new one.

## Consequences

- Nothing on the interface thread makes sounds any longer: it only copies
  shared pointers before the work and swaps one after it.
- The first Play starts a second later with a large SoundFont, but the
  window answers meanwhile. The Play button gives no sign of the wait yet,
  as it gives none while the device opens.
- After a device changes its rate the music plays at the wrong pitch for as
  long as the new player takes to prepare, as it already did while the
  interface thread prepared it.
- The transport's tests use a sound bank whose sounds wait to be made and
  that says on which thread they were: the first Play, another song and a
  new rate make them on a worker, and a second Play takes back the first.
