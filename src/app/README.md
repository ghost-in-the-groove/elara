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
| `src/score_document.hpp` | `ScoreDocument` (QML): opens a file and lays out a track, or all of them, as notation, tablature or both on A4 pages, on worker threads. Edits the song through `core::Document` (new song, song info, tempo, signatures, length, tracks; undo, redo) and saves it as `.elara` (ADR-0031). Holds the keyboard entry caret and input duration, and types frets, note names, drum hits, durations, rests and deletes at it, and moves pitches (ADR-0032, ADR-0034, ADR-0035); the input duration can be a tuplet (ADR-0050). The caret can be in any of the four voices (`setCaretVoice`). Changes the time and key signatures from the caret's measure, and inserts or deletes measures there (ADR-0033). Reads and sets a measure's signs and jumps (`navigationAt`, `setNavigation`; ADR-0049), its repeats and alternate endings (`repeatsAt`, `setRepeats`; ADR-0016) and the tempo it starts with (`tempoAt`, `setTempoFrom`; ADR-0042). |
| `src/score_page.hpp` | `ScorePage` (QML): one page as a texture, painted on a worker thread at its size in device pixels and painted again when zooming settles; a relayout that leaves the page as it was keeps the texture. |
| `src/startup.hpp` | What the app does after its window loads: `--track`, `--view`, `--seek`, `--smoke-test`, `--screenshot` (`--tuner`, `--song-pane`, `--measure` and `--tuning` are properties of the window). |
| `qml/Main.qml` | The window: toolbar (new, open, save, undo, redo, view, track), welcome pane, pages, transport, drag and drop, errors, status bar (page, measures, zoom); asks to save unsaved changes before they would be lost. |
| `src/track_list.hpp` | `TrackList`, behind `ScoreDocument::trackList`: the song panel's tracks as a list model. A new list of tracks changes, inserts or removes only the rows that differ, so a mute or an added track keeps the other rows instead of making them all again. |
| `src/when_done.hpp` | `whenDone()`: hands the result of a worker job to the interface thread from its event loop, never inside the call that started the job, so a call that makes several edits shows only the last one's layout (ADR-0054). Opening, layout, sequencing and painting use it. |
| `qml/SongPane.qml` | The song panel at the right of the score (Ctrl+I): title, artist, tempo, time and key signatures from the first measure, length, and the tracks, which can be shown, removed or added for any instrument preset. Under each track its mix: mute, solo, a volume and a pan fader, heard while they move and kept as one undo step when let go; a track mute or solo keeps quiet fades (ADR-0039). A track with strings shows its tuning and capo under its row (ADR-0046). |
| `qml/ScoreView.qml` | Pages on a desk in a `ListView`, smooth zoom (Ctrl + wheel, pinch, Ctrl +/-/0), the playback cursor and scrolling after it. The caret over its page, keyboard entry (digits, arrows, Delete, Insert, `+`, `-`, `.`, Ctrl+2 to Ctrl+9 for tuplets) and clicks that place the caret and move playback to the note there; the view scrolls to keep the caret in sight. Letters A to G type note names (with Shift they join the beat's chord) and Alt+Up/Down moves the pitch a semitone (with Ctrl an octave). Alt+1 to Alt+4 pick the voice typing goes into. On a drum track Up and Down move over the lanes, one per kit piece, and Enter adds or removes the lane's hit (ADR-0035). Ctrl+M or a right click opens the caret's measure; Ctrl+Insert inserts a measure before it, Ctrl+Shift+Delete deletes it. |
| `qml/EntryBar.qml` | The entry bar, in a strip over the top of the pages while there is a caret: the note value typing uses, the dot, the tuplet (a click toggles a triplet, a right click picks another), the voice (a click goes on to the next, a right click picks one), insert a rest, delete, the measure popup, and where the caret is. |
| `qml/MeasurePopup.qml` | The caret's measure in a card next to it: its time and key signatures (from it until the next change), its tempo (in the beat unit in force there; a cross removes the measure's own change), its signs and jumps (chips for the signs, Fine and To Coda; the jump and how it ends), its repeats (chips for the start and end barlines, how many times the section plays, chips for the endings 1 to 8), insert a measure before or after it, delete it. |
| `qml/Theme.qml` | The theme, one object every QML file shares: the colours of the surfaces, text, accent and states in the light and in the dark theme, the corner radii and the type sizes. The window sets its `dark` from the Material theme. |
| `qml/Logo.qml` | The Elara logo, drawn as shapes, one word written with one pen: a calligraphic e crossed by the strings of a tablature (the frets 0 2 2 0 0 0 climbing them as an arpeggio), which leave it and are tied to the l; a; an eighth rest for the r; a. `detailed: false` shows three plain waves for small sizes, `named: false` only the e. `icons/elara-logo.svg` holds the same shapes, `icons/elara.svg` the app icon. |
| `qml/Elevation.qml` | The shadow under what floats over the score (transport, popups, errors). |
| `qml/IconButton.qml` | A round tool button drawn from an SVG path (`Icon.qml`), with a tooltip; checked, it shows in the accent. |
| `qml/SignatureFields.qml` | Time signature, key signature and mode fields, shared by the song panel and the measure popup. |
| `qml/TuningFields.qml` | A track's tuning: a preset or open strings typed from the lowest, the capo, and whether a change keeps the sound or the frets (ADR-0046). |

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
| `src/audio_controller.hpp` | `AudioController` (QML): the transport. Opens the audio device on the first play, hands the open score's sequence to an `elara::audio::Player` and reports `playingSong` and `position` about once a frame. An edit that leaves the sequence as it was (a mix, the title) only passes the new mix to the player, so playback carries on; a moving fader does the same without editing the song (ADR-0039). The document builds an edit's sequence on a worker beside its layout; until it arrives the player keeps the previous one, and it is always loaded with the song it was built from (ADR-0031). A new sequence plays on from the same place in the music (`audio::carriedPosition`: the same share of the same played measure after another speed or a tempo edit, else the same time); only another song (`songReplaced`) goes back to the start. |
| `src/score_document.hpp` | `ScoreDocument::duration` and `cursorAt(seconds)`: where the cursor stands on the pages, through `elara::notation::cursorAt` (it steps from one column of notes to the next); `secondsAt(page, x, y)`: where a click moves playback, through `elara::notation::measurePositionAt`. `speed` (25 to 200 %) and `metronome`: what every sequence is built with, kept from song to song; changing either builds the sequence again (ADR-0024). |
| `qml/PracticeControls.qml` | In the transport: the metronome button, and the speed as a pill that opens a slider with 50, 75 and 100 %. The song is sequenced again once the slider is let go. |

