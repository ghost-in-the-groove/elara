# 0044. Dynamics marks

- Status: Accepted
- Date: 2026-11-23

## Context

Every note carries a dynamic (ppp to fff, mf by default), which the Guitar Pro,
MusicXML and MIDI importers fill and playback turns into velocity (ADR-0009),
but the score never showed it. Printed music writes a dynamics mark under the
staff where the loudness changes and lets it hold until the next one.

## Decision

**When.** Notes struck together (one column, every voice) sound at the
loudest of their dynamics; tied notes only sustain and rests change nothing.
A mark is written under a column whose dynamic differs from the one in force:
mf before the first note of the song, so a song played at mf throughout shows
no mark. A line starts with the dynamic of the last notes struck before it, so
a mark is never repeated at the start of a system.

**Where.** Under the standard notation of the staff, or under its tablature
when the system shows the staff only as tablature (under the rhythm written
there, ADR-0041). All marks of a line share one baseline: at least 2.8 spaces
under the bottom line and 0.5 spaces clear of the ink above. Each mark is
centred under its notes by its SMuFL `opticalCenter` anchor, moved right to
keep 0.4 spaces from the mark before and then left to stay on the line; when
a line has no room for its marks at all, they overlap rather than leave it.

**What.** The font's composed marks (`dynamicPPP` ... `dynamicFFF`, with
`dynamicPiano`, `dynamicMP`, `dynamicMF`, `dynamicForte`), which carry their
own kerning. They carry a new `Role::Dynamic` ("dynamic").

## Consequences

- Songs with authored dynamics (Guitar Pro, MusicXML) show them where the
  author put them; the render snapshots of the corpus change accordingly.
- Files whose dynamics come from velocities (MIDI) show a mark wherever the
  velocity crosses to another dynamic, which is noisy for drum kits and played
  performances; the MIDI importer should read velocities into steadier
  dynamics. That is a change to the importer, not to this rule.
- Hairpins (crescendo, diminuendo), sforzando and dynamics on beats rather
  than notes are not in the model and are not drawn.
- Tests: `notation.Dynamics: ...` (a mark where the dynamic changes and none
  at mf, every mark, no overlaps and no marks off the line, rests, ties and
  chords, the dynamic in force at the start of a line, clear of the ink under
  the staff, under standard notation or tablature shown alone).
