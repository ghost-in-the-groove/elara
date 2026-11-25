# 0074. PDF export written by Elara

- Status: Accepted
- Date: 2026-11-25

## Context

A score could be laid out on A4 pages, shown in the app and written as SVG
(one file per page), but not printed or sent as a PDF, which is what a
musician hands to a band or a print shop. Qt can print to PDF, but the
notation module may not use Qt (golden rule 2), the CLI has no Qt at all, and
CI has to check the result without a screen (ADR-0003). Embedding the music
font as a font program would need subsetting the OpenType CFF table, and
viewers differ in how well they handle CFF fonts placed in PDF files.

## Decision

**`elara_notation` writes PDF 1.7 itself from the display lists, drawing each
music glyph from its outline in the font file rather than embedding the
font.**

- `FontOutlines` reads an OpenType font with CFF outlines (Bravura.otf): the
  table directory, the Unicode `cmap` (formats 4 and 12) and the CFF table,
  whose Type 2 charstrings it runs (local and global subroutines, hints, flex)
  into moves, lines and cubic curves in staff spaces (one em is four). It reads
  only the glyphs of `Glyph`, and refuses other files with the reason.
- `toPdf()` writes one page per display list, the size of the list (the A4
  page, or the line's ink plus a margin). A page's content works in staff
  spaces with y going down, as the display lists do, so the items are written
  as they are: lines (with dashes), beams, ties and slurs as filled lenses,
  polygons, and each glyph as a form XObject made once from its outline and
  placed with its scale. Text uses Helvetica and Helvetica-Oblique, the
  standard fonts every viewer has, in WinAnsi encoding (characters outside it
  become `?`); the title goes in the document information as UTF-16.
- Streams are compressed when the caller passes a compressor: `elara_io`
  wraps miniz as `zlibCompress()` (and `zlibDecompress()` for tests), so the
  notation module stays free of third-party libraries.
- `elara-cli render --pdf FILE` writes every page (or the line) as one
  document and reports `"pdf": {"file", "pages", "bytes"}`. The app's More
  menu has Export as PDF… (Ctrl+P), which lays the shown track or tracks out
  again on A4 pages in the shown view, so the continuous view exports as pages
  too, and writes the file through `QSaveFile`.

## Consequences

- The PDF looks the same in every viewer: there is no font to substitute or
  fail to load, and text is searchable and copyable for titles and names
  (music glyphs are shapes, not text).
- The outlines cost a tenth of a millisecond to read; the 21 pages of a
  500-measure track are written in a few milliseconds (`docs/performance.md`),
  so the app exports on the interface thread.
- Unit tests check every glyph's outline against the bounding box in the
  font's metadata, the structure of a document (cross-reference offsets, page
  tree, transforms, items) and that compressed streams inflate to the plain
  ones; CLI tests check the JSON, the file and the errors; the app's smoke test
  exports the open score and counts its pages. What a PDF looks like was
  checked by eye in a browser's PDF viewer, not in CI.
- Text outside WinAnsi (Cyrillic, CJK) prints as `?` until an embedded text
  font is added.
