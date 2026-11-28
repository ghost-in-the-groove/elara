# The interface

How Elara's window is laid out and what its parts are made of. Read this before
adding a control, a panel or a colour; the decisions behind it are in
ADR-0108.

## What it is for

A player sits with the instrument on their lap, often in the evening, reading
from arm's length with busy hands. A writer types a song from the keyboard for
a long stretch. So:

1. The score is the product. Nothing covers the music but what the caret asked
   for, and the page is the brightest thing in the window.
2. What a player needs while holding the instrument is one glance and one key
   away: where the song is, how fast, the loop, the metronome.
3. Tools are found by looking, learnt by their keys, and then stay out of the
   way. Every command has a name in a menu and shows its keys.
4. Familiar where people have habits from other notation programs; calmer
   where those are cluttered.
5. Nothing costs frames, memory or battery for decoration.

## The window

```
+--------------------------------------------------------------------------+
| mark  File Edit View Sound Help   open save undo redo                    |
|              |< (play) [ measure | time | tempo ]  loop metro count 100% |  command bar
|=====[12]=======o==========================o==============================|  song string
| note values . 3 V1 | rest delete effects chord text lyrics measure       |
|                                                    [track] [Not|Tab|Both]|  tool row
| [Riff]  [• Ballad  x]  [Etude]                                           |  song tabs
+-----------+----------------------------------------------+---------------+
| note      |                                              | song and      |
| effects,  |                  the page                    | tracks,       |
| in groups |                on the desk                   | or the tuner  |
|           |                                              |               |
+-----------+----------------------------------------------+---------------+
| Measure 12 . Beat 2 . String 3   Page 1 of 4 . 64 measures    view  zoom |  status bar
+--------------------------------------------------------------------------+
```

| Part | File | What it holds |
|---|---|---|
| Command bar | `CommandBar.qml` | The mark, the menus, the file tools, the transport with its readings and the practice controls, the button of the command palette, and the buttons of the effects palette, the song panel and the tuner, each lit while its panel shows. Dark in both themes. |
| Song string | `SongString.qml` | The lower edge of the command bar: the song as one line with a tick where measures start, lit as far as it has played, the measure at the position riding it (outlined at the start, filled once playback has moved), a taller tick at the caret's measure, the A-B loop as a band, the sections as inlays. Click or drag to move playback. |
| Tool row | in `Main.qml`, `EntryBar.qml` | What typing uses at the left; the track and the staff view at the right. |
| Song tabs | `SongTabs.qml` | A tab for each song open, while there are two or more (ADR-0160): its title, a dot while it has unsaved changes, a cross that closes it (Ctrl+W, or a middle click). Ctrl+Tab and Ctrl+Shift+Tab show the next and the previous. |
| Note effects | `EffectsPalette.qml` | Every technique, articulation, dynamic and line the caret's note or beat can take, in named groups that fold (F2, ADR-0109). A tile is lit while the caret has what it sets. |
| Score | `ScoreView.qml` | The pages on the desk, the caret, the playback cursor, the shaded measure, the popups the caret opens. With playback stopped the caret marks where it starts and the cursor hides; while it plays the view follows its system (ADR-0170). |
| Song and tracks | `SongPane.qml` | The song's own values and each track's mix and tuning (Ctrl+I). |
| Tuner | `TunerPane.qml` | The note heard, how far it is from in tune and the strings of the shown track (Ctrl+Shift+T). It docks where the song panel does, one of the two at a time, and listens only while it shows. |
| Status bar | `StatusBar.qml` | Where the caret is, the page and the measures; the layout and the zoom. |
| Command palette | `CommandPalette.qml` | Every command by name, with its keys (Ctrl+E, ADR-0135): the menus' items, the entry tools, the note effects, the places to go and the tracks. It floats at the top of the window only while asked for, like a popup of the caret. |

