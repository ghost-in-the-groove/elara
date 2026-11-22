# elara_core

The single musical model that tablature, standard notation, piano roll and the
DAW timeline all read and edit (see `docs/architecture.md`).

- **May depend on:** the C++ standard library only.
- **Must not include:** Qt (`<Q...>`), JUCE (`<juce_...>`), or any other Elara module.
- **Status:** milestone 0 only provides `elara::core::version()`. The model
  (song, tracks, measures, voices, beats, notes, tunings, articulations,
  undo/redo) arrives in milestone 1 - see `docs/roadmap.md`.

Public headers live in `include/elara/core/`, sources in `src/`, tests in
`tests/core/`.
