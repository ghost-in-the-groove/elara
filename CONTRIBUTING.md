# Contributing to Elara

How to build, test and change Elara. Keep this file short and true; details live
in `docs/`.

## What Elara is

A free, open-source (AGPL-3.0) desktop app combining a guitar tablature editor,
a score editor and, later, a DAW. C++23, Qt 6.12 (QML) for the UI, JUCE 9 for
audio. Scope and milestones: `docs/SCOPE.md`, `docs/roadmap.md`.

## Commands

Always go through the task runner (use `py` instead of `python` on Windows):

| Command | What it does |
|---|---|
| `python scripts/elara.py doctor` | Check the toolchain; run first if anything fails to configure |
| `python scripts/elara.py check` | **The gate.** Format check, architecture guard, build, all tests |
| `python scripts/elara.py build` | Configure (first time) and build the `dev` preset |
| `python scripts/elara.py test -R audio` | Build, then run tests whose name matches a regex |
| `python scripts/elara.py bench` | Build and run the benchmarks in the optimised `release` preset (results: `docs/performance.md`) |
| `python scripts/elara.py format` | Apply clang-format 23 |
| `python scripts/elara.py cli -- --json version` | Run the headless CLI |
| `python scripts/elara.py app` | Launch the desktop app |

The runner loads MSVC on Windows by itself; do not hand-roll `cmake`/`vcvars` calls.
Build output: `build/dev/` (executables in `build/dev/bin/`). Toolchain setup:
`docs/development.md`.

## Golden rules

1. **One musical model.** Tablature, notation, piano roll and timeline are views of
   `elara_core`. Never store musical data in UI or audio code.
2. **Module boundaries** (enforced by `scripts/verify_architecture.py` and CMake):

   | Module | May use | Never |
   |---|---|---|
   | `src/core` | std | Qt, JUCE, other modules |
   | `src/audio` (`elara_audio`) | std, core | Qt, JUCE |
   | `src/audio/backend_juce` | std, core, audio, JUCE | Qt; JUCE in its public header |
   | `src/io` (`elara_io`) | std, core, miniz, pugixml, yyjson | Qt, JUCE |
   | `src/notation` (future) | std, core | Qt, JUCE |
   | `src/cli` | all modules but app | Qt, JUCE |
   | `src/app` | Qt, all modules | JUCE |

   A new directory under `src/` needs a rule in the guard and a row in this table.
3. **Real-time audio.** Code on the audio thread never allocates, locks, logs, throws
   or does I/O. Talk to it with atomics or lock-free queues.
4. **Everything verifiable headlessly** (ADR-0003). CI has no speakers or screen,
   so every feature gets: an `elara-cli` subcommand with `--json` output, and tests
   (Catch2 unit tests and/or CTest cases driving the CLI). UI changes must keep
   `app.smoke` passing (QML loads with zero warnings).
5. **Tests first for behaviour.** Add or extend a test that fails, then make it pass.
   Test names are prefixed by module (`core.`, `io.`, `audio.`, `cli.`, `app.`).
6. **Licences.** Never copy code or assets from Guitar Pro, MuseScore or other
   projects unless the licence is compatible *and* recorded in
   `THIRD_PARTY_NOTICES.md`. File formats are implemented from public documentation.
   Test corpus files must be self-made or CC0 (`tests/corpus/README.md`).
7. **Record decisions.** A choice that constrains future work (dependency, format,
   threading model) gets an ADR in `docs/adr/`.

## Conventions

- Code, comments, docs and commit messages in English. UI strings are English
  source strings wrapped in `tr()`/`qsTr()` (Spanish translation planned).
- Musical vocabulary: use the names in `docs/glossary.md` (e.g. `Measure`, not `Bar`).
- C++23 without modules. Allowed: `std::expected`, `std::span`, `std::format`,
  designated initializers. Avoid `std::print` (not yet on the macOS toolchain).
- Naming: types `CamelCase`, functions/variables `camelBack`, private members
  `trailing_`, constants `kName`, namespaces and files `snake_case`.
  Everything lives in `namespace elara::<module>`.
- Every source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- Public headers: `src/<module>/include/elara/<module>/*.hpp`; sources in `src/<module>/src/`.
- Do not commit `build/`, downloaded SDKs or generated files.

## Pull requests

- One branch and one pull request per roadmap task, with a test that proves it.
- Commit messages: imperative subject line (`Add rational durations`), a body that
  says why when it is not obvious.

## Definition of done

`python scripts/elara.py check` passes, new behaviour has tests, docs touched by the
change are updated (module README, `docs/architecture.md`, roadmap checkbox).
