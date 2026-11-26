# 0032. Keyboard entry in the app

- Status: Accepted
- Date: 2026-11-23

## Context

The core can enter notes at a caret (ADR-0021): a caret on a beat and a string,
the moves of the arrow keys, and edits for typing frets, durations, rests and
deletes, each returned as a command. `elara-cli edit` drives them in scripts.
The app needs the same on the score: a caret drawn where typing goes, keys that
move it and type, a click that puts it under the pointer, and controls for the
note value. Pages are display lists laid out on worker threads (ADR-0015),
which until now did not say where a beat is.

## Decision

**The layout records its columns.** Each measure's columns (the times at which
notes start, shared by every staff of a system) are placed by the layout and
then thrown away. The display list now keeps them as `ColumnBox`es (measure,
start time, x), moved with their system onto the page. They cost a few bytes
per column and are listed by `elara-cli render --json` under `"columns"`.

**Caret geometry lives in the notation module.** `notation::caretBox()` turns a
caret into a rectangle on a page: on its string over the fret number on
tablature (the tablature when both views are shown), across the staff on
standard notation. The free slot after the last beat sits where its time falls
between the columns around it. `notation::caretAt()` does the reverse for a
click: the measure under the point, the nearest staff, string and beat. Both
are pure functions of a display list and its song, tested without a screen; a
click at a caret's box gives that caret back for every beat, free slot and
string of the tests' songs.

**The app keeps the caret; the core edits.** `ScoreDocument` holds the caret
and the input duration. Keys, the entry bar and clicks call the core's entry
functions and execute their commands, so every entry is undoable. The caret is
clamped after each edit, undo and redo, follows the shown track, and is placed
again when a new layout arrives. Moving the caret onto a beat makes its value
the input duration, so the next note on the free slot continues the rhythm.

**Two digits make one fret.** A digit types its fret at once. A second digit
within 700 ms on the same caret makes a two-digit fret (up to the 30 frets the
core allows): the first note is undone and the two-digit one entered, so it is
one undo step. Off the neck, the second digit starts a fret of its own.

**Keys.** Digits type frets; Left/Right move by beat (with Ctrl by measure);
Up/Down move between strings; Delete or Backspace removes the note (with Ctrl
the beat); Insert adds a rest; `+` and `-` pick the next longer or shorter
value; `.` toggles the dot. The score view takes the keyboard when a song is
opened, started or clicked; buttons of the entry bar never take it.

**Light animation.** The caret glides between beats on a page (90 ms) and the
view scrolls to keep it in sight (160 ms); nothing animates while idle, so the
caret does not blink and costs no frames when nothing moves.

## Consequences

- Each entry is still a whole relayout and a copy of the song (ADR-0031): well
  under a frame for songs of a few hundred measures, more for the largest.
  Incremental relayout and copy-on-write measures will make entry on large
  songs cheaper without changing this design.
- The caret is drawn by QML over the page texture, so moving it never repaints
  a page.
- The caret starts in voice 1. Alt+1 to Alt+4 and the entry bar's voice
  button take it to another voice of its measure, at its beat or the nearest
  one that voice has; the status line names any voice but the first. The
  core and `caretAt()` keep the voice, so a click stays in it, and the CLI's
  `voice <1-4>` step does the same (`cli.edit_types_in_voice_two`).
- `--smoke-test` types on the first fretted track shown: a click on the caret
  keeps it, two digits make fret 12 as one undo step, a duration, a rest
  inserted and deleted, then everything undone back to the original song.
  `tests/app/test_fret_entry.cpp` checks the rules for the second digit
  through the document: off the neck, after a 0, after the caret moved or an
  undo, too late, and on a track without strings. The rule itself is
  `core::EditSession::typeDigit()` (ADR-0127), tested in
  `tests/core/test_edit_session.cpp` with the time given to it.
  `tests/app/test_score_editing.cpp` checks the rest of what the keys do
  through the document: where the caret goes, what the input duration
  follows, what a selection covers, and what copy, paste, transpose and undo
  do to them.
