# 0174. The staves' lines are dimmer on the dark page

- Status: Accepted
- Date: 2026-11-28

## Context

On the dark page (ADR-0114) everything was painted in one ink, twelve to one
against the paper: the notes, the fret numbers, and the five or six lines
under them. Black on white, a staff's thin lines recede behind the heads that
sit on them. Light on dark they do not: a thin light line looks heavier than
the same line in black, the lines of notation and tablature together are most
of the ink on the page, and they came out as bright as the notes. The page
read as a grid with marks on it, and a fret number, light on a light line,
was told from its string only by the gap cut for it.

## Decision

- **`ScoreColours` has a third colour, `staff`, for the lines of the staves**
  of notation and of tablature (`Role::StaffLine`). Unset, they take the ink,
  so print, PDF and the white page paint as before.
- **On the dark page the lines are `Theme.nightStaff` (#8c8a96)**, four and a
  half to one against the paper where the ink is twelve to one: plain to see,
  and behind what they carry. `ScorePage` has a `staff` beside its `paper` and
  `ink`.
- **Everything else stays in the ink**: barlines, ledger lines, stems, beams,
  clefs, numbers and text. A ledger line belongs to its note, and barlines
  are few and say where the measures are.

## Consequences

- Notes and fret numbers stand out from the staff on the dark page as they do
  on paper; the light theme and whatever leaves the program are unchanged.
- Painting costs what it did: the pen changes colour between lines.
- A colour inside a score (none is drawn yet) still has to say what it becomes
  on the dark page, as ADR-0114 noted.
