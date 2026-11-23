# 0045. Played MIDI velocities

- Status: Accepted
- Date: 2026-11-23

## Context

The MIDI importer (ADR-0009) gave each note the dynamic nearest its velocity.
Files written from notation (by Elara or another score editor) use one
velocity per dynamic, and for them that is exact. Played music and drum
programming are different: every note has its own velocity, so neighbouring
notes land on different dynamics. Played back that is close enough, but as
notation it is noise: a played melody alternates mp and mf from note to note,
and a drum beat alternates fff (kick), mp (hi-hat) and ff (snare), which the
dynamics marks (ADR-0044) would write on almost every beat.

## Decision

**Written or played, per staff.** A staff whose velocities all lie within 2
of a dynamic's velocity (16, 32 ... 112, 127) was written from dynamics, and
each note keeps its nearest dynamic as before. Any other staff was played.
The margin also takes in score editors that round differently (33 for pp,
126 for fff).

**A dynamic per measure.** A played staff gives each measure with notes the
dynamic nearest the median velocity of the notes struck in it. A measure
keeps the dynamic of the measure before when that dynamic's velocity is
within 12 of the median, so a level that wavers does not flip between two
dynamics.

**Accents and ghost notes.** A played note at least 20 above its measure's
dynamic is accented, at least 36 above it is a marcato, and at least 28 below
it is a ghost note. Playback adds 16 for an accent and 32 for a marcato and
takes 32 off a ghost note (ADR-0022), so these come close to the played
velocity; smaller differences are left out to keep the notation readable. A
note held over a barline keeps the loudness it was struck with, and only its
first part carries the accent or ghost.

## Consequences

- Imported drum beats read like drum notation: the corpus beat is f
  throughout, with its crash accented and its pedal hi-hat as ghost notes.
  Played melodies keep one dynamic until the level really changes.
- Playback of played files is less exact: a note within the margins plays at
  its measure's dynamic, up to 20 velocity steps from the file (28 below).
  The notes and their timing are unchanged.
- Files written from dynamics, including Elara's own, still import note for
  note, so the round-trip tests of ADR-0009 hold.
- Tests: `io.MIDI import keeps velocities written from dynamics note by
  note`, `io.MIDI import reads played velocities as a dynamic per measure,
  accents and ghost notes`, `io.MIDI import gives a note held over a barline
  the loudness it was struck with`; the import snapshots of the corpus drum
  and played-piano files.
