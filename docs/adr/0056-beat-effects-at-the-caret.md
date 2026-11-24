# 0056. Beat effects at the caret

- Status: Accepted
- Date: 2026-11-24

## Context

A beat can be strummed down or up, picked with a down or up stroke and
tremolo-picked. The importers read these effects, playback plays strums and
tremolo picking (ADR-0025) and the score draws them (ADR-0013), but nothing
could set them: a strummed chord typed in the app stayed a block chord.
Unlike the note effects (ADR-0037), they belong to the beat, not to the note
on one string.

## Decision

**One edit sets the beat's effects.** `setBeatEffects(song, caret, effects)`
gives the caret's beat a whole `BeatEffects` (strum, pick stroke, tremolo
picking, whammy bar, bass technique) as one undo step ("Change beat
effects"); `beatEffectsAt()` reads them. Callers change one field and set the
rest as they were, so one edit serves every beat effect, present and future.
Nothing changes when the beat has those effects already, on a rest (no notes
to strike) or on the free slot.

**CLI.** `elara-cli edit` gains `strum <down|up|none>`, `pick <down|up|none>`
and `tremolo <8|16|32|none>`, each changing only its own effect on the caret's
beat.

**App.** The effects panel gains a Beat row: strum down and up drawn as the
arrows the score uses, the down-bow and up-bow signs for pick strokes, and
tremolo picking in eighths, sixteenths or thirty-seconds drawn as one, two or
three slashes. A button is lit while the caret's beat has its effect, and a
click on a lit one takes it off. The status line names them after the note
effects ("Strum down", "Up stroke", "Tremolo picking 1/16").

## Consequences

- Whammy-bar curves and bass techniques (tap, slap, pop) go through the same
  edit, but still have no buttons in the app or steps in the CLI.
- Strums and tremolo picking typed in the app play as written (ADR-0025)
  without other changes.
