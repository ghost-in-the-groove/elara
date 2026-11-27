# 0134. Where CI's time goes

- Status: Accepted
- Date: 2026-11-27

## Context

Every pull request waits for the `windows`, `macos` and `linux` checks, so the
slowest of the three sets how long a change takes to merge. Measured on the
pull requests of 2026-11-27, with ccache warm (92 to 95 % of the compiles
hit):

| Job | Total | Slowest parts |
| --- | --- | --- |
| macos | 5.6 to 7.3 min | tests 110 s, build 64 s, configure 56 s, restoring Qt 44 to 57 s, cloning vcpkg 29 to 42 s, restoring ccache 18 s |
| windows | 3.7 to 4.8 min | tests 122 s, build 50 s, configure 43 s, cloning vcpkg 20 to 23 s |
| linux | 2.0 to 2.3 min | tests 24 s, build 37 s, Linux packages 12 to 37 s |

Behind those numbers:

- **Tests.** Every job built and tested the debug preset. On Windows that means
  Qt's debug DLLs and the debug C runtime: each app smoke test took 17 to 25 s
  there and 3 s on Linux, and the tests used 470 s of CPU. On macOS, whose Qt
  is a release build anyway, Elara's own unoptimised code still took 423 s of
  CPU; `notation.Laying` alone took 19 s.
- **vcpkg.** Each job cloned vcpkg's whole history to check out the baseline.
  Its install then downloaded and unpacked the CMake it requires (15 s on
  macOS), and configured a project to hash the compiler (18.6 s on macOS,
  7.7 s on Windows) before it could look its packages up in the binary cache.
- **The format check** ran clang-format on 50 files at a time, one process
  after another: 9 s on macOS.
- **Node 20.** The actions at checkout v4, setup-python v5, cache v4 and
  upload-artifact v4 target Node 20, which GitHub deprecated; the runners were
  forcing them onto Node 24 with a warning on every run.

## Decision

- **Windows and macOS test the release preset**, Linux the debug one. The tests
  exercise the code that ships, and they run in a fraction of the time there.
  The debug build stays covered: Linux builds and tests it with its assertions,
  the `sanitize` job builds it with AddressSanitizer and UndefinedBehavior-
  Sanitizer (ADR-0105), and `dev` is the preset everyone builds locally. Each
  preset has its own ccache key (`ccache-<os>-<preset>-<sha>`), so a job never
  restores objects compiled for another build type.

  The release build showed one thing the debug one had hidden: the smoke test
  that leaves a recovery copy (ADR-0106) quit with `std::_Exit`, which on
  Windows is `ExitProcess`. That stops the other threads and then runs each
  DLL's detach code, Qt's global destructors included; with the release DLLs
  the process reported success and never ended, most likely in code waiting
  for a thread already stopped, until the test timed out. The app now quits
  there with `TerminateProcess` on Windows, which runs nothing, as a real
  crash.
- **The package workflow compiles through ccache too**, restoring the release
  cache that ci.yml's last run on main saved for Windows and macOS. It saves
  nothing, so packages never evict the caches pull requests restore.
- **vcpkg is fetched at the baseline only** (`git fetch --depth 1` of that
  commit), not cloned with its history.
- **The CI triplets leave the compiler out of the package ABIs**
  (`VCPKG_DISABLE_COMPILER_TRACKING`, through an overlay of the default
  triplet that only CI uses). The binary cache key names the runner image,
  which fixes the compiler, so the hash vcpkg spent a configure on added
  nothing. The key also hashes the overlay ports now, which it missed before.
- **vcpkg's tools are cached with its binary packages**, so the CMake it
  unpacks for itself is unpacked once per runner image; the sources it builds
  the packages from are left out, as only a cache miss needs them. On a cache
  miss the
  packages are built and the cache saved before the build starts: saved at the
  end of the job, as before, a failing test cost the next run the same
  rebuild.
- **The format check runs one clang-format per core.**
- **py7zr is installed only when Qt is not in the cache**: it extracts what a
  cache miss downloads, nothing else.
- **The macOS Qt kit keeps only the runner's architecture.** Qt ships it
  universal, each binary an x86_64 and an arm64 build, and restoring its cache
  was the longest step before the build on macOS (31 to 57 s).
  `install_qt.py --thin` keeps the runner's slice of each universal file, as
  `lipo -thin` does; the slice carries its own code signature, so it stays
  valid. That takes the kit from 1695 to 894 MB, and the compressed cache from
  about 378 to 206 MB. The Qt cache key names the runner's architecture.
- **The actions run on Node 24**: checkout v7, setup-python v7, cache v6 and
  upload-artifact v7. None of the inputs the workflows use changed.
- **One composite action, `.github/actions/toolchain`, sets up what the three
  workflows share**: Python and its tools, the Linux packages, Qt, vcpkg with
  its cache and ccache. The workflows used to repeat those steps, and keeping
  the vcpkg checkout and its cache key the same in each was by hand.

Not done:

- **Arm64 macOS runners** (`macos-15`) are faster than the Intel ones, but
  pull request #39 moved the job to Intel because arm64 runners were not
  acquired for over an hour. Worth trying again before the Intel image retires
  in 2027.
- **Skipping vcpkg's install** by caching `vcpkg_installed` would save the few
  seconds left, at the price of a second way to configure.

## Consequences

- A pull request's checks finish sooner; the slowest job sets the wait.
  Measured on this change's own pull request, before main had a release
  ccache to restore:

  | Part | Before | After |
  | --- | --- | --- |
  | Tests, macOS | 110 s | 34 s |
  | Tests, Windows | 122 s | 23 s |
  | vcpkg on macOS (fetch, cache, install) | 68 to 80 s | 19 s |
  | vcpkg install in the configure | 8 / 15 / 35 s | 0.4 / 1 / 2.2 s (Linux / Windows / macOS) |
  | Format check, macOS | 9 s | 3 s |
- The first run of each preset after this change compiles everything once,
  and so does the first run on each new runner image, which also rebuilds the
  vcpkg packages.
- A debug-only failure on Windows or macOS (the MSVC debug runtime's checked
  iterators, say) shows only on a developer's machine, not in CI.
