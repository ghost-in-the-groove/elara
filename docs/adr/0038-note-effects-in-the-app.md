# 0038. Note effects in the app

- Status: Accepted
- Date: 2026-11-23

## Context

The core toggles twelve note effects at the caret (ADR-0037) and the CLI
exposes them, but the app had no way to reach them, so a tablature typed in the
app stayed plain. Typing should not stop for a dialog: an effect needs a key,
and a newcomer needs to find the effects without knowing the keys. Letters A to
G already type note names (ADR-0034) and digits type frets (ADR-0032).

## Decision

**One key per effect, the letter of its name where it is free.**

| Key | Effect | Key | Effect |
|---|---|---|---|
| T | tie | X | dead note |
| H | hammer-on or pull-off | L | let ring |
| S | slide | N | natural harmonic |
| U | bend (B is a note name) | O | ghost note |
| V | vibrato | `>` | accent |
| P | palm mute | `'` | staccato |

Keys match the typed character, not the physical key, in either case, so `>`
and `'` work on any keyboard layout that can type them. Ctrl and Alt combinations
keep their own meanings. The table lives in `ScoreDocument` (`effectForKey`,
`effectKey`, `effectName`), so the keys, the panel and the status line cannot
drift apart.

**A panel in the entry bar.** An effects button opens a panel above the bar
with one switch per effect, its name and its key on a keycap. A switch is lit
when every note the caret is on has the effect (`core::hasEffectAtCaret`), and
a click toggles it as the key does. The panel stays open, so several effects can
be set in a row, and never takes the keyboard from the score. A dot on the
button shows that the caret's note has an effect while the panel is closed.

**The status line names the effects** of the caret's note after its pitch, so a
screen reader and the smoke test hear what the page shows.

**Refusals are toasts**, translated: no note to tie to, a harmonic without
strings, a fret without a natural harmonic, and an effect on a rest or an empty
string.

## Consequences

- The free letters left for later keys are I, J, K, M, Q, R, W, Y and Z.
- Other forms of an effect (bend curves, wide vibrato, artificial harmonics)
  still need an effect editor; the panel is where it would open from.
- The smoke test types a note, toggles accent and staccato and the accent off
  again, checks the effects, the undo name and the status line, checks the key
  table, and undoes back to the start.