Narrow windows drop what is offered elsewhere, never overlap: the command bar
gives up the file tools, then the tempo and the time readings, then the
metronome and the count-in, which the Sound menu has, and the measure reading
last; the tool row gives up the view switch. Under 1040 px the score keeps a
panel at one side only: opening the palette closes the panel at the right, and
opening that one closes the palette. The page keeps fitting the room between
the panels until its zoom is chosen by hand.

The window opens as it was last closed (ADR-0137): its place and size, or
maximized, on the screen it was on while that screen is still there; the
palette and the song panel open or closed; the zoom, the dark page and the
continuous view. The tuner always starts closed.

Several songs can be open at once, a tab each (ADR-0160). A new song or a
file opened goes into a tab of its own, beside the others, and is shown; a
file already open is only shown. The menus, the tools, the transport and the
panels act on the song shown; each song keeps its own caret, history, view
and place in the score, while the zoom and the continuous view are the
window's. Measures copied in one song paste into another. Closing a song
asks whether to save its changes; quitting asks so for each unsaved song in
turn, showing it.

Nothing floats over the music but what the caret opened, and the command
palette while it is asked for. A panel docks and the score makes room; an
error slides up over the foot of the score and goes; what an import changed
stays in the status bar for as long as the song is open.

## Colours

All in `Theme.qml`; a QML file never writes a colour of its own.

| Name | For |
|---|---|
| `night`, `pearl`, `pearlSoft`, `lilac` | The command bar, in both themes: its ground, what is written on it, what is quieter on it, and the string and what is lit on it. `nightHover`, `nightPressed`, `nightLine` and `nightWell` are its states, hairlines and the readings' well. |
| `desk` | What the pages lie on. |
| `nightPage`, `nightInk` | The page of the score in the dark theme and the ink on it (ADR-0114). In the light theme, and whenever the score leaves the program, it is black on white. |
| `bar` | The tool row, the panels and the status bar. |
| `surface` | Fields, cards and what floats: a step under the paper, which is the brightest surface of the window in both themes. |
| `line` | Hairlines between and around surfaces. |
| `ink`, `inkSoft`, `faint` | Text and icons; text that explains or counts; ticks and outlines that only hint. |
| `hover`, `pressed` | A control under the pointer, and pressed. |
| `accent`, `accentInk`, `accentWash`, `accentText` | The accent as a fill, as text or an icon, as a wash behind what is chosen, and what is written on the fill. |
| `danger…`, `success`, `warning`, `solo` | States, each only where it means that. |
| `trackHue(index)` | A hue per track, the same wherever the track shows: its card and its faders in the song panel, the dot before its name in the track picker. None of them is the accent's violet. |

The accent says one of three things: this plays, this is chosen, or this is
the main action. It never decorates. Only Play and the main button of a dialog
are filled with it; whatever is chosen (a tile, a note value, a choice of a
switch, a chip) wears `accentWash` with `accentInk` on it and a hairline of
the same. Secondary text uses `inkSoft`, not a lowered opacity; disabled
controls drop to 38 %, never further, so that they stay readable.

## Sizes and timings

| Name | Value | For |
|---|---|---|
| `tool` | 32 | A square tool button. |
| `icon` | 20 | The icon in it. |
| `control` | 30 | The height of a field, a picker or a switch. |
| `radiusSmall`, `radiusCard`, `radiusSheet` | 6, 8, 10 | Controls; cards and menus; sheets that float. |
| `fontCaption`, `fontSmall`, `fontBody`, `fontTitle` | 11, 12, 13, 15 | The interface's type, in the system's font. |
| `quick`, `calm` | 120, 180 ms | A state under the pointer; what slides or opens. |

Figures that change (time, measure, tempo, zoom, cents) use tabular numerals
and sit in a cell as wide as their widest value (`Readout.qml`), so that a
change never moves what is beside them.

## Parts

