# 0055. Bends of any form at the caret

- Status: Accepted
- Date: 2026-11-24

## Context

The model holds a bend as a curve of points in cents (milestone 1), the
importers read curves of any shape, playback and tablature follow them, but
the entry toggle (ADR-0037) only puts on a full-step bend. A guitarist writes
half-step bends, bends that are released, and pre-bends far more often than
curves of their own, and had no way to enter them from the CLI or the app.

## Decision

**Four usual forms and how far they go.** `core::BendForm` names the forms
tablature writes: a bend (up and held), a bend and release, a pre-bend (bent
before the attack and held) and a pre-bend and release. `bendCurve(form,
cents)` gives the curve of a form, from the presets `BendCurve` already had,
going up by 1 to 2400 cents.

**The form is read from the shape.** `bendForm(curve)` looks at the curve's
direction rather than its points: it rises from 0 and holds, rises and falls
back to 0, starts at its top and holds, or starts at its top and falls to 0.
Curves read from Guitar Pro or MusicXML keep their own points and are still
recognised. Anything else (a bend, release and bend again, a dip below the
written pitch) is `Custom`: shown and kept, never rewritten.

**One edit sets any bend.** `setBend(song, caret, curve)` sets the curve on the
notes the caret is on, the same notes as the toggles, as one undo step
("Change bend"); an empty curve takes it off. It changes nothing when they all
have that curve, so choosing the bend the note has adds no undo step.
`bendAtCaret()` reads the bend of the first of those notes.

**CLI.** `elara-cli edit` gains `bend <form> [cents]`, with the forms `none`,
`bend`, `release`, `pre-bend` and `pre-release` and a full step (200 cents) by
default. `effect bend` keeps toggling a full-step bend.

**App.** The effects panel of the entry bar gains a Bend row: four buttons
drawn as the arrows tablature uses for the forms, then the amounts 1/4, 1/2,
full, 1 1/2 and 2 steps. A form takes the amount of the caret's bend (or the
last amount chosen), a click on the lit form takes the bend off, and an amount
keeps the caret's form (a bend when it has none). The status line names the
bend with its form and amount ("Bend and release 1/2") instead of just "Bend".

## Consequences

- Curves of their own still need an editor with points; the panel shows no
  form lit for them, and choosing a form replaces the curve.
- Whammy-bar dips and other beat effects (strokes, tremolo picking) still have
  no editor in the app.
- The amounts follow the quarter steps tablature prints; other amounts in cents
  come only from files or the CLI, and the status line says them in cents.
