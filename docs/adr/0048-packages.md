# 0048. Packages

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 7 asks for installers on every platform. Until now Elara ran only
from a build tree, where the app finds Qt through the kit it was built with and
the CLI finds its fonts in `build/<preset>/bin/fonts`. Testers need something
they can download and run, and CI must prove that what ships actually starts:
a missing Qt plugin or QML module only shows on a machine without the kit.

The project stays alpha: no releases are published (`CONTRIBUTING.md`).

## Decision

**CMake install rules describe a runnable tree; CPack packs it.**

- The app installs with the Qt libraries, plugins and QML modules it uses,
  found by Qt's deployment script (`qt_generate_deploy_qml_app_script`):
  windeployqt on Windows, macdeployqt on macOS, Qt's own deployment on Linux
  (with the `$ORIGIN/../lib` run path `qt_standard_project_setup` sets).
- `elara-cli` installs next to the app with the fonts it renders SVG with and
  their licences; on macOS inside the app bundle (`elara.app/Contents/MacOS`),
  so the disk image holds one thing to drag.
- On Windows the libraries vcpkg builds as DLLs and the Visual C++ runtime
  install as DLLs next to the executables (the universal CRT is part of
  Windows 10 and later) rather than as the 25 MB runtime installer, and Qt's own
  translations only for the app's languages (English and Spanish, ADR-0030).
- `LICENSE`, `THIRD_PARTY_NOTICES.md` and `README.md` go at the top of the
  package (on Linux, in the doc directory).
- CPack makes a portable package per platform: a ZIP archive on Windows, a disk
  image on macOS, a tarball on Linux, each with a SHA-256 file.

**Every package is smoke-tested from its install tree.** `python
scripts/elara.py package` builds the app and the CLI in the release preset,
installs them into `build/package/stage`, runs the installed app's
`--smoke-test` (the window's QML must load without warnings) and a CLI edit and
SVG render with no Qt kit on `PATH`, and only then packs. On Windows the
offscreen platform plugin ships too, so the test runs without a screen; macOS
uses its own platform plugin.

**CI builds packages, it does not publish them.** The `package` workflow runs
on every push to `main`, on pull requests that change the install rules, and on
demand; each platform's package is a workflow artifact kept for 14 days. Its
jobs are not required checks, so they never hold a merge.

## Consequences

- Testers can download a build of `main` from the workflow's artifacts.
- The Windows package is about 44 MB (most of it Qt and its graphics shader
  compiler); every Qt Quick Controls style ships although the app uses
  Material, because Qt's deployment cannot tell which one the app picks.
- Still to come: installers (MSIX or MSI on Windows, an AppImage or Flatpak on
  Linux), signing and notarisation on macOS, and a bundled General MIDI
  SoundFont once playback uses one.
