# 0064. The app icon

- Status: Accepted
- Date: 2026-11-24

## Context

Elara has an icon, `src/app/icons/elara.svg`, but nothing showed it: the window
had Qt's default icon, `elara.exe` had none in Explorer or the taskbar, the macOS
bundle showed a blank application, and Linux desktops had no entry to launch it
from. Installers (milestone 7) need the icon for shortcuts too. Each platform
wants its own format: PNG images for the window, an `.ico` file compiled into
the Windows executable, an `.icns` file in the macOS bundle, and a desktop entry
with an icon on Linux.

## Decision

**One source, drawn at build time.** The SVG stays the only drawing. A small
tool, `elara-icon-files` (`src/app/tools/`), draws it with Qt Svg at 16 to 1024
pixels and writes `elara-<size>.png` up to 256, `elara.ico` (each size up to 256
as PNG, which Windows reads since Vista) and `elara.icns` (each size under every
name macOS gives it, as PNG). The drawing and the packing live in
`elara_app_icon` (`app_icon.hpp`), which the app tests check: the tile's rounded
corners stay clear, and both files hold what their formats say. Rendered images
are not committed, so the icon cannot drift from its SVG.

- **Window:** the PNG images are built into the executable (`:/icons`) and set
  as the application's window icon. The smoke test fails without it.
- **Windows:** `elara.rc` gives `elara.exe` the icon and its version details.
- **macOS:** `elara.icns` goes to the bundle's `Resources`, named by
  `CFBundleIconFile`.
- **Linux:** a desktop entry and the icon (SVG, and a 256-pixel PNG) install
  under `share/applications` and `share/icons/hicolor`, named after the app id
  `io.github.ghost_in_the_groove.Elara`, which the app also sets as its desktop
  file name so that Wayland compositors match its windows to the entry.

Only the build uses Qt Svg; the app itself loads PNG images.

## Consequences

- The build runs a Qt program it built; on Windows it finds the Qt libraries
  through `PATH`, set for that command alone.
- Installers can take `elara.ico` and the desktop entry as they are.
- Small sizes are scaled from the same drawing; if they read poorly, a simpler
  drawing for 16 and 24 pixels can join the SVG later.
