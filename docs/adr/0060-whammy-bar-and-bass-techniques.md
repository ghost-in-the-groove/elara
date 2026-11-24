# 0060. Whammy bar and bass techniques at the caret

- Status: Accepted
- Date: 2026-11-24

## Context

The beat effects edit (ADR-0056) carries the whammy bar and the bass
techniques (tapping, slap and pop), but only strums, pick strokes and tremolo
picking could be set: no CLI step or button gave a beat a dive or a slap. The
importers read both and playback bends the beat's notes along the whammy-bar
curve.

## Decision

**Whammy-bar forms are the bend forms, either way.** A whammy bar dives below
the written pitch as often as a bend goes above it, in the same shapes: a dive
(down and held), a dip (down and back), a pre-dive (pushed down before the
attack and held) and a pre-dive and return. `whammyForm(curve)` reads the
`BendForm` of a curve going down or up, `whammyCents(curve)` how far it goes
(negative below the written pitch), and `whammyCurve(form, cents)` makes it,
mirroring the bend presets for negative cents. A curve that goes both ways is
`Custom`. No new enum: a whammy bar pulled up has exactly a bend's shapes.

**One edit, as for the other beat effects.** The whammy bar and the bass
technique go through `setBeatEffects()` ("Change beat effects"), each changing
only its own field.

**CLI.** `elara-cli edit` gains `whammy <dive|dip|pre-dive|return|none>
[cents]`, a step (-200 cents) down by default and up for positive cents, and
`bass <tap|slap|pop|none>`.

**App.** The effects panel gains a Whammy bar row: the four forms drawn going
down, then the depths -1/2, -1, -1 1/2, -2 and -3 steps, used as the bend row
is. The Beat row gains T, S and P for tapping, slap and pop. The status line
names the whammy bar with its form and signed amount ("Dip -1 1/2", "Inverted
dip +1/2") and the technique.

## Consequences

- The panel only dives; a whammy bar pulled up comes from files or the CLI,
  and shows in the status line.
- The score does not draw the whammy bar or the bass techniques yet, and
  playback does not change the sound of a slap or a pop.