The transport floats over the bottom of the score: back to the start (Home),
play and pause (Space), the time, a seek bar, the metronome and the speed. A click on a note moves
playback to it. While the cursor moves, the view scrolls to keep it a third of
the way down.

## Command line

```
elara [FILE] [--track N|all] [--view notation|tab|both] [--seek SECONDS]
      [--tuner] [--song-pane] [--measure] [--language en|es] [--smoke-test]
      [--screenshot OUT.png]
```

- `FILE` opens a score at start; `--track` (from 1) and `--view` show that track
  and view once it is laid out; `--seek` moves the playback cursor there,
  paused (the audio device stays closed).
- `--smoke-test` loads the window, fails on any QML warning and quits. With a
  file it also waits until the first page is painted and shows the score in
  every view and with all its tracks, moves the cursor halfway through and
  clicks on it (playback must move to the note there), adds a track and undoes it (playback must stay there) and makes a new song (playback must go back to the start), then prints `smoke-test: ok (P pages, N painted, L layouts)`; a file that cannot be
  opened fails. CTest runs it with `QT_QPA_PLATFORM=offscreen` (`app.smoke`,
  `app.smoke_open_*`, `app.smoke_tuner`), so CI checks the UI without a screen.
- `--screenshot OUT.png` saves the window once it is shown and quits.
- `--tuner` opens the tuner at start (`app.smoke_tuner` checks it loads cleanly;
  without an audio input it says so instead of listening).
- `--song-pane` opens the song panel at start (`app.smoke_song_pane`).
- `--measure` opens the caret's measure popup once the score is laid out
  (`app.smoke_measure`).
- After the views, `--smoke-test` edits the song, or a new one when no file is
  given: it adds a track, changes the time signature, undoes both, redoes and
  undoes again, and fails unless the song ends as it began. Then, on a fretted
  track, it types: a click on the caret, two digits for fret 12 as one undo
  step, a duration, a rest inserted and deleted, all undone back to the start.
  Last it inserts a measure after the caret, gives it 3/4 and two sharps,
  checks the first measure kept its signatures, deletes it and undoes all.
  On a pitched track it types a C, checks the status names it, raises it a
  semitone and undoes both. It also sets 90 beats per minute from the second
  measure, removes that change again and undoes both. In voice 2 it types a
  note, checks it went there and the status names the voice, goes back to
  voice 1 and undoes. Then it adds a drum kit, checks the status names
  the top lane and the snare's and that the caret goes down with the lanes,
  adds and removes a snare hit, and undoes back to the start.
- `--language es` shows the UI in Spanish (`en` in English) instead of the
  system's language. With `--smoke-test` or `--screenshot` the app first prints
  `ui language: es`, which `app.smoke_spanish` checks; `app.language_unknown`
  checks that a language without a catalog is refused.

## Tests

`elara_app_tests` (`tests/app/`, `app.Score painter: ...`) paints display lists
on the offscreen platform and checks the pixels: lines, beams, glyphs, knocked-out
text, ties, a laid-out page inside its margins. Its hidden painting benchmarks
run with `python scripts/elara.py bench`.
