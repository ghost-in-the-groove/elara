# elara_notation

The notation engine (ADR-0011): it turns the `elara_core` model into what a
reader sees. Lengths are in staff spaces; renderers (the app's Qt scene graph,
`elara-cli`'s SVG) choose how big a staff space is.

| Header | What it gives you |
|---|---|
| `display_list.hpp` | What layout produces and renderers draw: `GlyphItem`, `LineItem` and `BeamItem` in staff spaces (y down, top staff line at 0), each with a `Role` and its measure; `MeasureBox` per measure. |
| `staff_layout.hpp` | `layoutStaff()`: one staff on one line, measures at their natural width, with clef, key and time signatures, noteheads at their written pitch, accidentals, dots, ledger lines, stems, flags, beams, rests and barlines; drum staves through the default drum map (ADR-0012). |
| `svg.hpp` | `toSvg()`: a display list as a standalone SVG document. |
| `smufl.hpp` | SMuFL music fonts: `Glyph`, the symbols Elara draws, with their SMuFL names and code points (`glyphName()`, `codepoint()`, `glyphNamed()`); `EngravingFont`, read from a font's SMuFL metadata (`fromMetadata()`, `fromMetadataFile()`), with each glyph's bounding box, advance width and anchors (`metrics()`) and the font's engraving defaults (`defaults()`). |

The build downloads Bravura (the SMuFL reference font, SIL OFL 1.1) and puts
`Bravura.json`, `Bravura.otf` and the licence in `bin/fonts/` next to the
executables (`cmake/ElaraDependencies.cmake`). Code points are checked against
the SMuFL specification's glyph list before a glyph is added to the enum.

Engraving rules (spacing, stems, beams, accidentals) and their constants are
recorded in ADR-0012. Ties, tuplets, articulations, tablature, systems and
pages come next in milestone 3 (`docs/roadmap.md`).

- **May depend on:** `elara_core`, the standard library and yyjson (private:
  it never appears in public headers).
- **Must not include:** Qt, JUCE, `elara_io`, `elara_audio`.

Public headers live in `include/elara/notation/`, sources in `src/`, tests in
`tests/notation/`.
