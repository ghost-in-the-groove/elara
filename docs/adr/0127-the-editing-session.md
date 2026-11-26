# 0127. The editing session lives in core

- Status: Accepted
- Date: 2026-11-26

## Context

The edits of keyboard entry are in core: each takes a song and a caret and
returns a command and the caret after it (ADR-0021). What entry keeps from one
key to the next was not. The app's `ScoreDocument` held the caret, the
duration typing uses, the digit waiting for a second one, the selection and
the clipboard among its files, pages and playback, about 480 lines of some
3450, and wrote their rules where each key landed: twenty places ended the
wait for a second digit, and the caret was moved by hand before or after an
edit in five. The CLI kept a caret, a duration and a clipboard of its own for
its `edit` steps (ADR-0021).

None of it could be tested without a `QGuiApplication`, the fonts and the
document's workers, and until the app tests built `ScoreDocument` none of it
was tested at all. The first tests through the document found a selection
that outlived its song.

## Decision

- `core::EditSession` holds what entry keeps between actions and how each
  action moves it. It holds no song: an action takes the song, or the
  `Document` it edits, and leaves the session's state on places that song
  has. It is plain C++, tested in `tests/core`.
- It starts with the caret and the input duration:
  - `move()`, `setVoice()` and `place()` move the caret, and the input
    duration follows the beat it lands on when typing offers that duration
    (`kInputValues`, `kInputTuplets`, at most one dot).
  - `enter()` is how every edit at the caret goes in: the caret goes where the
    `Entry` says, its command is executed, and the caret is back inside the
    song whatever happened. An entry's caret may be of the song after the
    edit, as for a measure inserted at the end.
  - `setInput()` changes the duration typing uses and gives it to the caret's
    beat.
  - `showTrack()` names the track shown alone, which the caret stays on.
- It has the digit waiting for a second one (ADR-0032). `typeDigit()` takes
  the time, so a test types the second digit as late as it wants without
  waiting. Every other action of the session ends the wait, and so does
  `songChanged()`, which the document calls for the edits, undos and redos it
  makes itself. An edit at the caret that may turn out to change nothing and
  never be entered ends it with `endTyping()`.
- `ScoreDocument` has a session and delegates to it. It keeps what is Qt's:
  the properties and invokables QML binds to, the signals, the caret's box on
  the pages, the translated messages. QML does not change.
- The selection and the clipboard move to the session next, with their
  tests; the CLI's steps can then use the same session instead of their own
  state.

## Consequences

- A rule of entry is written once and tested without Qt.
- `ScoreDocument` gets smaller by what moves, not by what binds: the readers
  and setters QML calls at the caret, about 1000 lines, stay until the class
  QML sees is itself divided, which is a change to QML and a decision of its
  own.
- The session and the document it is used with must be told the same things:
  the document tells the session the track shown whenever it changes and
  every change of the song it makes itself, and the session is the only one
  to move the caret.
- Ten places of the document end the wait for a second digit where twenty
  did: the edits at the caret that are made before they are entered. The
  rest are the session's own actions.
- `hasCaret()` is core's, where the app and the CLI each had a copy.
