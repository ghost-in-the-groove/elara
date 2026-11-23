# 0015. The score view: painting display lists in the app

- Status: Accepted
- Date: 2026-11-23

## Context

The notation engine lays out pages as display lists (ADR-0012, ADR-0014) and
writes them as SVG for tests. The app must show them: open a file, show a
track or the whole score as notation, tablature or both, scroll through the
pages and zoom smoothly, without blocking the window while a long score is
laid out or painted. CI has no screen (ADR-0003), so the painting must be
testable without one and give the same pixels on every platform.

## Decision

**Painting with QPainter into images.** `ScorePainter` paints a display list
with QPainter: lines, beams, curves and polygons as shapes, music glyphs and
text as glyph outlines (`QRawFont::pathForGlyph`), in the order of the SVG
writer. Outlines rather than the platform's glyph cache: Qt's offscreen platform
on Windows draws no glyphs from fonts loaded from memory, and outlines give the
same result everywhere, on any thread. Each glyph is filled on its own and its
outline made once per size: Qt's rasteriser fills many small paths far faster
than one large path (a page of notation and tablature in 5 ms instead of 28 ms,
`docs/performance.md`).

**Bundled fonts.** The app embeds Bravura (music) and Arimo (text, regular and
italic) as Qt resources, so it paints the same wherever it is installed and
without system fonts. Arimo is metric-compatible with Arial and Helvetica, the
widths that layout assumes for text (`textWidth()`). Both are SIL OFL 1.1,
downloaded at configure time as pinned files checked against their SHA-256.

**Pages as textures.** `ScorePage` (a `QQuickItem`) shows one page: it paints
the page on a worker thread (`QtConcurrent`) into a `QImage` at its size in
device pixels and hands it to the scene graph as a texture (`QSGImageNode`).
While its size changes (zooming), the last texture is stretched by the GPU and
the page is painted again once the size has not changed for 120 ms. Textures
are capped at 8192 pixels a side and 32 million pixels; past that a page is
painted at a lower resolution. A `ListView` creates page items only for the
pages on and near the screen, so memory stays bounded however long the score.

**Opening and layout off the GUI thread.** `ScoreDocument` opens a file
(`io::openFile`) and lays it out on A4 pages on a worker thread, and lays it
out again when the track or the view changes. Songs and pages are immutable
and shared (`std::shared_ptr<const ...>`): a page being painted stays alive
while a new layout replaces it. A generation counter drops results that a
newer request has made stale. The window keeps showing the previous pages
until the new ones are ready.

**Whole tracks.** `notation::selectTracks()` turns tracks and a view (notation,
tablature or both) into staves, tablature only where a staff has a tuning;
the CLI's `render --tracks` and the app share it.

**Interface.** Qt Quick Controls with the Material style (dense, following the
system's light or dark theme). Animations are short transitions on state
changes (fades, the sliding view selector, zoom); nothing animates while the
window is idle, so an idle window costs nothing.

**Headless checks.** `elara --smoke-test FILE` opens the file, waits until its
first page is painted, shows it in every view and with all its tracks, and
fails on any QML warning or error; `--screenshot OUT.png` saves the window.
Both render on the GUI thread (`QSG_RENDER_LOOP=basic`) so that animations
advance with each frame whether or not the window is on a screen.

## Consequences

- The score view is a stack of raster images: crisp at every zoom once painted,
  briefly soft while zooming. Very high zoom levels on large pages hit the
  texture cap; painting only the visible tiles of a page would lift it.
- Selection, editing and playback cursors will be drawn as separate scene
  graph items over the pages, so that a page is painted again only when its
  layout changes. The display list's roles and boxes are there to find what
  was clicked.
- Incremental relayout (milestone 3) will replace only the pages an edit
  touches; page items already repaint when their page changes.
- Text is laid out by the font's advance widths without shaping, which is
  right for the Latin text of scores; other scripts would need `QTextLayout`.
