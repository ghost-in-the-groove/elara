# 0036. Selecting measures in the app

- Status: Accepted
- Date: 2026-11-23

## Context

The core copies, pastes (ADR-0027) and transposes (ADR-0026) whole measures,
and the CLI exposes those edits as steps. The app had a caret (ADR-0032) but no
way to say which measures an edit is for, so none of those edits reached the
score. Editors of tablature and scores select a range with Shift and the arrow
keys and tint it on the page.

## Decision

**A selection is a range of measures, from an anchor to the caret.** Shift with
Right or Left starts a selection at the caret's measure (its anchor) and moves
the caret a measure on or back; the selection is every measure from the anchor
to the caret's, so the caret stays where typing goes. Ctrl+A selects every
measure, with the caret on the first. Escape, a plain arrow key or a click lets
go of it. Edits keep it inside the song. The selection belongs to the app
(`ScoreDocument`), not to the song or its history: it is not saved and undo does
not bring it back.

**Measures, not beats.** The edits the core makes over a range work on whole
measures, so a selection of beats would have nothing to act on yet. A beat
range can come later on top of the same anchor and caret.

**The edits act on the selection, or else on the caret's measure.**

- Ctrl+C copies the selected measures of the shown track, or of every track
  when all are shown, into the app's clipboard (`core::MeasureClip`).
- Ctrl+V pastes them over the measures from the first selected one, or from the
  caret's; Ctrl+Shift+V inserts them as new measures there. One copied track
  goes to the caret's track; several go to the tracks they came from. The
  pasted measures end up selected.
- Alt+Up/Down transposes the selected measures a semitone (with Ctrl an
  octave) on the shown track, or on every track when all are shown; without a
  selection the same keys move the caret's note (ADR-0034).

**On the page** each selected measure is tinted across its system, with a short
fade, under the caret. The status line starts with the selected range.

## Consequences

- The clipboard lives in the app process; copying between windows or programs
  needs a clipboard format, which this does not define.
- Cut and clearing measures need a core command that empties measures; until
  then Delete still works on the caret's note or beat only.
- The smoke test selects two measures, checks they are tinted and named in the
  status line, copies them, pastes them before the caret's measure, checks the
  pasted measures are selected, transposes them and undoes back to the start.
