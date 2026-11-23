# Roadmap

Milestones from `docs/SCOPE.md`. Each task should fit in one working session and
one pull request, with a test that proves it. Tick the box in the same PR.

## Milestone 0 - Foundation

- [x] Repository layout, contributor guide, ADRs, glossary
- [x] CMake presets, vcpkg manifest, pinned JUCE, Qt 6.12 install script
- [x] Task runner `scripts/elara.py` (doctor, check, build, test, format, guard, cli, app)
- [x] Architecture guard and clang-format check
- [x] `elara-cli`: `version`, `render-tone`, `devices`, `tone`
- [x] Qt Quick window with a test-tone button through JUCE; `--smoke-test`
- [x] CI workflow for Windows, macOS and Linux
- [x] First green build on Windows (MSVC 19.44, Qt 6.12.1, JUCE 9.0.6)
- [x] First green CI run on GitHub (Windows, macOS, Linux)

## Milestone 1 - Musical model (`elara_core`)

Done when a song can be built, edited, undone and saved/loaded from the CLI.

- [x] Rational durations (`Fraction`) and time positions; tuplet arithmetic
- [x] Pitch and spelling (step, alter, octave), MIDI number conversion
- [x] Tuning and fret/string <-> pitch mapping, capo
- [x] Song > Track > Staff > Measure > Voice > Beat > Note hierarchy
- [x] Time and key signatures, tempo map
- [x] Note articulations and effects (bend curve, slide, hammer/pull, palm mute,
      dead note, harmonics, vibrato, ties) as data
- [x] Measure validation (voice fills its measure), rests auto-fill
- [x] Command pattern with undo/redo
- [x] Native format v1 (zip container + JSON), round-trip tests
- [x] `elara-cli new`, `elara-cli dump --json` (model snapshot used by later tests)

## Milestone 2 - Importers (`elara_io`)

- [x] Self-made test corpus with `SOURCES.md`
- [x] Guitar Pro 7-8 (`.gp`) import with `elara-cli import`
- [x] Guitar Pro 6 (`.gpx`) import
- [x] Guitar Pro 3/4/5 (binary) import
- [x] MusicXML import (partwise, `.mxl`) with `elara-cli import`
- [x] MusicXML export (text and `.mxl`) with `elara-cli export`, tested by round trip
- [x] MIDI import/export with `elara-cli import`/`export`, tested by round trip
  - [x] Played velocities as a dynamic per measure, accents and ghost notes (ADR-0045)
- [x] `elara-cli import <file> --json` snapshot tests per corpus file

## Milestone 3 - Notation engine (`elara_notation`)  -> usable as a viewer

- [x] SMuFL metadata loading (Bravura), glyph metrics
- [x] Horizontal spacing, beaming, stems, accidentals for one staff
- [x] Tablature staff layout with effects notation
- [x] Systems and pages: line breaking, justification, multi-track scores
- [x] Repeat barlines, repeat counts and alternate endings (ADR-0016)
- [x] Tuplet numbers, with a bracket when no beam shows the group (ADR-0040)
- [ ] Incremental relayout (only the systems an edit touches)
- [x] `elara-cli render --svg` snapshot tests: every corpus file on pages
- [x] Score view in the app: pages painted on worker threads and shown as scene
      graph textures, open-file dialog and drag and drop, track and view selection,
      smooth zoom (ADR-0015)

## Milestone 4 - Playback  -> usable as a player

- [x] Sequencer: model -> timed events, tempo map, repeats (ADR-0017)
- [ ] FluidSynth integration, MuseScore General SF3
- [ ] sfizz integration and guitar SFZ library selection
- [x] Articulation rendering (bends, slides, legato, palm mute, harmonics; ADR-0022, ADR-0023)
- [x] Mixer (volume, pan, mute, solo): playback (ADR-0020) and the app, in the song panel (ADR-0039)
- [x] `elara-cli render-audio` offline render + event-log tests, with a built-in synthesizer (ADR-0018)
- [x] Real-time playback through the built-in synthesizer, `elara-cli play` (ADR-0019)
- [x] Transport, playback cursor and click-to-seek in the app, `elara-cli render --cursor-at`
  and `--time-at`

## Milestone 5 - Editor

- [ ] Selection model and keyboard-driven entry (fret numbers, durations)
  - [x] Note names at the caret on any pitched staff, chords, semitone moves
        (ADR-0034)
  - [x] Drum entry: lanes for the kit pieces, hits toggled at the caret (ADR-0035)
  - [x] Note effects toggled at the caret: ties, legato, slides, bends, vibrato,
        palm mutes, dead and ghost notes, let ring, harmonics, accents,
        staccato (ADR-0037)
  - [x] Dynamics at the caret, holding until the next change; new notes take
        the dynamic in force (ADR-0047)
- [x] Copy/paste of measures, insert/delete measures, transposition (ADR-0026, ADR-0027)
- [ ] Edit every element of milestone 1 from the UI
  - [x] Song panel: new song, title, artist, tempo, time and key signatures,
        length, tracks of any instrument, undo and redo, saving (ADR-0031)
- [x] Spanish and English translations (Qt Linguist, ADR-0030)

## Milestone 6 - Practice tools

- [ ] Metronome with count-in, A-B loop, speed 25-200 %
- [x] Tuner: pitch detection, microphone input, the app panel, `elara-cli tune` and `listen` (ADR-0028, ADR-0029)

## Milestone 7 - Distribution

- [ ] Installers: MSIX/MSI (Windows), DMG (macOS), AppImage/Flatpak (Linux)
- [ ] Release automation and 1.0
