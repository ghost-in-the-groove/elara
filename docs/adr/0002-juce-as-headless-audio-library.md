# 0002. JUCE as a headless audio library

- Status: Accepted
- Date: 2026-11-22

## Context

Qt and JUCE both ship a GUI toolkit and an event loop. Using both for UI would
mean two widget systems, two message loops and painful embedding.

## Decision

- Qt owns the UI. JUCE is used only for audio: `juce_core`, `juce_events`,
  `juce_audio_basics`, `juce_audio_devices` (later `juce_audio_processors` for
  plugin hosting). We do not use `juce_gui_basics` for Elara's UI.
- All JUCE code lives in `src/audio/backend_juce` (`elara_audio_juce`). Its
  public header is plain C++ and hides JUCE behind a pimpl. The architecture
  guard rejects JUCE includes anywhere else.
- JUCE modules are compiled into that one static library (linked `PRIVATE`), so
  their sources are compiled once and never exposed to consumers.
- The Qt main thread is also JUCE's message thread
  (`juce::ScopedJuceInitialiser_GUI` is created there). On Windows and macOS,
  Qt's event loop pumps the native messages JUCE relies on.
- Future VST3 plugin editors open in their own native windows, not inside QML.

## Consequences

- The UI never blocks on audio and audio code is testable without Qt.
- **Open:** on Linux, Qt's loop does not service JUCE's message queue, so async
  JUCE notifications (device changes) are not delivered. Before milestone 4,
  either run a dedicated JUCE message thread on Linux or poll device state from
  a Qt timer. Synchronous calls (open device, start/stop) are unaffected.
