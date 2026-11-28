# 0175. A view shown before comes back as it was

- Status: Accepted
- Date: 2026-11-28

## Context

The layout worker kept one memory (ADR-0052, ADR-0150): the measures it had
measured, the systems it had laid out and the pages with their layout issues,
all for the staves shown last. Showing another track, another view of it
(notation, tablature, both) or the continuous view started that memory again,
and so did coming back: every change of track laid the whole song out from
nothing, though the song had not changed.

On a song of 480 measures and sixteen tracks, laying out one track takes about
30 ms in the optimised build and close to a second in the debug build that is
run from the sources; every track at once, several times that. Picking a
track in the track picker to read what it plays against another, and going
back, paid it each way.

## Decision

- **The worker keeps a memory per view**: a track or every track, as notation,
  tablature or both, on pages or as one continuous page (`LayoutMemories`).
  A layout goes through the memory of the view it is for.
- **A view asked for with the song it last laid out gives its pages back**,
  the same objects with the same issues, with nothing measured, broken into
  lines or compared: the song is the same object (`ScoreDocument` makes a new
  one at each edit), and a view's pages follow from the song alone.
  `ScoreDocument::layoutsKept()` counts these.
- **A view kept while the song was edited lays out what changed**: its memory
  starts from the song it knew, as an edit's layout does (ADR-0052).
- **Six views are kept**, the ones shown last; the one used longest ago is let
  go when a seventh is shown. A song opened or made anew starts with none.

## Consequences

- Going back to a track read a moment ago costs the painting of its pages,
  not a layout. The first time a view is shown costs what it did.
- Up to six layouts stay in memory where one did, and each holds the song as
  it was when it was last shown. Versions of a song share the measures that
  did not change (ADR-0129), so that costs the pages, not six songs: about
  2 MiB a view of one track of the song above, more for every track at once.
- The pages of a view kept are painted again when it is shown: the page items
  are made anew when the number of pages changes. Keeping the textures too
  would cost video memory for views that may never come back.
