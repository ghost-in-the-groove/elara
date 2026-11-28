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
| `src/score_painter.hpp` | `ScorePainter`: paints a display list with QPainter (glyph outlines of the bundled fonts, shapes), the same on every platform and thread, in the paper and the ink it is given (`ScoreColours`: black on white unless told, light on dark for the dark theme's page, ADR-0114, with the staves' lines in a dimmer colour of their own there, ADR-0174). `ScoreFonts` loads Bravura and Arimo. Built as the `elara_app_view` library so that tests link it. |
| `src/score_document.hpp` | `ScoreDocument` (QML): opens a file and lays out a track, or all of them, as notation, tablature or both on A4 pages, or in strips as high as a page for the continuous view (`continuous`, `showsStrips`, `pageTops`; ADR-0057), on worker threads, one job at a time with only the newest request waiting (`layoutWork`, `sequenceWork`; ADR-0110); after an edit only what it changed is laid out again (`notation::PageLayoutCache`, ADR-0052), and each of the six views shown last keeps its own memory, so that a track shown before comes back with its pages, nothing laid out again (`layoutsKept`, ADR-0175). Edits the song through `core::Document` (new song, song info, tempo, signatures, length, tracks; undo, redo) and saves it as `.elara` (ADR-0031); the staves shown are laid out at the staff space that fits them, smaller for every track at once (`staffSpace`, `notation::fittedStaffSpace`, ADR-0171); `exportPdf()` writes the shown tracks and view on A4 pages as a PDF document (ADR-0074); both run on workers, saves one after the other, and a save marks saved only the state it wrote (`saving`, `saveFinished`, `exportFinished`; ADR-0107); `exportMusicXml()` and `exportMidi()` write the whole song as MusicXML (`.mxl` compressed) or as a MIDI file, then `report()` what the format left out; `importWarnings` keeps what opening the song's file approximated or dropped, which the status bar tells for as long as the song is open, cleared by the next file or a new song; `pageIssues()` holds the layout issues of each page shown, found on the layout worker only for the pages a layout changed (`layoutIssueCount`, `layoutIssueList()`, `showLayoutIssue()` takes the caret to one; ADR-0150); `measureStarts` is when each measure played starts, changing only when one does, and `measureStart()` when a written measure first plays (the song string's ticks and its caret); `exportFile()` suggests the song's file with the export's extension. Keeps the keyboard entry caret and input duration, the selection and the measures copied in a `core::EditSession` (ADR-0127): moves the caret, types frets and durations at it (ADR-0032), the input duration being a tuplet when asked (ADR-0050), and takes it to any of the four voices (`setCaretVoice`). What is at the caret is `CaretEditor`'s to read and edit, through `editedSong()`, `session()`, `enter()`, `endTyping()` and `refuse()` (ADR-0128). Selects measures from an anchor to the caret, and copies, pastes and transposes them (ADR-0036). Changes the time and key signatures from the caret's measure, and inserts or deletes measures there (ADR-0033). Reads and sets a measure's signs and jumps (`navigationAt`, `setNavigation`; ADR-0049), its repeats, alternate endings and double barline (`repeatsAt`, `setRepeats`; ADR-0016, ADR-0103), each group through the core edit that checks it, what core refuses said through `editFailed` (ADR-0123), the tempo it starts with (`tempoAt`, `setTempoFrom`; ADR-0042) and whether it moves gradually to the next one (`setTempoGradual`; ADR-0082), its triplet feel (`tripletFeelAt`, `setTripletFeelFrom`; ADR-0104) and the section it starts (`sectionAt`, `setSection`; ADR-0062). Says which symbol of a page is drawn at a point, among those a double click edits, and the measure its value starts at (`symbolAt`, through `elara::notation::symbolAt`; `signatureAt` reads a measure's signatures; ADR-0130). While the song has unsaved changes it keeps a copy of it in `recoveryFolder`, and opens the copies an earlier run left as unsaved songs (`recoverable`, `recover`, `discardRecovery`; ADR-0106). |
| `src/caret_editor.hpp` | `CaretEditor` (QML): what is at the caret of its `document`, read and edited (ADR-0128). The document has the caret; this is what the effects palette, the lyrics popup, the entry bar, the status bar and the keys ask of the place it is on. Says where the caret is for the status line (`caretStatus`) and what kind of staff it is on (`caretFretted`, `caretPitched`, `caretOnDrums`), types note names and drum hits, moves pitches, deletes and inserts rests (ADR-0034, ADR-0035). Toggles note effects at the caret and names them, with their keys (ADR-0038), and sets bends of any form and amount (`setBend`, `caretBendForm`, `caretBendCents`; ADR-0055) and the beat's strum, pick stroke and tremolo picking (`setStrum`, `setPickStroke`, `setTremolo`; ADR-0056) and the kinds of the note effects: slides in and out, harmonics, vibrato and accents (`setSlideIn`, `setSlideOut`, `setHarmonic`, `setVibrato`, `setAccent`; ADR-0058), and the beat's whammy bar and bass technique (`setWhammy`, `setBassTechnique`; ADR-0060) and hairpin (`setHairpin`, `caretHairpin`; ADR-0078) and fermata (`setFermata`, `caretFermata`; ADR-0085) and octave line (`setOttava`, `caretOttava`; ADR-0089) and sustain pedal (`setPedal`, `caretPedal`; ADR-0092) and the break after it (`setBreath`, `caretBreath`; ADR-0095), the trill of its notes (`toggleTrill`, `caretTrill`; ADR-0090) and their mordent or turn (`setOrnament`, `caretOrnament`; ADR-0091) and fingers (`setFingering`, `caretFingering`, `setPluck`, `caretPluck`; ADR-0093) and slur to the next beat (`setSlur`, `caretSlur`; ADR-0086), its grace notes (`toggleGraceNote`, `caretGraceNotes`; ADR-0079), its chord symbol (`setChordSymbol`, `caretChord`, `songChords`; ADR-0080), its text (`setBeatText`, `caretText`, `songTexts`; ADR-0081), and the dynamic from the caret's notes to the next change (`caretDynamic`, `setDynamic`; ADR-0047). Changes the clef of the caret's staff from the caret's measure (`caretClef`, `setClef`; ADR-0087), or of any staff from any measure with the caret left where it is (`clefAt`, `setClefFrom`; ADR-0130), and sets or writes out a measure repeat sign there (`caretRepeat`, `setMeasureRepeat`; ADR-0102). Reads and types the verses of the caret's staff (`caretVerse`, `setVerse`; ADR-0084). Every edit is one of core's edits at a caret, entered through the document, which tells of what the song refuses; `caretChanged` follows the document's. |
| `src/score_common.hpp` | What `ScoreDocument` and `CaretEditor` both need: the model's text as the interface's (`qstring`), the names the core writes translated (`translated`), a note value's denominator, and what kind of staff a caret is on (`onStrings`, `onDrumStaff`). |
| `src/score_page.hpp` | `ScorePage` (QML): one page as a texture, painted on a worker thread at its size in device pixels, in its `paper`, `ink` and `staff`, and painted again when zooming settles or they change; a relayout that leaves the page as it was keeps the texture. On opaque paper the texture is opaque, drawn without blending (ADR-0015). |
| `src/startup.hpp` | What the app does after its window loads: `--track`, `--view`, `--seek`, `--continuous`, `--size`, `--smoke-test`, `--screenshot`, `--play`, `--hold`, for the recovery checks `--recovery-folder` and `--crash-after-recovery-copy` (ADR-0106), and `--window-state`, where the smoke test changes the window's state or finds it as the run before left it (ADR-0137) (`--tuner`, `--song-pane`, `--measure`, `--lyrics`, `--effects`, `--commands`, `--tuning` and `--menu` are properties of the window; the smoke test checks that `--effects` opened the note effects palette, `--commands` the command palette and `--menu` its menu). |
| `qml/Main.qml` | The window (ADR-0108, `docs/interface.md`): the command bar with the menus (File, Edit, View, Sound, Help: every command with its keys, among them Open recent, the exports as PDF (Ctrl+P, ADR-0074), MusicXML and MIDI, and the sound choices), the song string, the tool row (the entry tools, the track picker and the view switch), the welcome pane, the note effects palette, the pages, the right dock with the song panel or the tuner (one at a time; under 1040 px the palette and the dock close each other), drag and drop, errors and notices over the foot of the score (a notice with details, such as what an export left out, shows them on demand; it goes when a sheet opens at the caret) and the status bar; asks to save unsaved changes before they would be lost, and at start offers back the songs a crash left unsaved (ADR-0106). The songs open are a `DocumentSet`, a tab each (`SongTabs.qml`, ADR-0160): `score` is the one shown, which the menus, the tools, the panels and the transport act on; Close song (Ctrl+W), Next song (Ctrl+Tab) and Previous song (Ctrl+Shift+Tab) are in the File and View menus, and quitting asks about each unsaved song in turn. |
| `src/document_set.hpp` | `DocumentSet` (QML): the songs open, a `ScoreDocument` each, as a list model (title, unsaved changes, busy) with the one shown (`current`, ADR-0160). A new song or a file goes into a song of its own, or into the empty one shown; files opened together each start their own jobs, so they load side by side on the worker pool, and a file already open is only shown. A file that does not open says why (`failed`) and its song closes again. Every song pastes from one `core::Clipboard`, so measures copied in one paste into another, sharing their content. Recovers each copy an earlier run left into a song of its own, and says whether any song has unsaved changes and which to ask about next (`unsaved`, `nextUnsaved`). There is always one song, empty when none is open. |
| `qml/SongTabs.qml` | The tab strip under the tool row while two or more songs are open (ADR-0160): a tab per song with its title, a dot while it has unsaved changes and a cross that asks to close it; a click shows it, a middle click closes it. |
| `qml/RecoveryDialog.qml` | Offers back, at start, the songs a crash left with unsaved changes: each with its title, its file (or that it was never saved) and when it was copied, to recover or discard; closing keeps them for the next start (ADR-0106). |
| `qml/CommandBar.qml` | The command bar across the top, dark in both themes: the mark, the menus, the file tools (open, save, undo, redo), the transport (back to the start, play, the readings of the measure playing, the time and the tempo at the speed it plays, then the practice controls), and the buttons of the palette, the song panel and the tuner, lit while each shows. In a narrow window it gives up the file tools, then the tempo and the time, then the metronome and the count-in, and the measure last. |
| `qml/SongString.qml` | The song as one string along the lower edge of the command bar: a tick where measures start (`ScoreDocument::measureStarts`, one in two, four… when they would crowd), lit as far as it has played, the measure at the position riding it, outlined until playback has moved, a taller tick at the caret's measure (`ScoreDocument::measureStart`), the A-B loop as a band, the sections (`ScoreDocument::sectionMarks`) as inlays; a click or a drag moves playback, and the pointer reads the time and the section under it. |
| `qml/Readout.qml` | A reading of the transport in a cell as wide as its widest text, in tabular figures, so that a change moves nothing. |
| `qml/StatusBar.qml` | The status bar: where the caret is, the page, the measures and the changes on import (which open their list) at the left; page or continuous view and the zoom at the right. |
| `qml/AppMenuBar.qml`, `qml/AppMenu.qml`, `qml/AppMenuItem.qml`, `qml/AppMenuSeparator.qml` | The menus on the command bar: a menu as wide as its widest item, each item with the keys that do the same (`keys`) and a check mark while it is the current choice or on (`chosen`). |
| `qml/Picker.qml` | A choice from a list, as high as the other controls, with a check mark on the chosen one and, given `swatches`, a dot of colour before each (the tracks' hues in the track picker). |
| `qml/Field.qml`, `qml/Stepper.qml`, `qml/Fader.qml` | A line of text to type, a number to step or type, and a slider with a thin groove (filled from its middle for a pan), built on Qt Quick's templates so that they are as high as the other controls and take the theme's colours. |
| `qml/Chip.qml`, `qml/TextButton.qml` | A mark that is on or off, and a button with words (`kind`: "main" for the one a dialog is there for, "danger" for the one that takes something away). |
| `qml/Sheet.qml`, `qml/AppDialog.qml` | The card that floats over the score for the caret, and a dialog with its title and its buttons, on the interface's surface. |
| `src/key_names.hpp` | `KeyNames.native()` (QML): the keys of a shortcut as the platform writes them, for the menus. |
| `qml/WelcomePane.qml` | What the window shows before a score is open, in two columns that stand one over the other in a narrow window: at the left the logo, Open and New and the formats it reads; beside them the recent scores, each with its format, name and folder, opened with a click and taken off the list with a cross, or a line that says what the list is for while it is empty. Centred when it fits, scrolled in a short window. |
| `src/recovery_store.hpp` | `RecoveryStore`: the recovery copies in a folder, this run's own copy of the song with unsaved changes and the ones earlier runs left, each held by a lock file while its run goes on; one store per song open, and a copy one store listed is `release`d for another to recover (ADR-0160). A copy is prepared beside its place and renamed there, so removing one never waits for the disk, and what runs that are gone left besides a copy is cleared away (ADR-0106). Built into `elara_app_view` so that tests link it. |
| `src/command_search.hpp` | `CommandSearch` (QML through `qml_types.hpp`): the command palette's search (ADR-0135). Each word typed must be found in a command's name, its group or its words, as a word, the start of one, its initials, a part of a word or letters in order, whatever the capitals and accents; commands that cannot run come last and the eight used last first, kept in the settings while `remember` is on. `searchCommands()` is the ranking as a function, `markedName()` the name with what matched in bold. Built into `elara_app_view` so that tests link it. |
| `src/window_state.hpp` | `WindowState` (QML through `qml_types.hpp`): the window as it was when the app last quit, kept in the settings while `remember` is on (ADR-0137). Follows the window's place and size once they settle, only while it is neither maximized nor full screen, and puts it back where it was, or in the middle of a screen when that place is no longer on one (`placed()`); holds the palette, the song panel, the zoom, the dark page and the continuous view, which `Main.qml` restores and binds. Written as the app quits. Built into `elara_app_view` so that tests link it. |
| `src/recent_scores.hpp` | `RecentScores` (QML through `qml_types.hpp`): the last eight score files opened or saved, the latest first, shown only while they exist and kept in the settings for the next start while `remember` is on (ADR-0071). Built into `elara_app_view` so that tests link it. |
| `src/track_list.hpp` | `TrackList`, behind `ScoreDocument::trackList`: the song panel's tracks as a list model. A new list of tracks changes, inserts or removes only the rows that differ, so a mute or an added track keeps the other rows instead of making them all again. |
| `src/job_lane.hpp` | `JobLane`: a flow of worker jobs, such as laying out or sequencing, where at most one job runs and only the newest request waits; a result is delivered only when no newer request came, and the lane counts what it ran and skipped (ADR-0110). |
| `src/when_done.hpp` | `whenDone()`: hands the result of a worker job to the interface thread from its event loop, never inside the call that started the job, so a call that makes several edits shows only the last one's layout (ADR-0054). Opening, layout, sequencing, painting, saving, exporting PDF and recovery copies use it; a job whose context object is gone delivers nothing. |
| `src/included_sound.hpp` | The SoundFont that comes with the packages (ADR-0069): `includedSoundFont()` finds it around the executable, `startingSoundFont()` picks what the app opens at start from the saved choice. `includedGuitarSfz()` finds the guitar SFZ instrument that comes with them (ADR-0072). |
| `qml/SongPane.qml` | The song panel at the right of the score (Ctrl+I), each track in its own hue (`Theme.trackHue`): title, artist, the other lines of the credits folded under Credits (`ScoreDocument::credits`, `setCredit`; ADR-0059), tempo, time and key signatures from the first measure, length, and the tracks, which can be shown, removed or added for any instrument preset; a double click on a track's name types another (`setTrackName`). Under each track its mix: mute, solo, a volume and a pan fader, heard while they move and kept as one undo step when let go; a track mute or solo keeps quiet fades (ADR-0039). A track with strings shows its tuning and capo under its row (ADR-0046). |
| `qml/ScoreView.qml` | Pages on a desk, or the strips of the continuous view on one sheet (ADR-0057), each placed where `pageTops` puts it and loaded only near the view; smooth zoom (Ctrl + wheel, pinch, Ctrl +/-/0); a song opens at the zoom chosen last (`fitSong()`, ADR-0137), by default with its page across the view, at most at 150 % (`readingZoom`, ADR-0133), from its top (a zoom that starts at the top stays there, `heldAtTop`), and the zoom keeps fitting the page or the width when a panel opens or closes until it is chosen by hand (`fit`); the playback cursor and following it a system at a time, and the measure playing or the caret's shaded across its system (ADR-0170). The caret over its page, a band that says its beat across the notation and the tablature of its staff and a cell on the band for the string (`ScoreDocument::caretColumn` and `caretRect`, ADR-0132), a frame that never covers the fret number or the note (ADR-0173), keyboard entry (digits, arrows, Delete, Insert, `+`, `-`, `.`, Ctrl+2 to Ctrl+9 for tuplets) and clicks that place the caret and move playback to the note there; the view scrolls to keep the caret in sight. After a layout an edit, an undo or a redo caused, the caret's staff stays at its height on screen while the caret stays in its system and was in view (`ScoreDocument::caretHeld`): the view scrolls as far as the layout moved it, at once. Shift+Left/Right select measures, tinted on the page, Ctrl+A selects all and Escape lets go; Ctrl+C copies, Ctrl+V pastes over (Ctrl+Shift+V inserts), and Alt+Up/Down transposes the selection (ADR-0036). Letters A to G type note names (with Shift they join the beat's chord) and Alt+Up/Down moves the pitch a semitone (with Ctrl an octave). Alt+1 to Alt+4 pick the voice typing goes into. On a drum track Up and Down move over the lanes, one per kit piece, and Enter adds or removes the lane's hit (ADR-0035). A double click on a time signature, a key signature, a clef, a tempo mark or a section's name opens its card under it (`SymbolCard.qml`), and on a chord symbol or a beat's text the popup of its beat; the one under the pointer is lit (ADR-0130). Ctrl+M or a right click opens the caret's measure, Ctrl+K the chord symbol of its beat and Ctrl+T its text (`WordsPopup.qml`), Ctrl+Shift+L the lyrics of its staff (`LyricsPopup.qml`); Ctrl+Insert inserts a measure before it, Ctrl+Shift+Delete deletes it. Note effects take one key each: T tie, H hammer-on or pull-off, S slide, U bend, V vibrato, P palm mute, X dead note, L let ring, N natural harmonic, O ghost note, `>` accent, `'` staccato, `_` tenuto, `"` staccatissimo (ADR-0038, ADR-0088, ADR-0094). |
| `qml/EntryBar.qml` | The entry tools, at the left of the tool row (dimmed while there is no caret): the note value typing uses, the dot, the tuplet (a click toggles a triplet, a right click picks another), the voice (a click goes on to the next, a right click picks one), insert a rest, delete, the chord symbol, the text, the lyrics and the measure popup. Where the caret is shows in the status bar. |
| `qml/CommandPalette.qml` | The command palette (Ctrl+E or Ctrl+Shift+P, the Help menu, the command bar's search button; ADR-0135): every item of the menus, read from the menu bar each time it opens, the entry tools, every note effect in the effects palette's groups, the places to go and the tracks to show, found by typing and shown with their keys, dimmed with what they wait for when they cannot run. A number goes to its measure, "tempo 90" and "3/4" set the tempo and the time signature from the caret's measure. Enter runs the command and closes it, Shift+Enter keeps it open, and its own key closes it; opened again, the command used last is chosen. |
| `qml/EffectsPalette.qml` | The note effects palette, docked at the left of the score (F2, ADR-0109): named groups that fold, each effect a tile drawn as the score draws it, lit while the caret's note or beat has it; a click sets it or takes it off. The fourteen effects with a key show it in the corner of their tile. The groups: the keyed effects (the techniques of strings only on fretted staves, ADR-0077), the bend forms and amounts, the slides, the harmonic's kind, the vibrato's width, the accent's strength, the fingerings and the ornaments, the dynamics from ppp to fff with a crescendo and a diminuendo, slurs, grace notes, fermatas, octave lines, the pedal and breaths, the beat's strum, pick stroke, tremolo picking and tapping, slap or pop, and the whammy-bar forms and depths. |
| `qml/MeasurePopup.qml` | The caret's measure in a card next to it, which scrolls in a window too short for it: its time and key signatures and its staff's clef (from it until the next change; not on drum staves), its tempo (in the beat unit in force there; a cross removes the measure's own change; a Gradual chip makes it speed up or slow down to the next change), its swing (straight, or pairs played as triplets, dotted or Scottish, from it until the next change), the name of the section it starts, its signs and jumps (chips for the signs, Fine and To Coda; the jump and how it ends), its repeats (chips for the start and end barlines, how many times the section plays, chips for the endings 1 to 8), a chip for the double barline that closes it, insert a measure before or after it, delete it. |
| `qml/ChordPopup.qml` | The chord symbol of the caret's beat in a card over it (ADR-0080): a name typed and Enter, or a chip with a chord the song already names, sets it, the bin takes it away; on a fretted staff the beat's frets give the diagram. |
| `qml/Theme.qml` | The theme, one object every QML file shares (`docs/interface.md`): the colours of the command bar, the surfaces, text, accent and states in the light and in the dark theme, a hue per track, the sizes of tools, icons and controls, the corner radii, the type sizes and the timings. The window sets its `dark` from the Material theme. |
| `qml/Icons.qml` | The interface's icons, one family of line drawings on a 24 x 24 grid, each an SVG path that `Icon.qml` draws at one size and one width. |
| `qml/Scroll.qml` | The scroll bar of the panels and the lists: slim, shown whenever there is more than fits. |
| `qml/Logo.qml` | The Elara logo, drawn as shapes, one word written with one pen: a calligraphic e crossed by the strings of a tablature (the frets 0 2 2 0 0 0 climbing them as an arpeggio), which leave it and are tied to the l; a; an eighth rest for the r; a. `detailed: false` shows three plain waves for small sizes, `named: false` only the e. `icons/elara-logo.svg` holds the same shapes, `icons/elara.svg` the app icon. |
| `qml/Elevation.qml` | The shadow under what floats over the score (menus, popups, errors). |
| `qml/IconButton.qml` | A square tool button with soft corners showing an icon (`Icon.qml`), with a tooltip; checked, it shows in the accent; `nightBar` gives it the command bar's colours, and `takesFocus: false` leaves the keyboard with the score. |
| `qml/SignatureFields.qml` | Time signature, key signature and mode fields, shared by the song panel, the measure popup and the card of a symbol, which shows the one it is for (`showTime`, `showKey`). |
| `qml/SymbolCard.qml` | A symbol of the score edited where it is drawn (ADR-0130): the card a double click on it opens, with the one thing that symbol is. A time or a key signature, a clef, a tempo (with its Gradual chip and the cross that removes a change) or the name of a section; each changes the song from the measure the symbol's value starts at, which the card names, and leaves the caret where it was. |
| `qml/TuningFields.qml` | A track's tuning: a preset or open strings typed from the lowest, the capo, and whether a change keeps the sound or the frets (ADR-0046). |

## The tuner (ADR-0029)

| File | What it does |
|---|---|
| `src/tuner_controller.hpp` | `TunerController` (QML): while `active`, listens to the default input through `elara::audio::MicrophoneInput` and shows what `elara::audio::Tuner` reads: note, cents, nearest string, input level. Tunings are the core presets, chromatic, or (after `showTuning`) a track's tuning that no preset has, listed as "Track: <name>". |
| `qml/TunerPane.qml` | The tuner, docked at the right of the score where the song panel also docks, one of the two at a time (its button on the command bar, Ctrl+Shift+T): the note inside an arc from -50 to +50 cents with a sliding marker, which way to turn, the instrument's strings, the tuning and the reference A4. It listens only while open, and tunes to the strings of the score's shown track when it opens and whenever that track or its tuning changes; a tuning picked by hand stays until then. |
| `Info.plist.in` | The macOS bundle's Info.plist, with the reason the system shows when it asks for the microphone. |
| `src/app_icon.hpp` | The app icon (ADR-0064): `renderIcon()` draws an SVG image at a size, `icoFile()` and `icnsFile()` pack images as the icon files of Windows and macOS. Built as the `elara_app_icon` library, which the build tool and the tests link. |
| `tools/icon_files.cpp` | `elara-icon-files`: draws `icons/elara.svg` at build time as the window's PNG images (built into the app under `:/icons`), `elara.ico` (compiled into `elara.exe` by `elara.rc.in`) and `elara.icns` (the macOS bundle's icon). |
| `elara.desktop` | The Linux desktop entry, installed with the icon as `io.github.ghost_in_the_groove.Elara`. |

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

The fonts (`Bravura.otf`, `Bravura.json`, `Arimo.ttf`, `Arimo-Italic.ttf`,
`Arimo-Bold.ttf`) are
embedded as resources under `:/fonts`; `qtquickcontrols2.conf` picks the
Material style.

## Playback (ADR-0019)

| File | What it does |
|---|---|
| `src/audio_controller.hpp` | `AudioController` (QML): the transport. Opens the audio device on a worker on the first play, which starts once it has; the output device is the system's default or one chosen among `outputDevices` (`setOutputDevice`, Sound > Output), which takes over at once while playing (ADR-0122). It plays through the `elara::audio::OutputDevice` its factory makes, `SystemAudio` unless a test gives it another (ADR-0126). Prepares the songs of edits, new songs and new sound banks for the player on a worker (ADR-0112), and the first Play's too: playback starts once the song's sounds are made, and a second press meanwhile takes it back (ADR-0148); hands the open score's sequence to an `elara::audio::Player` and reports `playingSong` and `position` about once a frame. An edit that leaves the sequence as it was (a mix, the title) only passes the new mix to the player, so playback carries on; a moving fader does the same without editing the song (ADR-0039). The document builds an edit's sequence on a worker beside its layout; until it arrives the player keeps the previous one, and it is always loaded with the song it was built from (ADR-0031). A new sequence plays on from the same place in the music (`audio::carriedPosition`: the same share of the same played measure after another speed or a tempo edit, else the same time); only another song (`songReplaced`) goes back to the start. A device that restarts at another sample rate (headphones plugged in, Bluetooth switching profiles) gets a player made at that rate on a worker, which takes over from the same place (`Player::takeOver`). For practice (ADR-0024) it starts playback after a measure of clicks (`countIn`, `countingIn` while they sound) and loops from A to B, both marked at the playback position (`markLoop`); the loop keeps its place in the music as the position does, and another song ends it. The tracks play through the built-in synthesizer or a SoundFont (`setSoundFont`, opened on a worker; `useBuiltinSound`; ADR-0043), by default the one that comes with the packages (`includedSoundFont`, ADR-0069) and SFZ instruments for the guitars and the basses (`setSfz`, `clearSfz`; ADR-0066), among them the guitar that comes with the packages (`includedGuitarSfz`, ADR-0072), all kept in the settings when `rememberSound` is on; `trackSound` names the sound a track plays with. |
| `src/score_document.hpp` | `ScoreDocument::duration` and `cursorAt(seconds)`: where the cursor stands on the pages, through `elara::notation::cursorAt` (it steps from one column of notes to the next); `secondsAt(page, x, y)`: where a click moves playback, through `elara::notation::measurePositionAt`. `speed` (25 to 200 %) and `metronome`: what every sequence is built with, kept from song to song; changing either builds the sequence again (ADR-0024). |
| `qml/PracticeControls.qml` | On the command bar, after the readings: the loop button (A, then A–B, then off; Ctrl+L), the metronome button, the count-in button, and the speed as a button that opens a slider with 50, 75 and 100 %. The song is sequenced again once the slider is let go. |

The transport is in the middle of the command bar: back to the start (Home),
play and pause (Space), the measure playing, the time and the tempo, the loop,
the metronome, the count-in and the speed; the song string under it moves
playback and shows the A-B loop. `ScoreDocument::measureAt(seconds)` gives the
measure playing. A click on a note moves playback to it. While the cursor moves, the view brings its system in whole
when it is not, near the top, and a scroll by hand holds that off until the cursor goes into another system
("Follow playback" in the Sound menu turns it off). With playback stopped a click, a key that moves the caret or an edit at it put the caret
and playback together and the cursor hides; an edit from a panel, an undo or a redo (`ScoreDocument::scoreEdited`) hides it where it is, and playback moved by the transport shows it again (ADR-0173). The measure playing, or the caret's, is shaded across its system
(`ScoreDocument::measureBox`, ADR-0170). The A-B loop is a band over the top of its measures on the pages, a
system at a time (`ScoreDocument::measureSpans`, ADR-0172).

## Command line

```
elara [FILE...] [--track N|all] [--view notation|tab|both] [--seek SECONDS]
      [--continuous] [--tuner] [--song-pane] [--measure] [--lyrics] [--language en|es]
      [--soundfont FILE | --included-soundfont]
      [--guitar-sfz FILE | --included-guitar-sfz] [--bass-sfz FILE]
      [--smoke-test] [--screenshot OUT.png] [--play] [--hold SECONDS]
```

- `FILE` opens a score at start, and several files each in a tab of their own,
  loading side by side (ADR-0160); with two, `--smoke-test` checks the tabs
  instead: it copies a measure in the first song, pastes it in the second and
  closes that one, discarding the change (`app.smoke_songs`); `--track` (from 1) and `--view` show that track
  and view once it is laid out; `--seek` moves the playback cursor there,
  paused (the audio device stays closed); `--continuous` shows the continuous
  view (ADR-0057).
- `--smoke-test` loads the window, fails on any QML warning and quits. With a
  file it also waits until the first page is painted and shows the score in
  every view and with all its tracks, in the continuous view (strips no higher
  than a page, stacked without gaps) and back on pages, moves the cursor halfway through and
  clicks on it (playback must move to the note there, the cursor hide and the caret's
  measure be shaded, and Right must move playback with the caret), plays at half speed with
  the metronome and back (playback must keep its place in the music), marks an
  A-B loop (it must follow the speed too), adds a track and undoes it (playback
  and the loop must stay there) and makes a new song (playback must go back to
  the start, without the loop), then prints `smoke-test: ok (P pages, N painted, L layouts)`; a file that cannot be
  opened fails. CTest runs it with `QT_QPA_PLATFORM=offscreen` (`app.smoke`,
  `app.smoke_open_*`, `app.smoke_tuner`), so CI checks the UI without a screen.
- `--follow`, with `--smoke-test`, moves playback through a new song three pages
  long every tick as playing does: the view must bring the cursor's system in
  while it moves, hold still after a scroll by hand with the wheel until the
  cursor goes into another system, and stay put with following off
  (`app.smoke_follow`, ADR-0170); then it marks an A-B loop and checks its
  bands on the pages (ADR-0172).
- `--screenshot OUT.png` saves the window once it is shown and quits.
- `--play` starts playback once the song is shown.
- `--hold SECONDS`, for measuring: once the window is ready (the song laid out
  and painted, playing with `--play`), prints `hold: N s`, stays open that long
  with nothing of the check running, then quits as a check does, leaving the
  settings alone. `scripts/usage.py` measures the processor time and memory the
  app takes meanwhile: idle, and playing through each sound (`app.hold` runs it).
- `--tuner` opens the tuner at start (`app.smoke_tuner` checks it loads cleanly;
  without an audio input it says so instead of listening). With a file,
  `app.smoke_tuner_track` checks that it tunes to the shown bass's strings,
  to an entry of the track's own when the bass is retuned to a tuning no
  preset has, and back on undo, printing `tuner: <strings>` each time.
- `--song-pane` opens the song panel at start (`app.smoke_song_pane`).
- `--measure` opens the caret's measure popup once the score is laid out
  (`app.smoke_measure`), and `--lyrics` the lyrics popup of the caret's staff
  (`app.smoke_lyrics`).
- After the views, `--smoke-test` edits the song, or a new one when no file is
  given: it sets the subtitle and undoes it, adds a track, changes the time signature, undoes both, redoes and
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
  adds and removes a snare hit, and undoes back to the start. Then it selects
  two measures, checks the page tints them and the status line names them,
  copies them, pastes them before the caret's measure, checks the pasted
  measures are selected, transposes them and undoes back to the start. Then
  it types a note, toggles accent and staccato and the accent off again, checks
  the effects, the undo name, the status line and the effect keys, sets a bend
  and release of a half step, replaces it with a pre-bend and takes it off,
  checking the form, the amount and the status line, strums the beat down,
  adds tremolo picking and an up stroke and takes the strum off again, sets
  wide vibrato, a marcato, slides in from below and out downwards and an
  artificial harmonic, checking the status line, dips the whammy bar 1 1/2
  steps and slaps the beat, then takes the dip off, puts the beat under a
  diminuendo and takes it off, checking the status line, and undoes back to
  the start.
- `--soundfont FILE` plays the tracks through that SoundFont (SF2 or SF3)
  instead of the one chosen last time in the menu (More, then Choose a
  SoundFont). With `--smoke-test` the app prints `sound: NAME` once it has
  opened it, then checks that a file that is no SoundFont fails and leaves it
  in place, and that the built-in synthesizer comes back (`app.smoke_soundfont`,
  with the CLI tests' test font). Checks and screenshots never read or write
  the remembered SoundFont.
- `--included-soundfont` does the same with the SoundFont that comes with the
  packages, MuseScore General (ADR-0069), found next to the app
  (`share/elara/soundfonts`, or `Resources/soundfonts` in the macOS bundle),
  and fails with a message when the build has none. Without either option
  the app opens the SoundFont chosen last time, or the included one when none
  was ever chosen or the file chosen is gone (`app.smoke_included_soundfont`
  and the package smoke tests).
- `--guitar-sfz FILE` and `--bass-sfz FILE` play the guitars or the basses
  through an SFZ instrument (ADR-0066); with `--smoke-test` the app prints
  `sfz: guitars NAME, basses NAME` and the sound of each track once they are
  open (`app.smoke_sfz`). `--included-guitar-sfz` plays the guitars through
  the instrument that comes with the packages, FreePats' clean electric
  guitar (ADR-0072), found in a folder of its own next to the app
  (`share/elara/sfz`, or `Resources/sfz` in the macOS bundle), and fails with
  a message when the build has none (`app.smoke_included_guitar_sfz` and the
  package smoke tests). "Use the included guitar" in the menu does the same.
- `--language es` shows the UI in Spanish (`en` in English) instead of the
  system's language. With `--smoke-test` or `--screenshot` the app first prints
  `ui language: es`, which `app.smoke_spanish` checks; `app.language_unknown`
  checks that a language without a catalog is refused.

## Tests

`elara_app_tests` (`tests/app/`, `app.Score painter: ...`) paints display lists
on the offscreen platform and checks the pixels: lines, beams, glyphs, knocked-out
text, ties, a laid-out page inside its margins. Its hidden painting benchmarks
run with `python scripts/elara.py bench`.
