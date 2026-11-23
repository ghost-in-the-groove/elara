# elara (desktop app)

Qt Quick / QML user interface.

- **May depend on:** Qt, every Elara module. **Must not include:** JUCE (go through
  `elara::audio_juce`'s plain C++ API).
- QML lives in `qml/`, C++ glue (QObjects exposed to QML) in `src/`.
- UI strings are English source strings wrapped in `qsTr()` / `tr()`; Spanish and
  English translations are added in milestone 5 (see `docs/roadmap.md`).

## The score view (ADR-0015)

| File | What it does |
|---|---|
| `src/score_painter.hpp` | `ScorePainter`: paints a display list with QPainter (glyph outlines of the bundled fonts, shapes), the same on every platform and thread. `ScoreFonts` loads Bravura and Arimo. Built as the `elara_app_view` library so that tests link it. |
| `src/score_document.hpp` | `ScoreDocument` (QML): opens a file and lays out a track, or all of them, as notation, tablature or both on A4 pages, on worker threads. |
| `src/score_page.hpp` | `ScorePage` (QML): one page as a texture, painted on a worker thread at its size in device pixels and painted again when zooming settles. |
| `src/startup.hpp` | What the app does after its window loads: `--track`, `--view`, `--smoke-test`, `--screenshot`. |
| `qml/Main.qml` | The window: toolbar (open, view, track, zoom), welcome pane, pages, drag and drop, errors, status bar. |
| `qml/ScoreView.qml` | Pages on a desk in a `ListView`, smooth zoom (Ctrl + wheel, pinch, Ctrl +/-/0). |

The fonts (`Bravura.otf`, `Bravura.json`, `Arimo.ttf`, `Arimo-Italic.ttf`) are
embedded as resources under `:/fonts`; `qtquickcontrols2.conf` picks the
Material style.

## Command line

```
elara [FILE] [--track N|all] [--view notation|tab|both] [--smoke-test] [--screenshot OUT.png]
```

- `FILE` opens a score at start; `--track` (from 1) and `--view` show that track
  and view once it is laid out.
- `--smoke-test` loads the window, fails on any QML warning and quits. With a
  file it also waits until the first page is painted and shows the score in
  every view and with all its tracks, then prints
  `smoke-test: ok (P pages, N painted, L layouts)`; a file that cannot be opened
  fails. CTest runs it with `QT_QPA_PLATFORM=offscreen` (`app.smoke`,
  `app.smoke_open_*`), so CI checks the UI without a screen.
- `--screenshot OUT.png` saves the window once it is shown and quits.

## Tests

`elara_app_tests` (`tests/app/`, `app.Score painter: ...`) paints display lists
on the offscreen platform and checks the pixels: lines, beams, glyphs, knocked-out
text, ties, a laid-out page inside its margins. Its hidden painting benchmarks
run with `python scripts/elara.py bench`.
