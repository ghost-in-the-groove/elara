# 0135. The command palette

- Status: Accepted
- Date: 2026-11-27

## Context

The window has some two hundred commands: about sixty in the menus, the entry
tools of the tool row, and some ninety note effects, lines and marks in the
palette at the left (ADR-0109). A command is found by knowing where it lives,
and its keys by hovering over its tile. Nothing finds a command from a word,
nothing goes to a measure by its number, and a command with no key, such as a
dynamic, needs the pointer every time.

How other programs answer the same need:

- **Guitar Pro 8** has a command palette (Ctrl+E, Tools › Show Command
  Palette), a bar above the score. A word finds commands ("ff" finds the
  dynamic); commands that cannot run with the selection are greyed out rather
  than hidden; some take an argument ("add-bar 20", a bend's amount).
- **Dorico** has the jump bar (J): commands by name, matched by snippets and
  initials, with their keys; "b32" goes to a bar, "p6" to a page. Opened
  again, the previous command is chosen, so J and Return repeat it. Commands
  that cannot run are hidden.
- **MuseScore Studio 4** has no search of commands. Ctrl+F goes to a measure
  (a bare number), a page or a rehearsal mark; the palettes have a search box
  of their own.
- Outside music, Visual Studio Code's palette (Ctrl+Shift+P) lists the
  commands used last first, shows each one's keys, and in another language
  also finds a command by its English name.

## Decision

**One palette for every command, opened with Ctrl+E** (Guitar Pro's key, which
tablature writers know) **or Ctrl+Shift+P**, from the Help menu ("Find a
command…") and from a button of the command bar. It floats at the top of the
window, over the score, until a command runs or Escape closes it.

- **What it offers**: every item of the menus, read from the menu bar each
  time it opens, so that a command added to a menu is in the palette with its
  keys and state without more work; the entry tools (note values, the dot,
  tuplets, voices, rests, deleting, the pitch); every note effect, line and
  mark of the effects palette, in the same groups; the first, last, next and
  previous measures, each section by its name, and each track to show.
- **What a row shows**: a check mark when the command is on (the caret's note
  has the effect, the view is chosen), the name with the letters typed in
  bold, its group, and its keys. A command that cannot run now is listed after
  those that can, dimmed, and the line at the foot says what it waits for
  ("Click a staff to put the caret there first."); Guitar Pro's choice, kinder
  than hiding it.
- **Search** (`CommandSearch`, in `elara_app_view`): each word typed must be
  found in the name, the group or more words a command carries (the signs the
  score writes for it, such as P.M. or 8va, and its English name), as a whole
  word, the start of one, the initials of the words ("pm" for palm mute), a
  part of a word, or its letters in order. Capitals and accents do not count,
  so "armonico" finds "Armónico". The name counts most, and a name that what
  was typed covers more of comes first.
- **Recent first**: the commands used last (eight) come first among close
  matches and lead the list when nothing is typed, the latest chosen: the
  palette's key and Enter repeat the last command, as Dorico's jump bar does.
  They are kept in the settings like the recent scores, and only in memory for
  checks and screenshots.
- **What can be typed besides names**: a number goes to that measure, the
  caret and playback (MuseScore's Ctrl+F); "tempo 90", "q=90" or "90 bpm"
  sets the tempo and "3/4" the time signature from the caret's measure, like
  Guitar Pro's commands with an argument and Dorico's popovers. The empty
  field says so, and so does the line shown when nothing matches.
- **Keys inside it**: Up and Down choose, Enter runs and closes, Shift+Enter
  runs and keeps it open for the next command, Escape closes. The command runs
  once the palette has closed, so a popup it opens at the caret takes the
  keyboard, and the caret is brought into view after an edit at it.
- A menu item that hides itself does it with `shown`, not `visible`: a closed
  menu's items all read as not visible.

**Verified headlessly** through `app.Command search: ...` (the ranking, the
recent commands and their settings), `app.editing: the caret goes to a
measure's first beat ...` (`ScoreDocument::caretToMeasure`) and the smoke
test, which types into the palette as a person does, Ctrl+E included: a
note effect found and put on the caret's note, Ctrl+E and Enter repeating it,
a number taking the caret and playback to its measure, "tempo 90" setting the
tempo while the space typed plays nothing, and Escape doing nothing. The CLI
has no counterpart: the palette runs the window's own commands, and every
edit it makes is one the CLI's `edit` steps already make.

## Consequences

- A command is found by a word, in Spanish or in English, and its keys are
  learnt where it is found.
- New menu items join the palette by themselves; new note effects, entry tools
  and places need a line in `CommandPalette.qml` beside their tile or tool.
- The palette builds its list each time it opens, about two hundred plain
  objects; the rows are drawn only as they show.
- Assigning keys or aliases from the palette, Dorico's `=alias`, and asking
  what a key does are left for later.
