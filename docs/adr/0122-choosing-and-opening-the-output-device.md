# 0122. Choosing the output device and opening it on a worker

- Status: Accepted
- Date: 2026-11-26

## Context

Elara played through the system's default output device, the first device of
the first driver type JUCE lists. Nothing in the app or the CLI chose another.
On a laptop with a monitor attached by HDMI or DisplayPort, Windows often makes
the monitor's audio the default; with no speakers in the monitor, Play played
in silence, and the only remedy was to change the default for every program.

The device opened on the first Play, on the interface thread
(`AudioController::togglePlayback()`). Opening it means scanning every driver
type and starting the device: about 0.2 s on a warm device, and seconds when a
Bluetooth headset is connecting or an HDMI endpoint wakes up. The window did
not answer for that time. ADR-0112 moved preparing the player to a worker but
left this first Play on the interface thread.

JUCE needs COM on Windows, and starts it only on its message thread, which in
Elara is the interface thread (`SystemAudio` sets up the JUCE runtime where it
is made). JUCE's device manager is not safe to use from two threads at once.

## Decision

- `SystemAudio::open(OutputDeviceChoice)` opens a device by driver type and
  name, as `listOutputDevices()` gives them, or the system's default when the
  name is empty. A name alone is looked for among every type, in order. Opening
  another device closes the one open and keeps the source and the callback, so
  playback goes on on the new device. A device that is not there fails with its
  name, without falling back to the default: playing through other speakers
  than the ones chosen is worse than saying so.
  `SystemAudio::outputDevices()` lists the outputs through the object's own
  device manager.
- `open()` and `outputDevices()` may run on another thread, one at a time,
  while the interface thread leaves the object alone. `SystemAudio` is still
  made and destroyed on the interface thread, so the JUCE runtime stays there.
  On Windows, `open()` keeps the process's multithreaded COM apartment alive
  (`CoIncrementMTAUsage`, once, never released), so a worker can use COM
  without joining an apartment of its own.
- `AudioController` runs one device job at a time on a worker: opening the
  chosen device, or listing the outputs. While one runs, only the worker uses
  the device; the transport does not follow the device's rate until it ends.
  Play and the test tone asked for meanwhile start once the device is open; a
  second Play while it opens takes the first back. A device chosen while
  another opens is opened next, and what waited starts on it. Destroying the
  controller waits for the job running.
- The app chooses the device in Sound > Output, which lists the outputs each
  time it opens, and keeps the choice in the settings when `rememberSound` is
  on. A device chosen while the device is open takes over at once, playing or
  paused as before; the player follows its sample rate (`Player::takeOver`).
  The CLI takes `--device "TYPE / NAME"` or `--device NAME` on `play` and
  `tone`, as `devices` prints them.

## Consequences

- The window answers while the device opens, however long that takes. The
  first Play starts a moment after the click instead of during it.
- A device chosen and later unplugged fails on the next Play with its name,
  and Sound > Output offers the others and the system default.
- JUCE's own device notifications still run on the interface thread. One that
  arrives while a worker opens a device would touch the device manager from
  two threads; it needs a device to change in that fraction of a second.
- The first Play still prepares the song on the interface thread once the
  device is open (ADR-0112).
- CI has no audio device, so the tests check that a device that is not there
  fails with its name (`cli.play_rejects_unknown_device`,
  `cli.tone_rejects_unknown_device`). Opening on a worker, choosing a device and
  changing it while playing were checked by hand on Windows.
