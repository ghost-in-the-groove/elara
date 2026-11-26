# 0123. Measure marks and effects are edited through core

- Status: Accepted
- Date: 2026-11-26

## Context

The signs and jumps (ADR-0049), repeats and alternate endings (ADR-0016) and
the double barline (ADR-0103) of a measure had no edit of their own: the CLI
and the app each copied the header, changed fields and executed a
`SetMeasureHeader`, which names the step after what changed. Each front end
kept its own rules, and they had drifted. The CLI refused a repeat played
other than 2 to 255 times and endings other than 1 to 8 while the app
silently ignored them, and the rule that a measure without a jump has nothing
ending the music after it (`jumpThen` back to the end) lived in both, written
differently. The list of the seven sign flags was written out in the command,
the native format, the CLI report and the app.

The effects had the same shape. To change one beat effect, callers read the
beat's effects, changed a field and wrote them all back; to change a note's
slide in, they read the first note's slide out and wrote both onto every note
the caret is on, so a chord's notes lost the slides they did not share,
against ADR-0058. And an effect edit that changed nothing returned the same
empty `Entry` whether the notes had the effect already or there was no note
at the caret: the app read the song again afterwards to tell, and mapped the
exception types of every effect edit to the harmonic's messages.

## Decision

- **A group of marks is one core edit.** `changeNavigation()` (the signs and
  the jump, a `NavigationMarks`), `changeRepeats()` (the start and end repeat
  barlines and the endings, a `RepeatMarks`) and `changeBarline()` take the
  whole group for one measure, check it and return one undo step named as
  `SetMeasureHeader` names a change of that group alone: "Change navigation
  marks", "Change repeats", "Change barline". `navigationOf()` and
  `repeatsOf()` read a header's groups back. A repeat plays 2 to 255 times
  (`checkRepeatTimes()`, 0 for no end repeat) and an ending names a time from
  1 to 8 (`endingBit()`); anything else is `std::invalid_argument`. Without a
  jump, `jumpThen` is taken as the end.
- **Front ends parse and bind.** The CLI's `sign`, `jump`, `repeat`, `ending`
  and `barline` steps read the measure's group, put in what the step names and
  call the edit; core's refusal is a malformed step with core's reason, as the
  step reported it before. The CLI's grammar stays: a `jump` step names the
  whole jump, so without `al-...` the music plays to the end. The app's measure
  popup changes one field of a group at a time (a picker for the jump, another
  for how it ends) and calls the same edits; what core refuses it now says
  through `editFailed()` instead of ignoring.
- **The signs are listed once.** `kSignFlags` in `song.hpp` gives each
  `NavigationSign` its flag (`signFlag()`) and the flag's name (`signField()`),
  which the native format and the app's popup use as the sign's key
  (ADR-0006: keys use the model's names).
- **One effect, one field.** `setBeatEffect()` sets one field of the caret
  beat's `BeatEffects`, the others kept; `setSlideIn()` and `setSlideOut()`
  set one slide of each note, each keeping its other one.
- **Why nothing changed.** `Entry::missed` is true when an effect edit changes
  nothing because nothing at the caret takes it (no note there, the free slot,
  or a rest asked for what only notes take), and false when it is so already.
  The app says "Effects go on notes" from it, and maps exceptions to messages
  only around the edit that documents them (`setHarmonic()`).

## Consequences

- A rule about marks is written once, in core, and tested there
  (`tests/core/test_measure_marks.cpp`); the CLI's messages and exit codes are
  unchanged.
- The app refuses repeat counts and endings it used to drop silently; its own
  controls never send them.
- Setting a slide on a chord no longer copies one note's other slide onto the
  rest.
- Asking for "none" where there is no note now reports that there is nothing
  to change, as asking for anything else did.
- A new header field that belongs to one of these groups must be added to its
  marks type and its edit; a new navigation sign needs only its enumerator,
  name and `kSignFlags` entry.
