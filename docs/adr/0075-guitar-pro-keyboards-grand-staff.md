# 0075. Guitar Pro 3-5 keyboards on a grand staff

- Status: Accepted
- Date: 2026-11-25

## Context

Guitar Pro 3-5 files hold every track on one staff of up to seven strings,
whatever its instrument. Elara reads a track whose MIDI program is a keyboard
(programs 0-7) as a keyboard without tablature and gives it the clef that
suits the average pitch of its notes (treble or bass). A piano part that uses
both hands then lands on one clef: written for two hands, its notes run from
the bass staff's range up into the treble's, and a single bass clef puts a
melody around C5 on five ledger lines or more, a single treble clef a left
hand around F2 on as many below. Guitar Pro 6-8 files and MusicXML give a
piano its two staves themselves, and since ADR-0073 a track with two staves
reads as a grand staff.

## Decision

**A Guitar Pro 3-5 keyboard track whose notes reach below F3 and above G4 is
read as a grand staff: a treble and a bass staff, with the notes from middle C
up on the treble staff and the others on the bass staff.**

- The test is on the track's lowest and highest notes: below F3 (MIDI 53) a
  treble staff needs four ledger lines or more, above G4 (MIDI 67) a bass
  staff does. A keyboard within one of those ranges keeps one staff and the
  clef that suits it, as before.
- Each beat is split by pitch at middle C (MIDI 60), each half keeping the
  beat's duration (tuplets included), so both staves stay aligned. A half
  with notes keeps the beat's effects (strum, pick stroke, tremolo picking,
  whammy bar); a half without notes is a plain rest.
- A voice with no notes on a staff in a measure stays empty there rather than
  filling the measure with rests. The voices a staff never has notes in are
  dropped and the others move down from the first, the same way in every
  measure, so a note tied across a barline stays in its voice.
- Middle C itself goes to the treble staff, as engravers usually write it
  when the two hands share no beat.

## Consequences

- Piano parts written in Guitar Pro 3-5 read like those of Guitar Pro 6-8 and
  MusicXML: a brace, two clefs, few ledger lines. The split is fixed at middle
  C; a hand crossing it shows on the other staff, which a reader can move
  back by hand once editing notes between staves exists.
- A chord spread over both hands has its strum arrow on both staves, not one
  arpeggio line through both.
- Other instruments of more than one staff in Guitar Pro 3-5 (organs, harps)
  keep one staff; the test only applies to MIDI keyboards.
