# 0101. Tremolo picking on the stem

- Status: Accepted
- Date: 2026-11-25

## Context

Tremolo picking repeats a note as fast as its speed (an eighth, a sixteenth or
a thirty-second, `BeatEffects::tremoloPicking`, ADR-0025). Tablature writes it
as slashes in a lane over the staff, one per beam of the repeated notes
(ADR-0013). The standard staff borrowed that lane, but only when its staff had
no tablature under it, so a part shown as notation and tablature, the usual
guitar view, had no tremolo in its notation; and a score writes tremolo
picking through the stem, as MusicXML's single `tremolo` does.

## Decision

**The slashes cross the stem.** On the standard staff a tremolo-picked chord
carries its slashes (one, two or three, as the lane writes them) centred on
its stem, midway between the notehead nearest the tip and the tip, or the flag
or the beams at it. A note without a stem (a whole note) carries them where
its stem would be, clear of the notehead.

**The stem makes room.** A stem is lengthened when the slashes, a gap on each
side and its flag or beams would not fit; a beam group moves its beam away for
it. The chord makes room beside it for the slashes' width.

**No lane on the standard staff.** Its technique lanes leave tremolo picking
out; tablature keeps its lane.

## Consequences

- Tremolo picking reads in every view of standard notation, as scores write it.
- A tremolo-picked chord may have a longer stem than its neighbours.
- Tests: `notation.Staff layout: tremolo picking slashes cross the stem` and
  the render snapshot of `gp/techniques.gp5`.
