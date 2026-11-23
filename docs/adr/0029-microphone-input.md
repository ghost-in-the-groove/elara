# 0029. Microphone input for the tuner

- Status: Accepted
- Date: 2026-11-23

## Context

ADR-0028 turns samples into a pitch. The tuner also has to hear the
instrument: open the system's audio input, move what it hears off the audio
thread, find a pitch about thirty times a second and show it steadily enough
to tune by, as a panel in the app and from the command line.

The input callback runs on a real-time thread (golden rule 3). Detection takes
up to a millisecond per window (ADR-0028), which would fit, but its result is
for the user interface, and the readings need smoothing that is easier to
change and test away from the callback. Opening the input through
`SystemAudio`, which owns playback, would tie the two together: changing the
input would interrupt playback, and the tuner would need the output device
too.

## Decision

**A separate input device.** `elara_audio_juce` gets `MicrophoneInput`, with
its own JUCE device manager, independent of `SystemAudio`. It opens the
default input device only when asked, so the app and its tests start without
touching audio hardware, and `listInputDevices()` lists inputs as
`listOutputDevices()` lists outputs.

**The loudest channel, through a lock-free ring.** The callback takes the
samples of the loudest input channel, so that a guitar plugged into one input
of an interface is not mixed with the noise of the others. Another channel
takes over only when it is twice as loud (6 dB). The samples go into a
`SampleRing`, a single-producer single-consumer queue of floats in
`elara_audio`: `write()` and `read()` never allocate, lock or block, and
samples that do not fit are dropped and counted. The callback also keeps the
peak level in an atomic for the level meter.

**Readings on the reading thread.** `elara_audio`'s `Tuner` takes the samples
from whichever thread reads them (the app's main thread every 30 ms, the
command line's loop every 20 ms). It looks for a pitch in the latest window
each time half a window has arrived. It shows the median of the last five
estimates of the same note. A single estimate of another note (an octave or
fifth slip on a plucked string) is ignored, and two in a row change the note.
It keeps the reading for half a second after the note stops. `observe()`
takes detections directly, so the smoothing is tested without audio.

**What is shown.** With a tuning, the app shows the nearest open string and
how far it is from its pitch, which is what a player turns the peg by.
Chromatic, it shows the nearest note. A note within 5 cents is in tune. The
panel opens from the toolbar or with Ctrl+Shift+T and listens only while
open. `elara --tuner` opens it at start. `elara-cli listen` prints each change
of the reading.

**macOS.** The app bundle's `Info.plist` gives the reason the system shows
when it asks the user for the microphone. macOS quits an app that opens the
microphone without one.

## Consequences

- CI has no audio input. The ring and the tuner are tested with generated
  signals and detections, `app.smoke_tuner` checks that the panel loads
  without QML warnings and reports a missing input, and the command-line
  tests cover the arguments `listen` refuses before opening a device. Hearing
  a real instrument is checked by hand.
- The microphone and playback can run at once, each on its own device.
- Choosing an input device other than the default is left for the settings
  dialog.
