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

- [ ] Rational durations (`Fraction`) and time positions; tuplet arithmetic
- [ ] Pitch and spelling (step, alter, octave), MIDI number conversion
- [ ] Tuning and fret/string <-> pitch mapping, capo
- [ ] Song > Track > Staff > Measure > Voice > Beat > Note hierarchy
- [ ] Time and key signatures, tempo map
- [ ] Note articulations and effects (bend curve, slide, hammer/pull, palm mute,
      dead note, harmonics, vibrato, ties) as data
- [ ] Measure validation (voice fills its measure), rests auto-fill
- [ ] Command pattern with undo/redo
- [ ] Native format v1 (zip container + JSON), round-trip tests
- [ ] `elara-cli new`, `elara-cli dump --json` (model snapshot used by later tests)

## Milestone 2 - Importers (`elara_io`)

- [ ] Self-made test corpus with `SOURCES.md`
- [ ] Guitar Pro 3/4/5 (binary), GPX (6), GP (7-8) importers
- [ ] MusicXML import/export (partwise), MIDI import/export
- [ ] `elara-cli import <file> --json` snapshot tests per corpus file

## Milestone 3 - Notation engine (`elara_notation`)  -> usable as a viewer

- [ ] SMuFL metadata loading (Bravura), glyph metrics
- [ ] Horizontal spacing, beaming, stems, accidentals for one staff
- [ ] Tablature staff layout with effects notation
- [ ] Systems and pages, incremental relayout
- [ ] `elara-cli render --svg` snapshot tests
- [ ] Qt scene graph renderer in the app; open-file dialog

## Milestone 4 - Playback  -> usable as a player

- [ ] Sequencer: model -> timed events, tempo map, repeats
- [ ] FluidSynth integration, MuseScore General SF3
- [ ] sfizz integration and guitar SFZ library selection
- [ ] Articulation rendering (bends, slides, legato, palm mute, harmonics)
- [ ] Mixer (volume, pan, mute, solo), playback cursor in the app
- [ ] `elara-cli render-audio` offline render + event-log tests

## Milestone 5 - Editor

- [ ] Selection model and keyboard-driven entry (fret numbers, durations)
- [ ] Copy/paste, insert/delete measures, transposition
- [ ] Edit every element of milestone 1 from the UI
- [ ] Spanish and English translations (Qt Linguist)

## Milestone 6 - Practice tools

- [ ] Metronome with count-in, A-B loop, speed 25-200 %
- [ ] Tuner (microphone input, pitch detection)

## Milestone 7 - Distribution

- [ ] Installers: MSIX/MSI (Windows), DMG (macOS), AppImage/Flatpak (Linux)
- [ ] Release automation and 1.0
