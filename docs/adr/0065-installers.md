# 0065. Installers

- Status: Accepted
- Date: 2026-11-24

## Context

ADR-0048 made portable packages: a ZIP archive on Windows, a disk image on
macOS, a tarball on Linux. Milestone 7 asks for installers too. Windows users
expect a Start menu entry and an uninstaller; Linux users expect one file that
runs on any distribution without unpacking. The macOS disk image already is the
platform's usual installer: the bundle is dragged to Applications.

Only CI can build and test what a developer's machine lacks the tools for, and
the project stays alpha: nothing is published (`CONTRIBUTING.md`).

## Decision

**Windows: an MSI package made by CPack with the WiX Toolset 3.** It installs
for every user under Program Files, adds a Start menu shortcut, shows the app
icon (ADR-0064) and the project's page in Apps and features, and shows the
licence before installing. Its upgrade code is fixed in
`cmake/ElaraPackaging.cmake`, so a newer package replaces an older one instead
of installing beside it. The ZIP archive stays for users who want no install.
CPack makes the MSI only where it finds WiX (the `WIX` variable that its
installer sets, as on the CI runners).

**Linux: an AppImage made by `elara.py package` with appimagetool.** The
install tree goes under `usr/` of an AppDir, with the desktop entry and the icon
at its top and `AppRun` pointing at `usr/bin/elara`; the app finds its Qt
libraries and plugins through the run path and `qt.conf` that Qt's deployment
already wrote (ADR-0048). The tool comes from `APPIMAGETOOL` or `PATH`. CI
downloads release 1.9.1 and checks it against the digest GitHub publishes for
it. The tarball stays.

**Every installer is smoke-tested from what it installs.** On Windows the MSI
package is unpacked with an administrative install (`msiexec /a`: the files,
nothing registered) and the installed app and CLI run the same checks as the
install tree. On Linux the AppImage itself runs the app's `--smoke-test`, unpacked
in place of mounting (`APPIMAGE_EXTRACT_AND_RUN`), since CI machines have no
FUSE. `elara.py package --installers`, as CI runs it, fails when an installer
cannot be made; without the flag a missing tool only skips its installer.

## Consequences

- The package workflow's artifacts hold `Elara-<version>-windows-x64.msi` and
  `Elara-<version>-linux-x86_64.AppImage`, each with a SHA-256 file.
- The MSI is not signed: Windows SmartScreen warns before it runs. The AppImage
  needs the system's X11 or Wayland libraries that Qt's xcb and wayland plugins
  load; very old distributions may lack them.
- Still to come: signing on Windows and macOS (notarisation needs a paid Apple
  account), file associations, and a Flatpak if distributions ask for one.
