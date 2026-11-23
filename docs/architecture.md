# Architecture

## Modules

```mermaid
flowchart TD
    app["app (Qt Quick / QML)"] --> core
    app --> audio_juce
    app -.-> io
    app -.-> notation
    cli["cli (elara-cli, headless)"] --> core
    cli --> audio_juce
    cli -.-> io
    cli -.-> notation
    audio_juce["audio/backend_juce (JUCE)"] --> audio
    audio["audio (sequencer, synth, WAV)"] --> core
    io["io (importers/exporters)"] -.-> core
    notation["notation (layout engine)"] -.-> core
    core["core (musical model)"]
```

Dashed modules do not exist yet. Boundaries are enforced twice: CMake target
links (a module can only use what it links) and `scripts/verify_architecture.py`
(include rules, run by `scripts/elara.py check`).

| Module | Target | Responsibility | Milestone |
|---|---|---|---|
| `src/core` | `elara_core` | Musical model, commands with undo/redo | 1 |
| `src/io` | `elara_io` | Native `.elara` format (ADR-0006), MusicXML import and export (ADR-0007, ADR-0008), MIDI import and export (ADR-0009), Guitar Pro 3-8 import (ADR-0010) | 1, 2 |
| `src/notation` | `elara_notation` | SMuFL font metrics (ADR-0011); layout of standard notation and tablature into a display list, staves aligned in systems, line breaking and pages, SVG output (ADR-0012, ADR-0013, ADR-0014) | 3 |
| `src/audio` | `elara_audio` | Sequencer: songs as played, in seconds (ADR-0017); the built-in synthesizer, offline rendering (ADR-0018) and real-time playback (ADR-0019); WAV reading and writing; the tuner's pitch detection (ADR-0028) | 0, 4, 6 |
| `src/audio/backend_juce` | `elara_audio_juce` | System audio devices via JUCE; synths (FluidSynth, sfizz) | 0, 4 |
| `src/cli` | `elara-cli` | Headless access to every feature, JSON output | 0+ |
| `src/app` | `elara` | Qt Quick UI; paints the notation display list into page textures for the Qt scene graph, opens and lays out scores on worker threads (ADR-0015) | 0+ |

## Key decisions

- **One model, many views.** Tablature, staff, piano roll and timeline read the
  same `elara_core` objects (`Song > Track > Staff > Measure > Voice > Beat >
  Note`, ADR-0004). A live song is a `Document`: every edit is a reversible
  command, so undo/redo is uniform (ADR-0005).
- **Notation is a pure function.** `elara_notation` turns the model into a
  display list (glyphs, lines, positions) without Qt. The app paints that list
  into page textures for the Qt scene graph (ADR-0015); tests snapshot it as
  SVG/JSON and check the app's pixels without a screen. Systems are laid out
  independently of each other (in parallel), so re-layout can be incremental
  per system.
- **Qt draws, JUCE plays** (ADR-0002). JUCE is used as a headless library inside
  `elara_audio_juce`; nothing else sees JUCE types.
- **Headless first** (ADR-0003). Every feature is reachable from `elara-cli` and
  verifiable by tests without a screen or speakers.

## Threads

| Thread | Owner | Rules |
|---|---|---|
| Main / GUI | Qt event loop (also JUCE's message thread) | Owns the model; all edits happen here |
| Audio callback | JUCE device | No allocation, locks, I/O or logging; reads immutable snapshots / lock-free queues |
| Workers (future) | Thread pool | Layout of large scores, file import, offline render |

Open question (milestone 4): on Linux, Qt's event loop does not service JUCE's
message queue. Device-change notifications may need a dedicated JUCE message
thread there. Tracked in ADR-0002.
