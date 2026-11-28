# 0176. Nothing runs past the right margin

- Status: Accepted
- Date: 2026-11-28

## Context

Two things could run past the right margin of a page, into the margin or off
the paper, where the window and a PDF cut them:

- **A measure wider than the page.** The layout never squeezes a measure below
  what its ink needs, so a measure too wide for the page between its margins
  got a system of its own that ran past the margin (ADR-0150 reports it). A
  keyboard part with a 7/4 measure of forty-two sixteenth-note triplets,
  notation over tablature with two-digit frets, needs 122 staff spaces where
  an A4 page has 103 at 1.75 mm: its first two measures were cut at the edge
  of the page, and nothing a user could change in the layout brought them
  back.
- **Words above the staff.** A section's name starts at its measure, a beat's
  text at its beat, a tempo mark at the beat where the tempo changes, and each
  ran as far right as its words are long. In the last measure of a system a
  name such as "Guitar Solo 1 reprise" ended beyond the page.

ADR-0171 made the staff space follow the staves shown so that a system fits a
page's height, and said that it depends on the staves alone so that editing
never changes it. A measure that does not fit the page's width is the same
problem across.

## Decision

- **A view whose widest measure does not fit the page gets smaller staff
  spaces.** The page is as many millimetres wide, so it is wider in smaller
  staff spaces, and the measure needs as many as before:
  `notation::widthOverflow()` says how far the widest system of laid-out pages
  runs past the margin, and `narrowedStaffSpace()` gives the staff space at
  which it ends inside it, rounded down to a hundredth of a millimetre.
- **Never below 60 % of a part's staff space** (`kLeastStaffShare`, as for the
  staves): a measure wider still stays past the margin and is reported as
  before. Staves already smaller than that, as every track at once has them,
  are left alone.
- **The app lays out, looks, and lays out again when it must.** A view
  remembers the staff space it was narrowed to and the measure that needed it
  (`LayoutMemory`), and keeps that size while the measure is as it was, so an
  edit elsewhere lays out only what it changed (ADR-0052). An edit of that
  measure starts again from the staff space of the staves. The PDF export
  does the same, and so does `render --fit-staves`.
- **Words above the staff end with their system.** A section's box, a beat's
  text and a tempo mark that would run past the end of the system start early
  enough to end with it, as far back as the system's start allows; a tempo
  mark with a line (accel., rit.) ends where its line does. One that fits
  stays where it was.

## Consequences

- No measure of a part is cut unless it needs more than five thirds of the
  page's width, and that one is reported.
- The staff space of a view now depends on its widest measure too, when that
  measure is wider than the page: editing that measure can change the size of
  every staff in the view. Editing anything else cannot.
- A view with such a measure costs two layouts the first time, and when the
  measure is edited.
- A name moved back can stand over the measure before its own; its box still
  ends at its measure's system, and names that would overlap stack as before.
