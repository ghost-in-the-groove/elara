# 0057. Continuous view

- Status: Accepted
- Date: 2026-11-24

## Context

The app shows a score on A4 pages (ADR-0014, ADR-0015): systems are spread to
fill each page, and a gap separates the pages. On a screen many players prefer
the music as one continuous scroll, with no page breaks, the systems always as
close as their ink allows. Laying out one page as long as the song is already
possible (`height` 0, `--layout scroll`), but showing it as one item would mean
one texture as tall as the song: tens of thousands of pixels for a long score,
past what a GPU takes, and all of it painted again for every edit.

## Decision

**Strips of whole systems.** `cutIntoStrips(page, height)` cuts that endless page
into strips no higher than `height`, each holding whole systems. The cuts fall
halfway between the ink of one system and the ink of the next, so no glyph,
line or curve is cut, and a strip takes as many systems as fit; a system higher
than `height` gets a strip of its own. Every item goes to the strip that holds
its anchor (a glyph's or text's origin, the middle of a line, beam or curve, the
middle of a system, staff or measure box; a column goes with its measure's
system) and keeps its place, so the strips stacked without gaps are the page.
Each strip is an ordinary display list with its origin at its own top, so
everything that works on pages (painting, the caret, clicks, the playback
cursor, selections) works on strips unchanged.

**CLI.** `elara-cli render --layout strips` lays out the endless page of
`--layout scroll` and cuts it into strips `--strip-height` staff spaces high
(the paper's height by default); `--svg` writes one file per strip.

**App.** A Page view and a Continuous view button in the status bar switch
between pages and strips as high as an A4 page; `--continuous` starts in the
continuous view. The view stacks the strips on one sheet with one shadow and no
gap between them, and hides the page number. `ScoreDocument` reports where
each page or strip starts (`pageTops`), and the view places every page from it
instead of assuming pages of one height. Only pages near the view are loaded,
so only those hold a texture, as with pages.

## Consequences

- A strip holds about a page of music, so textures and the work an edit
  repaints stay as small as with pages; strips an edit leaves as they were keep
  their textures.
- The continuous view has no page numbers and no spread between systems; the
  title and credits stay at the top of the first strip and the copyright at the
  bottom of the last.
- The view is a `Flickable` with every page placed where `pageTops` says, since
  a `ListView` only estimates where items of different heights lie until they
  have been created.
