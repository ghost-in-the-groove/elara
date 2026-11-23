# 0035. Drum entry

- Status: Accepted
- Date: 2026-11-23

## Context

Drum notes are General MIDI percussion keys placed on the staff by a drum map
(docs/instruments.md). Keyboard entry types frets on strings (ADR-0021) and note
names on pitched staves (ADR-0034); neither fits a drum kit, where a beat is a
set of hits on kit pieces (bass drum and hi-hat together, a snare on the next
beat). Drum tablature and drum editors in other programs show one lane per kit
piece and toggle a hit where a lane crosses a beat.

## Decision

**Lanes are the drum caret's strings.** On a percussion staff the caret's
`string` is a lane, one per kit piece of `drumPieces()`: crash, closed and open
hi-hat, ride and ride bell, high and mid tom, snare, side stick, floor tom, bass
drum and hi-hat pedal, from the top of the staff down as the default drum map
places them. Up and Down move between lanes with the same caret moves as
strings, and clamping keeps the caret on a lane.

**A hit is toggled.** `toggleDrum(song, caret, duration)` adds the lane's piece
to the caret's beat, keeping the hits in lane order, or removes it when it is
there (the last hit leaves a rest). On a rest the hit takes the rest's
duration; on the free slot it starts a beat of the input duration. The caret
stays, so a groove is typed by moving along the lanes and beats. Delete removes
the lane's piece only.

**The kit list is core data.** `drumPieces()` lives with the instrument presets
in the core, as General MIDI keys and English names; the notation module
already places those keys, and the app translates the names.

`elara-cli edit` gains `drum` (the caret's lane) and `drum <key>` (that piece's
lane, which the caret moves to).

## Consequences

- Every hit is in the caret's voice. Hands and feet in separate voices (the
  drum map's default) and a per-track drum map come with the drum map editor.
- Percussion keys outside the lanes (cowbell, congas) are kept, shown and
  played, but can only be removed with the whole beat until the lanes can be
  configured.
- The app draws the drum caret on the lane's staff position and maps a key to
  `toggleDrum` (a later change on top of the caret in the app).
