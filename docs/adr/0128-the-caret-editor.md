# 0128. What is at the caret has a class of its own

- Status: Accepted
- Date: 2026-11-26

## Context

`ScoreDocument` was the one class QML saw of a song: 99 properties and 113
invokable methods over some 3300 lines. About a third of each was not about
the song, its file, its pages or its playback, but about the place the caret
is on: 33 properties and 52 methods that read the effects of the caret's
notes and beat and set them, one pair for every effect the model has, with
their names for the menus and the messages for what the song refuses. A new
effect adds to that third and to nothing else; a change to how files are
saved or pages laid out has no use for it.

With the editing session in core (ADR-0127), that third depends on little of
the document: the song being edited, the caret, and a way to put an edit in.

## Decision

- `CaretEditor` is what is at the caret of a document, read and edited: the
  notes of its beat, their effects, the chord, text and lyrics over it, the
  clef and the measure repeat of its measure, and the line that says where the
  caret is. QML makes one beside the document and gives it to the effects
  palette, the lyrics popup, the measure popup, the entry bar, the status bar
  and the score view.
- The rule for what goes where: the document has the session, so what moves
  the caret, changes the input duration, selects, copies and pastes is the
  document's, with the caret's box on the pages. What reads the song at the
  caret, or makes one of core's edits at a caret (`entry.hpp`), is the
  editor's. A typed digit is the document's, because the wait for a second
  one is the session's.
- The editor reaches the document through five methods, which QML does not
  call: `editedSong()`, `session()`, `enter()` for an edit, `endTyping()`
  before one that may never be entered, and `refuse()` for what cannot be
  made. It holds no state of its own. Its `caretChanged` is the document's,
  which says so when the caret moves and when the song changes.
- What the song refuses is still told by the document's `editFailed`, so the
  window listens in one place.
- The editor's texts are translated in its own context,
  `elara::app::CaretEditor`; the translations moved with them.
- `score_common.hpp` has what both classes need: the model's text as the
  interface's, the translated names, and what kind of staff a caret is on.

## Consequences

- `ScoreDocument` is about 2150 lines, with 66 properties and 61 invokable
  methods; `CaretEditor` about 1150, with 34 and 52. An effect is added in
  one class, a file format or a layout in the other.
- QML names two objects where it named one. A component that edits at the
  caret takes both.
- The editor can be made and tested without a window: with no document, or
  before a song is open, it reads an empty beat and edits nothing.
- The five methods are public on the document for the editor's sake. They are
  the whole of what the two share: the editor cannot reach the file, the
  pages or the playback.
- A caret-dependent reader that needs only the document's measures (the time
  and key signatures, the marks, the tempo and the section of the caret's
  measure) stays on the document, next to the same reader by measure number.
