# 0145. What the packages leave out

- Status: Accepted
- Date: 2026-11-27

## Context

The packages (ADR-0048) install the app with what Qt's deployment tools find
it needs. They find more than it uses. Measured on the Windows install folder
of 2026-11-27 (162 MB, 172 files), by reading every program's and library's
imports and following them from the programs and the plugins:

- **Shader compilers, 19.6 MB.** `dxcompiler.dll` and `dxil.dll` (15 MB)
  compile shaders for Direct3D 12 only; Qt Quick draws through Direct3D 11.
  `d3dcompiler_47.dll` (4.5 MB), which Direct3D 11 uses, is part of Windows 10
  and later, where Elara runs. No program imports any of the three; Qt loads
  them by name.
- **Qt Quick Controls styles, about 15 MB.** `QtQuick.Controls` may pick any
  style at run time, so the deployment brings all seven (Basic, Material,
  Fusion, Imagine, Universal, FluentWinUI3, Windows) with their libraries,
  plugins and QML, and the native style library the Windows and macOS styles
  draw with. The app always picks Material (`qtquickcontrols2.conf`), which
  builds on Basic.
- **Plugins for what the app never does, about 1.4 MB:** QML debugging (twelve
  `qmldbg_*` plugins), TLS, network information and TUIO touch input over the
  network.

On Linux the package workflow's artifact (the tarball and the AppImage) was
349 MB against 163 MB on Windows: the release preset builds with `-g`, and
the debug information stayed inside `elara` and `elara-cli`.

## Decision

- On Windows, `windeployqt` runs with `--no-system-d3d-compiler`,
  `--no-system-dxc-compiler` and `--skip-plugin-types
  qmltooling,tls,networkinformation,generic`.
- On every platform an install step after the deployment removes the unused
  Controls styles: libraries, plugins and QML of Fusion, Imagine, Universal,
  FluentWinUI3, Windows, macOS and iOS, and the native style. Material and
  Basic stay.
- On Linux the programs are installed stripped (`cmake --install --strip` for
  the AppImage, `CPACK_STRIP_FILES` for the tarball). Windows keeps the debug
  information in `.pdb` files that are not installed; macOS keeps it in the
  object files.

The packages' smoke tests run the app and the CLI from what is left, without
Qt on the path (ADR-0048), on all three platforms in the package workflow.

## Consequences

- See `docs/performance.md` for the sizes before and after.
- A new Qt Quick Controls style for the app means taking its name out of the
  removal list in `src/app/CMakeLists.txt`.
- Forcing Qt Quick onto Direct3D 12 (`QSG_RHI_BACKEND=d3d12`) in a package
  would fail to compile shaders at run time; nothing in Elara does.
- Debugging a crash from a Linux package needs a build of the same commit for
  its symbols.
