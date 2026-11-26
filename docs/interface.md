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
+-----------+----------------------------------------------+---------------+
| note      |                                              |               |
| effects,  |                  the page                    | song and      |
| in groups |                on the desk                   | tracks        |
|           |                                              |               |
+-----------+----------------------------------------------+---------------+
| Measure 12 . Beat 2 . String 3   Page 1 of 4 . 64 measures    view  zoom |  status bar
+--------------------------------------------------------------------------+
```

| Part | File | What it holds |
|---|---|---|
| Command bar | `CommandBar.qml` | The mark, the menus, the file tools, the transport with its readings and the practice controls, the panels and the tuner. Dark in both themes. |
| Song string | `SongString.qml` | The lower edge of the command bar: the song as one line, lit as far as it has played, the measure playing riding it, the A-B loop as a band, the sections as inlays. Click or drag to move playback. |
| Tool row | in `Main.qml`, `EntryBar.qml` | What typing uses at the left; the track and the staff view at the right. |
| Note effects | `EffectsPalette.qml` | Every technique, articulation, dynamic and line the caret's note or beat can take, in named groups that fold (F2, ADR-0109). A tile is lit while the caret has what it sets. |
| Score | `ScoreView.qml` | The pages on the desk, the caret, the playback cursor, the popups the caret opens. |
| Song and tracks | `SongPane.qml` | The song's own values and each track's mix and tuning (Ctrl+I). |
| Status bar | `StatusBar.qml` | Where the caret is, the page and the measures; the layout and the zoom. |

Narrow windows drop what is offered elsewhere, never overlap: the command bar
gives up the file tools, then the tempo, the time and the measure readings; the
tool row gives up the view switch. The page keeps fitting the room between the
panels until its zoom is chosen by hand.

## Colours

All in `Theme.qml`; a QML file never writes a colour of its own.

| Name | For |
|---|---|
| `night`, `pearl`, `pearlSoft`, `lilac` | The command bar, in both themes: its ground, what is written on it, what is quieter on it, and the string and what is lit on it. `nightHover`, `nightPressed`, `nightLine` and `nightWell` are its states, hairlines and the readings' well. |
| `desk` | What the pages lie on. |
| `bar` | The tool row, the panels and the status bar. |
| `surface` | Fields, cards and what floats. |
| `line` | Hairlines between and around surfaces. |
| `ink`, `inkSoft`, `faint` | Text and icons; text that explains or counts; ticks and outlines that only hint. |
| `hover`, `pressed` | A control under the pointer, and pressed. |
| `accent`, `accentInk`, `accentWash`, `accentText` | The accent as a fill, as text or an icon, as a wash behind what is chosen, and what is written on the fill. |
| `danger…`, `success`, `warning`, `solo` | States, each only where it means that. |
| `trackHue(index)` | A hue per track, the same wherever the track shows. |

The accent says one of three things: this plays, this is chosen, or this is
the main action. It never decorates. Secondary text uses `inkSoft`, not a
lowered opacity; disabled controls drop to 38 %, never further, so that they
stay readable.

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
| `Icon` with a path from `Icons` | Every icon: one family of line drawings on a 24 grid, round ends, one width. Add a new one to `Icons.qml`; never write a path where it is used. Signs of notation (note values, dynamics) are glyphs of the music font through `MusicSymbol`. |
| `IconButton` | A tool with an icon and a tooltip. `nightBar` on the command bar; `takesFocus: false` for tools of the score. |
| `SegmentedControl` | A few choices of which one is always chosen. |
| `Picker` | One choice from a list. |
| `Field`, `Stepper`, `Fader` | A line of text, a number, a slider (`centred` for a pan, `tint` for a track's hue). |
| `Chip` | A mark that is on or off, or one of a few words to pick. |
| `TextButton` | A button with words: `kind: "main"` once per dialog or pane, `"danger"` for what takes something away. |
| `Sheet`, `AppDialog` | What floats over the score for the caret; a dialog that asks or tells. |
| `AppMenuBar`, `AppMenu`, `AppMenuItem`, `AppMenuSeparator` | Menus. `keys: KeyNames.native("Ctrl+S")` writes the keys the platform's way; `chosen` marks the current choice or a switch that is on. |
| `Readout` | A reading on the command bar. |
| Tiles of `EffectsPalette` (`IconTile`, `LabelTile`, `SymbolTile`) | A sign the caret's note or beat can take: drawn as the score draws it, lit while the caret has it, with its key in the corner when it has one. |
| `Elevation` | The shadow under what floats. |

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
  while the window is at rest, and no animation runs for longer than `calm`.
- A new command goes into a menu first, then, if it is used all the time,
  onto a bar.
- A field has its name before it, not inside it: a placeholder only shows an
  example of what to type.
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

`--tuner`, `--song-pane`, `--measure`, `--lyrics`, `--effects` and `--tuning`
open a panel at start, and `--menu view` a menu, for screenshots of each. Set
`QT_QUICK_CONTROLS_MATERIAL_THEME=Dark` or `Light` to see a theme; look at both.
