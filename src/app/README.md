# elara (desktop app)

Qt Quick / QML user interface.

- **May depend on:** Qt, every Elara module. **Must not include:** JUCE (go through
  `elara::audio_juce`'s plain C++ API).
- QML lives in `qml/`, C++ glue (QObjects exposed to QML) in `src/`.
- UI strings are English source strings wrapped in `qsTr()` / `tr()`, translated
  in `i18n/` (see below).

## The score view (ADR-0015)

| File | What it does |
|---|---|
| `src/score_painter.hpp` | `ScorePainter`: paints a display list with QPainter (glyph outlines of the bundled fonts, shapes), the same on every platform and thread. `ScoreFonts` loads Bravura and Arimo. Built as the `elara_app_view` library so that tests link it. |
| `src/score_document.hpp` | `ScoreDocument` (QML): opens a file and lays out a track, or all of them, as notation, tablature or both on A4 pages, on worker threads. Edits the song through `core::Document` (new song, song info, tempo, signatures, length, tracks; undo, redo) and saves it as `.elara` (ADR-0031). Holds the keyboard entry caret and input duration, and types frets, durations, rests and deletes at it (ADR-0032). |
| `src/score_page.hpp` | `ScorePage` (QML): one page as a texture, painted on a worker thread at its size in device pixels and painted again when zooming settles. |
| `src/startup.hpp` | What the app does after its window loads: `--track`, `--view`, `--seek`, `--smoke-test`, `--screenshot` (`--tuner` is a property of the window). |
| `qml/Main.qml` | The window: toolbar (new, open, save, undo, redo, view, track, zoom), welcome pane, pages, transport, drag and drop, errors, status bar; asks to save unsaved changes before they would be lost. |
| `qml/SongPane.qml` | The song panel at the right of the score (Ctrl+I): title, artist, tempo, time and key signatures from the first measure, length, and the tracks, which can be shown, removed or added for any instrument preset. Under each track its mix: mute, solo, a volume and a pan fader, heard while they move and kept as one undo step when let go; a track mute or solo keeps quiet fades (ADR-0039). |
| `qml/ScoreView.qml` | Pages on a desk in a `ListView`, smooth zoom (Ctrl + wheel, pinch, Ctrl +/-/0), the playback cursor and scrolling after it. The caret over its page, keyboard entry (digits, arrows, Delete, Insert, `+`, `-`, `.`) and clicks that place the caret and move playback to the note there; the view scrolls to keep the caret in sight. |
| `qml/EntryBar.qml` | The entry bar floating over the score: the note value typing uses, the dot, insert a rest, delete, and where the caret is. |

## The tuner (ADR-0029)

| File | What it does |
|---|---|
| `src/tuner_controller.hpp` | `TunerController` (QML): while `active`, listens to the default input through `elara::audio::MicrophoneInput` and shows what `elara::audio::Tuner` reads: note, cents, nearest string, input level. Tunings are the core presets, or chromatic. |
| `qml/TunerPane.qml` | The tuner card (toolbar button, Ctrl+Shift+T): the note inside an arc from -50 to +50 cents with a sliding marker, which way to turn, the instrument's strings, the tuning and the reference A4. It listens only while open. |
| `Info.plist.in` | The macOS bundle's Info.plist, with the reason the system shows when it asks for the microphone. |

## Translations (ADR-0030)

`i18n/elara_es.ts` holds the Spanish translation of every UI string;
`i18n/elara_en.ts` holds only the English plural forms. They are compiled into
the executable under `:/i18n`. The app shows the system's language when it has
it, English otherwise, or the language given with `--language`.

After adding or changing a string:

1. `python scripts/elara.py translations` refreshes the `.ts` files (lupdate).
2. Translate the new entries in Qt Linguist (`linguist i18n/elara_es.ts`) or by
   hand, using the terms in `docs/glossary.md`.

`app.translations_complete` fails while a string is missing from the files or
has no translation.

The fonts (`Bravura.otf`, `Bravura.json`, `Arimo.ttf`, `Arimo-Italic.ttf`) are
embedded as resources under `:/fonts`; `qtquickcontrols2.conf` picks the
Material style.

## Playback (ADR-0019)

| File | What it does |
|---|---|
| `src/audio_controller.hpp` | `AudioController` (QML): the transport. Opens the audio device on the first play, hands the open score's sequence to an `elara::audio::Player` and reports `playingSong` and `position` about once a frame. An edit that leaves the sequence as it was (a mix, the title) only passes the new mix to the player, so playback carries on; a moving fader does the same without editing the song (ADR-0039). |
| `src/score_document.hpp` | `ScoreDocument::duration` and `cursorAt(seconds)`: where the cursor stands on the pages, through `elara::notation::cursorAt` (it steps from one column of notes to the next); `secondsAt(page, x, y)`: where a click moves playback, through `elara::notation::measurePositionAt`. |

The transport floats over the bottom of the score: back to the start (Home),
play and pause (Space), the time and a seek bar. A click on a note moves
playback to it. While the cursor moves, the view scrolls to keep it a third of
the way down.

## Command line

```
elara [FILE] [--track N|all] [--view notation|tab|both] [--seek SECONDS]
      [--tuner] [--song-pane] [--language en|es] [--smoke-test] [--screenshot OUT.png]
```

- `FILE` opens a score at start; `--track` (from 1) and `--view` show that track
  and view once it is laid out; `--seek` moves the playback cursor there,
  paused (the audio device stays closed).
- `--smoke-test` loads the window, fails on any QML warning and quits. With a
  file it also waits until the first page is painted and shows the score in
  every view and with all its tracks, moves the cursor halfway through and
  clicks on it (playback must move to the note there), then prints `smoke-test: ok (P pages, N painted, L layouts)`; a file that cannot be
  opened fails. CTest runs it with `QT_QPA_PLATFORM=offscreen` (`app.smoke`,
  `app.smoke_open_*`, `app.smoke_tuner`), so CI checks the UI without a screen.
- `--screenshot OUT.png` saves the window once it is shown and quits.
- `--tuner` opens the tuner at start (`app.smoke_tuner` checks it loads cleanly;
  without an audio input it says so instead of listening).
- `--song-pane` opens the song panel at start (`app.smoke_song_pane`).
- After the views, `--smoke-test` edits the song, or a new one when no file is
  given: it adds a track, changes the time signature, undoes both, redoes and
  undoes again, and fails unless the song ends as it began. Then, on a fretted
  track, it types: a click on the caret, two digits for fret 12 as one undo
  step, a duration, a rest inserted and deleted, all undone back to the start.
- `--language es` shows the UI in Spanish (`en` in English) instead of the
  system's language. With `--smoke-test` or `--screenshot` the app first prints
  `ui language: es`, which `app.smoke_spanish` checks; `app.language_unknown`
  checks that a language without a catalog is refused.

## Tests

`elara_app_tests` (`tests/app/`, `app.Score painter: ...`) paints display lists
on the offscreen platform and checks the pixels: lines, beams, glyphs, knocked-out
text, ties, a laid-out page inside its margins. Its hidden painting benchmarks
run with `python scripts/elara.py bench`.
