# 0063. Section marks in the score

- Status: Accepted
- Date: 2026-11-24

## Context

Songs name their sections at the measure where each starts (ADR-0062), but
the score did not show them, so a reader could not see where the chorus
starts.

## Decision

**The section's name in a box at the start of its measure, above everything
else over the first staff of the system**: above the tempo marks, the signs
and jumps (ADR-0049) and the alternate endings, as rehearsal marks are
printed.

- The box starts just after the barline before the measure (at the start of
  the system for its first measure), with the name upright inside it.
- The boxes of one system stand on one line; a box that would overlap one
  already on the line goes on the first line above where it fits.
- The name and its box take a role of their own, `Role::Section` ("section"
  in JSON and SVG), so viewers can style or hide them.

## Consequences

- A section reads at a glance where it starts, in every view that draws the
  first staff's marks: pages, the continuous view and `elara-cli render`.
- A system with sections is taller by one line of boxes; systems without them
  are unchanged.
- The name is written as given; a long name makes a wide box.
