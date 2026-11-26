# 0114. The page in the dark theme

- Status: Accepted
- Date: 2026-11-26

## Context

In the dark theme the page stayed white, with a grey veil over it so that it
would not glare on the dark desk. The veil made the paper look dirty and the
ink less sharp, and the page was still the brightest large thing in a dim
room, which is where a player practises in the evening. The programs our users
come from are faulted for the same: Guitar Pro has no dark page at all, and
its users ask for one; MuseScore can invert the score; the tablature sites
they read from show light notes on a dark page.

The page is painted by `ScorePainter` into an image, always in black on white;
what clears the staff lines behind a fret number was a white box.

## Decision

**In the dark theme the page is dark and its ink light.** `ScorePainter` takes
the colours it paints in (`ScoreColours`: a paper and an ink), black on white
unless told otherwise; the boxes that clear the lines behind a text take the
paper. `ScorePage` has a `paper` and an `ink` and paints again when they
change, and a page whose colours did not change keeps its texture as before.

- The dark page is `Theme.nightPage` (#27262e), lighter than the desk, the bars
  and the fields so that it stays the brightest surface, with a hairline round
  it instead of a shadow; its ink is `Theme.nightInk` (#e7e5ec), twelve to one
  against it.
- View > "Dark page in the dark theme" turns it off: the page is then white
  under its veil, as before. The light theme always shows black on white.
- What leaves the program is never dark: PDF, SVG and print stay black on
  white, whatever the window shows.

## Consequences

- Painting costs what it did; switching the theme or the choice paints the
  pages in sight once more.
- The choice is not kept from one run to the next yet; nothing of the window
  is (its size, its panels, its zoom). That is one task for all of them.
- The caret, the selection and the playback cursor are drawn over the page in
  the accent and show on both papers.
- A colour inside a score (none is drawn yet) will need to say what it becomes
  on the dark page.
