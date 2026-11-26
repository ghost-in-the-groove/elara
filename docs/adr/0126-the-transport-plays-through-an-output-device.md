# 0126. The transport plays through an output device it is given

- Status: Accepted
- Date: 2026-11-26

## Context

`AudioController`, the app's transport, made its `SystemAudio` itself, the
first time it needed the device. Everything it decides around the device was
therefore only reachable with a real sound card: Play and the test tone
waiting for a device that opens on a worker (ADR-0122), a second Play taking
the first back, a device that fails clearing what waited for it, a device
chosen while another opens, playback moving to another device at its rate,
and a device restarting at another rate (`Player::takeOver`). CI has no audio
device, so ADR-0122 left all of it checked by hand.

The rest of the transport, how an edit, another speed or another song moves
the position and the loop, had no test either: the app tests built neither
`AudioController` nor `ScoreDocument`.

## Decision

- `elara_audio` declares `OutputDevice`, the interface of a device the
  transport plays through: `open(OutputDeviceChoice)`, `outputDevices()`,
  `deviceDescription()`, `sampleRate()`, `setSource()`, `playTone()`,
  `stop()`, `isPlaying()`, with the threads of ADR-0122 as part of it.
  `OutputDeviceInfo`, `OutputDeviceChoice` and `parseOutputDeviceChoice()`
  move there from `system_audio.hpp`. It is plain C++: no JUCE, no Qt.
- `SystemAudio` (`elara_audio_juce`) is the `OutputDevice` that plays through
  the sound card. The CLI keeps using it directly.
- `AudioController` takes a factory of devices, called on the interface
  thread the first time a device is needed. Its default constructor, the one
  QML uses, makes `SystemAudio`.
- The app tests build `AudioController` and `ScoreDocument` and play through a
  device of their own, which opens when told to, fails when told to, changes
  its rate and renders the player as the audio thread would.

## Consequences

- What ADR-0122 checked by hand is tested on every platform, without a sound
  card: `tests/app/test_audio_controller.cpp`.
- Two adapters sit at the seam, `SystemAudio` and the tests' device. A third
  (a device that writes a file, say) would plug in the same way.
- A change to `OutputDevice` changes both adapters; it is a small interface,
  and the tests' device is a page long.
- `elara_app_tests` compiles `score_document.cpp` and `audio_controller.cpp`
  and links the audio backends, so it takes longer to build.