| Part | Use it for |
|---|---|
| `Icon` with a path from `Icons` | Every icon: one family of line drawings on a 24 grid, round ends, one width. Add a new one to `Icons.qml`; never write a path where it is used. A sign the page prints from the music font (note values, dynamics, articulations, ornaments, fermatas, the pedal, the ottavas, the pick strokes) is that glyph, through `MusicSymbol`, so that a tool looks like what it writes. |
| `IconButton` | A tool with an icon and a tooltip. `nightBar` on the command bar; `takesFocus: false` for tools of the score. |
| `SegmentedControl` | A few choices of which one is always chosen. |
| `Picker` | One choice from a list; `swatches` puts a dot of colour before each. |
| `Field`, `Stepper`, `Fader` | A line of text, a number, a slider (`centred` for a pan, `tint` for a track's hue). |
| `Chip` | A mark that is on or off, or one of a few words to pick. |
| `TextButton` | A button with words: `kind: "main"` once per dialog or pane, `"danger"` for what takes something away. |
| `Sheet`, `AppDialog` | What floats over the score for the caret; a dialog that asks or tells. |
| `AppMenuBar`, `AppMenu`, `AppMenuItem`, `AppMenuSeparator` | Menus. `keys: KeyNames.native("Ctrl+S")` writes the keys the platform's way; `chosen` marks the current choice or a switch that is on. |
| `Readout` | A reading on the command bar. |
| Tiles of `EffectsPalette` (`IconTile`, `LabelTile`, `SymbolTile`) | A sign the caret's note or beat can take: drawn as the score draws it, lit while the caret has it, with its key in the corner when the key is a letter (the tooltip names every key). |
| `Elevation` | The shadow under what floats. |
| `Scroll` | The scroll bar of a list or a panel that can be cut by its edge: it shows whenever there is more. |

The controls that hold a value are built on Qt Quick's templates
(`QtQuick.Templates`), not restyled from the Material style: that keeps them
30 px high and lets the theme colour every part of them. Use these parts in
new panels; a stock `TextField`, `SpinBox`, `ComboBox`, `Slider`, `Button` or
`Dialog` in a new file is a mistake.

Rules that keep the parts one family:

- A label keeps its weight when it becomes the chosen one.
- Every tool has a tooltip that names it and its keys.
- A tool of the score never takes the keyboard from it (`focusPolicy: Qt.NoFocus`);
  the focus ring shows only for the keyboard.
- A state under the pointer changes at once or within `quick`; nothing moves
  while the window is at rest, and no animation of a control or a panel runs
  for longer than `calm`. The score's own scrolling, to the caret and after
  the playback cursor, has its timings in `ScoreView.qml`.
- A new command goes into a menu first, then, if it is used all the time,
  onto a bar. A menu's commands are in the command palette by themselves; a
  command that lives on a tile or a tool gets a line in `CommandPalette.qml`
  (ADR-0135). Hide a menu item with `shown`, not `visible`.
- The command palette is how a command is found by name: its rows show the
  keys, dim what cannot run and say at their foot what it waits for.
- A field has its name before it, not inside it: a placeholder only shows an
  example of what to type.
- An empty list says what it is for, in a sentence: the recent scores before
  any was opened.
- Nothing plays an entrance: the window opens into its task. The home shows at
  once, and a panel slides only when someone opens it.
- What the caret opens stays inside the view: a popup taller than the window
  scrolls (the smoke test checks that it does not leave it).
- An icon is a shape, and a shape costs about a millisecond to make. What
  shows many of them only now and then makes them when it shows: the check
  marks and arrows of the menus, the tiles of the palette. Measure the start
  (`--screenshot` timed against `main`) after adding a panel.

## Checking a change

The app runs without a screen (`CONTRIBUTING.md`). For the interface:

```
python scripts/elara.py test -R app.smoke     # the window loads with no QML warnings,
                                              # no group of a bar overlaps another
python scripts/elara.py app -- song.gp --screenshot shot.png [--size 760x480]
                                              # the window as an image, at a size
```

`--tuner`, `--song-pane`, `--measure`, `--lyrics`, `--effects`, `--commands` and `--tuning`
open a panel at start, and `--menu view` a menu, for screenshots of each. Set
`QT_QUICK_CONTROLS_MATERIAL_THEME=Dark` or `Light` to see a theme; look at both.
