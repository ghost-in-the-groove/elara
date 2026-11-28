# 0180. Edits at the caret say why they are refused

- Status: Accepted
- Date: 2026-11-28

## Context

An edit at the caret (`entry.hpp`) that the song could not take answered in
one of three ways. Most threw: `std::invalid_argument` for a fret off the
neck or a technique of strings on a piano, `std::logic_error` for a drum hit
on a guitar or a tie with no note before it, `std::out_of_range` for a note
name no string can play. Some returned an empty `Entry` with
`Entry::missed` set (ADR-0123), and a few returned an empty `Entry` that
looked as if the edit were so already (`toggleEffect()` on the free slot).

Each front end then rebuilt the reason on its own. The app checked the caret
before calling (`caretFretted()`, `caretOnDrums()`), caught the exception
types around the edits it knew to throw and showed the exception's English
text to the user, untranslated; around the ones it did not (`setPluck()`),
the exception went on up. It told a missed dynamic by reading the dynamic
again after the edit. The CLI turned every exception into a malformed step
with its text. The reasons were the edits' interface, but written as
exception types and messages that no caller could test for except by
matching text.

## Decision

- **A refusal is a value.** `core::Refusal` names each reason an edit at the
  caret is refused: nothing at the caret takes it, it needs a staff with
  strings, it needs drums, drums take no note names, no string plays the
  note, the note would leave the neck, it is out of the MIDI range, there is
  no note to tie to, the fret has no natural harmonic, a trill goes more
  than an octave away. Each has a name (`EnumNames`, ADR-0006's keys).
- **The edit says it.** `Entry::refused` carries the reason, with no command
  and the caret where it was. `Entry::missed` is gone: it is
  `refused == Refusal::NothingAtCaret`. An entry with neither a command nor a
  refusal is so already. Each edit's contract in `entry.hpp` lists the
  refusals it gives.
- **Throwing is for callers' mistakes.** What a caller can check before
  calling, and a front end never sends from its own controls, still throws:
  a caret outside the song (`clampCaret()`), a finger out of range, a verse
  out of range, a measure repeat of more than two measures, a clef on a
  percussion staff.
- **The session passes it on.** `EditSession::enter()`, `typeDigit()` and
  `setInput()` return an `EntryResult`: whether the song changed (one more
  undo step) and the refusal, if any. They are `[[nodiscard]]`. A second
  digit that would make a fret off the neck leaves the first as it was typed
  and starts a fret of its own; a refused digit ends the wait for a second
  one.
- **Front ends only word it.** The app no longer checks the caret ahead of
  an edit or catches the edit's exceptions: `ScoreDocument::enter()` emits
  `editFailed()` with `refusalText()`, one translated sentence per refusal.
  The CLI goes on past an edit that nothing at the caret takes, as it did,
  now with a warning naming it, and makes any other refusal a malformed step
  reading `refused: <name>`.

## Consequences

- A test asks for the reason by value
  (`CHECK(entry.refused == Refusal::NeedsDrums)`), in core, the CLI and the
  app, instead of an exception type or a message.
- The app's messages for refused edits are translated, and a refused edit
  can no longer escape the app as an exception.
- A new reason to refuse needs its enumerator, its name and a sentence in
  `refusalText()`; the app's test checks that each refusal has words of its
  own.
- The CLI's messages for these refusals changed from the exceptions' texts
  to the refusals' names; its exit codes did not.
