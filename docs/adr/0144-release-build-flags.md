# 0144. The release build optimised as MSVC's own release

- Status: Accepted
- Date: 2026-11-27

## Context

The `release` preset builds `RelWithDebInfo`: optimised code that keeps its
debug information. The packages are made from it (ADR-0048), the benchmarks
run on it (`docs/performance.md`), and since ADR-0134 the Windows and macOS CI
jobs test it.

With MSVC, CMake gives this configuration two flags that its `Release` does
not have:

- `/Ob1`: only functions marked `inline` (or defined in their class) are
  inlined. Templates, lambdas and small functions of other files are called,
  not expanded.
- `/debug /INCREMENTAL` at link time. An incremental link pads every function
  so that the next link can patch it in place, and `/DEBUG` turns off
  `/OPT:REF` and `/OPT:ICF`, so unreferenced functions and identical copies of
  the same code (common with templates) all stay in the program.

The packaged `elara.exe` was 16.7 MB and `elara-cli.exe` 9.1 MB. Link-time
optimisation, measured before (`ELARA_IPO`), made them a quarter smaller but
no faster, and slower at importing and writing PDF; MSVC's `/LTCG` turns off
incremental linking, which explains most of that quarter.

## Decision

With MSVC the `RelWithDebInfo` configuration inlines as `Release` does
(`/Ob2`) and links with `/INCREMENTAL:NO /OPT:REF /OPT:ICF`. Its debug
information still goes into the `.pdb` beside each program, so crash dumps
and profilers keep their symbols. The flags are set in the top-level
`CMakeLists.txt` before any target, so Elara's code and the dependencies it
builds itself (JUCE's modules) get them; vcpkg's packages are built by vcpkg.

GCC and Clang keep `-O2 -g` for this configuration: they inline across
functions at `-O2`, and their linkers keep no incremental padding.
Link-time optimisation stays off (`ELARA_IPO`).

## Consequences

- On Windows x64: `elara.exe` 16.7 MB to 8.4 MB, `elara-cli.exe` 9.1 MB to
  3.1 MB. Speed: see the benchmark comparison in `docs/performance.md`.
- A release link takes longer than an incremental one, and a change relinks in
  full. Developers build the `dev` preset, which is unchanged.
- The objects CI's ccache holds for the release preset are compiled again once.
