# 0171. The staff size follows the staves shown

- Status: Accepted
- Date: 2026-11-28

## Context

The app laid out every view on A4 pages with staff spaces 1.75 mm apart, the
size of a part. A guitar track as notation over tablature is a system about
16 staff spaces high, and a page holds several. Every track at once of a band
is not: six guitars as notation over tablature make a system of about 140
staff spaces, so at 1.75 mm one system nearly filled a page, the first page
(with the title and credits) held one with room to spare, and reading the
song meant turning a page for every system, each zoomed to fit with the
staves as small on screen as on a full score anyway.

Printed full scores solve this by using a smaller staff size the more staves
a system has.

## Decision

- **`notation::fittedStaffSpace()`** gives the staff space for the staves
  shown: the largest, up to the one asked for, that leaves room for two
  systems a page. It estimates a system's height from the staves alone
  (`typicalSystemHeight()`: four staff spaces a staff of five lines, one and
  a half between tablature's string lines, five between staves, eight for the
  ink above and below), not from the notes, so that editing music never
  changes it and the layout cache keeps what it has; adding or showing a
  track does.
- **It never goes below 60 % of the staff space asked for**, where notes get
  too small to read (`kLeastStaffShare`): with more staves than that allows,
  a page holds one system, smaller still only when even one would not fit,
  and never below 0.5 mm (`kLeastStaffSpace`). It is rounded down to
  hundredths of a millimetre.
- **The app lays out what it shows at that staff space**: one track keeps
  1.75 mm; every track at once gets a smaller one. The page stays A4 on paper,
  so it is wider and taller in staff spaces, and the zoom (which counts in
  millimetres) shows it the size it was: at 100 % the page is as large on
  screen as before, its staves smaller. `ScoreDocument::staffSpace` says the
  size of the pages shown; a PDF export of the same view is laid out the same.
- **The command line does it on request**: `render --fit-staves` lays out at
  the fitted staff space and reports it (`"staffSpace"`).

## Consequences

- Every track at once reads as a full score: two systems or more a page where
  one stood, the whole page in view when zoomed to fit, zoom to read details.
- The estimate errs on the high side (laid-out systems of empty measures are
  10 to 20 % lower), so pages sometimes hold more systems than two.
- A staff size chosen for a song can become what the fit starts from, in
  place of 1.75 mm, without changing the rule.
