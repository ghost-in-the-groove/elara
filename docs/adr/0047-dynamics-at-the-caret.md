# 0047. Dynamics at the caret

- Status: Accepted
- Date: 2026-11-23

## Context

Every note carries a dynamic (`NoteEffects::dynamic`, mf by default): playback
turns it into a velocity, and the score writes a mark where the loudness
changes. Keyboard entry (ADR-0021) could type notes and toggle their effects
(ADR-0037), but not set a dynamic, and every new note was mf whatever came
before it.

Tablature editors store a dynamic per note and set it on the selected beats.
Notation editors write a mark that holds until the next one. A player who puts
"p" on a beat in the middle of an mf passage means the passage from there to be
soft, not one beat; one who types on after a forte passage expects forte notes.

## Decision

**A dynamic at the caret holds until the next change.** `setDynamic(song,
caret, dynamic)` finds the passage from the caret's beat: from the first start,
from the beat's on, with struck notes (on a rest or the free slot, the notes
after it), while the notes struck at each later start have the loudest dynamic
of that first start. It looks at every voice of the staff, as the marks do, and
runs through later measures. Every note struck in the passage takes the new
dynamic, as one undo step ("Change dynamic"); the caret stays. On the beat where
a mark is written, that changes the whole passage it governs; inside a passage
it starts a new one there.

**Tied notes follow the note they continue**, the same pitch in the voice's
beat before, even past the end of the passage; a tie continuing a note struck
before the caret keeps its dynamic.

**Nothing changes** when no note follows the caret or the passage already has
the dynamic, so no empty undo step is recorded.

**New notes take the dynamic in force.** A fret, a note name or a drum hit
typed at the caret takes `dynamicAt(song, caret)`: the dynamic struck at the
latest start no later than the caret's beat, over every voice and earlier
measures (the loudest of a chord), mf before the first note. A note typed over
another on its string keeps that note's effects, dynamic included, as before.

**Headless first (ADR-0003).** The CLI's `edit` step `dynamic <ppp..fff>` sets
it at the caret; `dump` shows each note's dynamic.

## Consequences

- The app can offer dynamics on the keyboard and in a palette over this call.
- Accents and ghost notes stay separate effects: a dynamic does not touch them.
- A passage is found from the notes as they are; notes written with a different
  dynamic in the middle of a chord count by the chord's loudest, as the marks do.
