# 0133. Bold marks, and the page across the view

- Status: Accepted
- Date: 2026-11-27

## Context

ADR-0131 put the score's texts on one scale. Two things it left were seen on
the user's own score:

- The texts had one regular and one italic face, so only size told a tempo
  or the name of a section from a fingering or a lyric. Engraving sets the
  tempo and rehearsal marks in bold (the defaults of MuseScore and printed
  editions), and they are what a reader looks for first on a page.
- A song opened at paper size, 100 %, whenever the view was wide enough
  (`fitPaper()`). On a wide window that left most of the desk empty either side
  of a page whose staff space was under 7 pixels, and the first thing done
  on opening was to zoom in.

The text font, Arimo, comes from Google Fonts as a variable font whose
weight axis holds the bold. `QRawFont`, which the painter draws outlines
with (ADR-0015), reads a variable font at its default instance only and
cannot be set to a weight.

## Decision

- A text of the display list has a face, `TextFace`: regular, italic or bold.
  It replaces the flag `italic`; the CLI's JSON says it as `"face"`.
- The tempo's "= 120" and the "=" of a triplet feel mark, and the name of a
  section, are bold. "rit." and "accel." stay italic. Their sizes stay those
  of ADR-0131.
- Layout takes the widths of the bold face for bold text (`textWidth()` with
  the face): a bold section name gets a box that fits it, and the marks after
  a tempo leave it room.
- The app paints bold text with Arimo Bold, a static font from the font's own
  repository (googlefonts/Arimo, SIL OFL 1.1, the same licence as the other
  faces), downloaded at configure time as a pinned file checked against its
  SHA-256 and embedded with them. PDF writes it in the standard
  Helvetica-Bold and SVG as `font-weight:bold`.
- A song opens with its page across the view (as "Fit the page width" does),
  but no larger than 150 % (`readingZoom`); on a narrow view, smaller than
  paper size, as before. The zoom keeps following the view's width as panels
  open and close until it is chosen by hand.

## Consequences

- The tempo and the sections stand out at the size they have, and the page
  opens at a size it can be read at: on a view wider than about 1,250 pixels
  at 150 %, with a staff space of 9.9 pixels.
- Less of a page is seen at once on opening: at 150 % on a 1080-pixel screen,
  about half a page. Ctrl+- or the zoom's level gives the rest.
- A third face of the text font is loaded the first time a page needs it.
- A bold italic would be a fourth face; none is used yet ("tr" is a glyph of
  the music font).
- Renders and snapshots of every score with a tempo or a section changed.
