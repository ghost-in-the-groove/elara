# 0105. Sanitizers

- Status: Accepted
- Date: 2026-11-25

## Context

The importers read Guitar Pro, MusicXML, MIDI and `.elara` files from anywhere:
byte offsets, counts and lengths come from the file. A read one byte past a
buffer, a shift by a count the file chose or a signed overflow in a duration
passes the ordinary tests whenever it happens to land on readable memory, and
the tests were never built with a sanitizer to catch it. The corpus snapshot
tests already import, lay out and render every corpus file, and the io unit
tests read truncated and damaged containers, so the inputs exist; only the
instrumentation is missing.

GCC and Clang build AddressSanitizer (reads and writes out of bounds, use after
free, leaks) and UndefinedBehaviorSanitizer (overflow, bad shifts, misaligned or
null access, out-of-range enum values) into a program. MSVC has only
AddressSanitizer, and the Windows Qt kit and vcpkg packages would need rebuilding
for it.

## Decision

- A CMake cache variable `ELARA_SANITIZE` lists sanitizers for GCC or Clang
  ("address,undefined"). `elara_target_warnings()` and
  `elara_source_warnings()`, which every Elara target and source already go
  through, add `-fsanitize=<list> -fno-sanitize-recover=all
  -fno-omit-frame-pointer` and `_GLIBCXX_ASSERTIONS` (bounds checks in
  libstdc++'s containers) to Elara's own code only, and the sanitizer runtime to
  what links it. JUCE's sources and the vcpkg packages are not instrumented. With
  MSVC the variable is an error.
- A `sanitize` preset (Debug, without the app, `address,undefined`, its tests
  failing at the first report and on leaks) runs with
  `python scripts/elara.py test --preset sanitize`.
- CI's `sanitize` workflow runs it on Linux with GCC 14 for pushes to main and
  for pull requests that change the libraries, the CLI, the tests or the build.
  It is not a required check: the three platform jobs stay the gate, and a
  finding fails this job to be fixed like any failing test.

## Consequences

- Every test but the app's runs instrumented: the corpus files through import,
  layout, render and export, the damaged containers, the audio engine and the
  CLI. The app is left out because Qt is not instrumented and its smoke tests
  need a display.
- Its first run found a signed overflow: `Fraction` ordered two values by
  multiplying across, and a damaged MusicXML file's `<divisions>` near the
  int limit gave terms whose products pass 64 bits, so the order came out
  wrong. Terms under 2^31 still multiply; larger ones are compared through
  their whole parts and remainders, which never overflow.
- A new importer gets the sanitizer for free once its corpus file and damaged
  cases are tests. Fuzzing the importers can build on the same preset.
- Code the tests never reach stays unchecked: the sanitizers only see what runs.
- The job takes longer than a platform job (an instrumented Debug build); its
  ccache is its own.
