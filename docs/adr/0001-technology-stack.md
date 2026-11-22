# 0001. Technology stack

- Status: Accepted
- Date: 2026-11-22

## Context

Elara needs native performance, low-latency audio, a modern animated UI and
three desktop platforms, under a licence compatible with open source.

## Decision

- **C++23**, built with **CMake 3.28+ presets and Ninja**. No C++ modules
  (uneven support across MSVC, GCC and Clang); no `std::print` until the macOS
  toolchain supports it.
- **Qt 6.12 LTS** (Qt Quick / QML) for the UI, LGPL-3.0, linked dynamically.
  Installed from the official repository by `scripts/install_qt.py`.
- **JUCE 9** for audio devices and DSP, AGPL-3.0 (ADR-0002). Pinned archive with
  SHA-256 through FetchContent.
- **vcpkg** (manifest mode, fixed baseline) for small libraries: CLI11, Catch2,
  later FluidSynth and others.
- **Catch2 3 + CTest** for tests.
- **GitHub Actions** CI on Windows, macOS and Linux.
- Licence **AGPL-3.0-or-later**, compatible with JUCE's open-source licence.

## Consequences

- Two large frameworks (Qt and JUCE) must not leak into each other (ADR-0002).
- Developers need a C++23 compiler: MSVC 2022 17.x, GCC 13+ or Clang 17+.
- Upgrading Qt, JUCE or the vcpkg baseline is a deliberate change with a CI run.
