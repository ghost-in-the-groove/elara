# elara_notation

The notation engine (ADR-0011): it turns the `elara_core` model into what a
reader sees. Lengths are in staff spaces; renderers (the app's Qt scene graph,
`elara-cli`'s SVG) choose how big a staff space is.

| Header | What it gives you |
|---|---|
| `display_list.hpp` | What layout produces and renderers draw: `GlyphItem` (at the staff's size or `scale`d), `LineItem` (solid or dashed), `BeamItem`, `TextItem`, `CurveItem` and `PolygonItem` in staff spaces (y down, top line of the first staff at 0), each with a `Role` and its measure; `SystemBox` per system, `StaffBox` per staff, `MeasureBox` per measure and `ColumnBox` per column (where the notes starting at a time in a measure are placed); `textWidth()`, the text metrics layout assumes. |
| `staff_layout.hpp` | `layoutSystem()`: staves in standard notation or tablature (`StaffSelection`, `StaffView`) on one line, one under the other, notes that start together aligned (ADR-0013). Standard notation: clef, key and time signatures, noteheads at their written pitch, accidentals, dots, ledger lines, stems, flags, beams, ties, articulations, rests and barlines; tuplet numbers and brackets (ADR-0040); dynamics marks under the staff where the loudness changes (ADR-0044); repeat barlines, the play count of repeats played more than twice and the brackets of alternate endings above the first staff (ADR-0016), segno, coda and jump texts above them (ADR-0049), tempo marks above those (ADR-0042) and the names of sections, boxed, above everything (ADR-0063); drum staves through the default drum map (ADR-0012). Tablature: fret numbers with dead, ghost, tied and harmonic notes, hammer-ons, pull-offs, slides and bends, and lanes for palm mute, let ring, vibrato, harmonics, pick strokes, strums and tremolo picking; without standard notation of the staff, its rests and its rhythm under it (stems, beams, flags, dots and tuplets, ADR-0041) and its dynamics marks. `layoutStaff()`: one standard staff. `selectTracks()`: the staves of whole tracks as notation, tablature or both (`TrackView`), tablature only where a staff has a tuning; `hasTablature()`. |
| `page_layout.hpp` | `layoutPages()`: the same staves broken into systems that fill the width of a page, justified, and stacked on pages with the title and credits, measure numbers, track names and page numbers (ADR-0014); `PageLayoutOptions` (page size in staff spaces, margins, measures per system, threads), `pageOptions()` for A4 or Letter (`kA4`, `kLetter`) at a staff space in millimetres. Systems are laid out on every core. `cutIntoStrips()` cuts a page as long as the music into strips of whole systems, no higher than a given height, that stacked without gaps are the page (the continuous view, ADR-0057). |
| `page_layout.hpp` | `PageLayoutCache`: lays out the versions of a song as it is edited, each as `layoutPages()` would, measuring and laying out again only the measures and systems an edit changed (ADR-0052) |
| `svg.hpp` | `toSvg()`: a display list as a standalone SVG document. |
| `caret.hpp` | Keyboard entry on a laid-out score (ADR-0032): `caretBox()`, where a caret shows (over the fret number on its string, on a drum lane where the drum map places its kit piece, or across a standard staff; the free slot between the columns around its time), and `caretAt()`, the caret a click picks (the nearest string or drum lane, ADR-0035). |
| `smufl.hpp` | SMuFL music fonts: `Glyph`, the symbols Elara draws, with their SMuFL names and code points (`glyphName()`, `codepoint()`, `glyphNamed()`); `EngravingFont`, read from a font's SMuFL metadata (`fromMetadata()`, `fromMetadataFile()`), with each glyph's bounding box, advance width and anchors (`metrics()`) and the font's engraving defaults (`defaults()`). |

The build downloads Bravura (the SMuFL reference font, SIL OFL 1.1) and puts
`Bravura.json`, `Bravura.otf` and the licence in `bin/fonts/` next to the
executables (`cmake/ElaraDependencies.cmake`). Code points are checked against
the SMuFL specification's glyph list before a glyph is added to the enum.

Engraving rules (spacing, stems, beams, accidentals) and their constants are
recorded in ADR-0012, tablature, techniques and systems in ADR-0013, line
breaking and pages in ADR-0014. Inside
`src/`, a `StaffWriter` per staff (`standard_staff.cpp`, `tab_staff.cpp`)
reports the ink of each measure's columns and draws at the positions the
system (`system_layout.cpp`) gives them; `page_layout.cpp` measures every
measure, picks the line breaks and stacks the systems; tuplets are in ADR-0040.
Incremental relayout comes next in milestone 3 (`docs/roadmap.md`).

- **May depend on:** `elara_core`, the standard library (threads included) and
  yyjson (private: it never appears in public headers).
- **Must not include:** Qt, JUCE, `elara_io`, `elara_audio`.

Public headers live in `include/elara/notation/`, sources in `src/`, tests in
`tests/notation/`.
