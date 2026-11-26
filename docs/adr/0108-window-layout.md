# 0108. The window: command bar, song string, tool row and status bar

- Status: Accepted
- Date: 2026-11-26

## Context

The window had grown piece by piece. A toolbar held the file tools, the title,
the view switch and a More menu with everything else (recent files, exports,
the sound choices); the entry tools sat in a strip that came and went with the
caret; the transport floated as a pill over the bottom of the page, and errors
stacked over it. Three things were wrong with that:

- **The transport covered the music.** The last systems of a page showed under
  it, and a notice hid still more.
- **Commands were hard to find.** Exports, the sound and the measure commands
  had no name anywhere but a menu behind three dots; most had keys that nothing
  showed.
- **The parts did not match.** Icons were drawn at two sizes and two weights
  (the entry tools scaled theirs up), controls were round pills in one place and
  outlined boxes in another, and the chosen label of a switch changed weight and
  moved its neighbours.

Before redrawing it we looked at how the programs our users come from lay out
the same job, from their manuals and reviews:

- *Guitar Pro 8* keeps one window: a toolbar with the transport and a display
  of the measure, the time and the tempo in the middle, a palette of every sign
  at the left whose buttons light up with the caret's note, an inspector at the
  right. Its users miss a dark theme and complain of a wall of more than a
  hundred unlabelled icons.
- *MuseScore Studio 4* has a status bar that says what is selected at the left
  and holds the view and the zoom at the right, flat controls, and a focus ring
  that shows only for the keyboard.
- *Dorico* hides and shows its panels with neighbouring keys, never changes the
  weight of a label that becomes active, and is faulted for grey text on grey
  and for buttons without tooltips.

## Decision

The window is four fixed bands around the score, and nothing floats over the
page but what the caret asks for.

**The command bar** (`CommandBar.qml`, 48 px) runs across the top and is dark
in the light theme too: the night violet of the app icon, with its lilac for
the strings. It holds, from the left: the mark; the menus (`AppMenuBar.qml`:
File, Edit, View, Sound, Help), where every command has a name and shows its
keys; the file tools (open, save, undo, redo); the transport (back to the
start, play, the readings, loop, metronome, count-in, speed); the panels and
the tuner. Play is the one filled control of the window and the one round one.

**The readings** (`Readout.qml`) are the measure playing, the time and the
tempo at the speed it plays, each in a cell as wide as the widest text it can
show, with tabular figures: a change of figures moves nothing.

**The song string** (`SongString.qml`) is the lower edge of the command bar:
one line as long as the song. What has played is lit; the measure playing
rides it as a fret number rides a string of tablature; the A-B loop is a band
on it and each section an inlay where it first plays. A click or a drag moves
playback, and the pointer reads the time and the section under it. It replaces
the seek bar and is the one decorative idea of the window, taken from the logo.

**The tool row** (46 px) is under it while a score is open: the entry tools at
the left (`EntryBar.qml`), what the score shows at the right (the track and
notation, tablature or both). It no longer comes and goes with the caret;
without one its tools are dimmed.

**The status bar** (`StatusBar.qml`, 28 px) says where the caret is, the page
and the measures at the left, and holds the layout and the zoom at the right.

In a narrow window the command bar gives up, in this order, the file tools,
the tempo, the time and the measure, all of which the menus or the score also
show; the tool row gives up the view switch, which the View menu has. Nothing
ever overlaps or leaves the window.

**One set of parts** (`docs/interface.md`): colours, sizes and timings in
`Theme.qml`; one family of line icons in `Icons.qml`, drawn at one size and
one width; tools 32 px square with 6 px corners; fields and switches 30 px
high. The accent is for what plays, what is chosen and the main action, never
decoration. A chosen label keeps its weight. Tools of the score never take the
keyboard from it. State changes take 120 to 180 ms and nothing moves at rest.

`ScoreDocument` gains `measureAt(seconds)` and `sectionMarks` for the readings
and the string, and `KeyNames.native()` writes a shortcut as the platform does.

### Checked without a screen

- `app.smoke` and every other smoke test fail when a group of the command bar
  or of the tool row overlaps another or leaves the window.
- `app.smoke_narrow` runs the same checks in the smallest window
  (`--size 760x480`).
- After moving playback, the smoke test reads the measure from the transport's
  reading and from the song string and compares them with the sequence; after
  naming a section it looks for its mark at the start of the string.

## Consequences

- The score is never covered by the transport, and a notice only covers the
  foot of the page.
- Every command can be found by reading the menus, with its keys. New commands
  go into a menu first.
- The bar costs no frames: the string and the readings are a few rectangles
  and labels that change with the playback position, as the seek bar did.
- The menus are drawn by the window on every platform. On macOS a program is
  expected to put them in the screen's menu bar; that has not been tried on a
  Mac and may need its own decision.
- The note effects are still a panel that opens over the score; docking them
  as a palette at the left is the next task of the redesign, then the song
  panel, the popups and the tuner, which still use the controls' stock fields.
- Tooltips still spell keys as "Ctrl+S" on every platform; only the menus
  write them the platform's way.
