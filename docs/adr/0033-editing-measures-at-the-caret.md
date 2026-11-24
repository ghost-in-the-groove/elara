# 0033. Editing measures at the caret

- Status: Accepted
- Date: 2026-11-23

## Context

The song panel (ADR-0031) edits the time and key signatures of the first
measure and the song's length by adding or removing measures at the end. A real
song changes meter and key in the middle, and measures are inserted or deleted
where the music needs them. The core already has the edits: a time or key
signature change from a measure until the next change, and measures inserted or
removed at any index in every track, each an undoable command. The app has a
caret on the score (ADR-0032) that says which measure the user works on.

## Decision

**The caret's measure is the one edited.** `ScoreDocument` exposes the caret's
measure and its time and key signatures, and edits them through
`setTimeSignatureFrom()` and `setKeySignatureFrom()`, which run the core's
changes from that measure until the next one. The song panel's first-measure
setters are the same calls at measure 0.

**A measure popup next to the caret.** Ctrl+M, the measure button of the entry
bar, or a right click on the score (which first places the caret there) opens a
small card near the caret: the measure's time signature, key signature and
mode, and buttons to insert an empty measure before or after it or to delete
it. The signature fields are one `SignatureFields` component shared with the
song panel, so both edit signatures the same way. The card closes on Escape or
a click outside and gives the keyboard back to the score.

**Keys.** Ctrl+Insert inserts a measure before the caret's; Ctrl+Shift+Delete
deletes it (never the last measure left). Inserting moves the caret to the new
measure, ready to type into it. Every edit is one undo step.

## Consequences

- Signature changes are visible where they apply: the popup shows the values
  in force at the caret, and the score redraws the new signature at the measure.
- Measure edits are whole relayouts, like every other edit (ADR-0031).
- Other per-measure properties (repeats, alternate endings, tempo changes,
  section markers) have a place to go: more rows in the same card.
- `--measure` opens the card at start and `--smoke-test` inserts a measure
  after the caret, changes its signatures, checks the first measure kept its
  own, deletes it and undoes everything (`app.smoke_measure` runs it with the
  card open).
