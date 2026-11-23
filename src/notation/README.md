# elara_notation

The notation engine (ADR-0011): it turns the `elara_core` model into what a
reader sees. Lengths are in staff spaces; renderers (the app's Qt scene graph,
`elara-cli`'s SVG) choose how big a staff space is.

| Header | What it gives you |
|---|---|
| `display_list.hpp` | What layout produces and renderers draw: `GlyphItem`, `LineItem` (solid or dashed), `BeamItem`, `TextItem`, `CurveItem` and `PolygonItem` in staff spaces (y down, top line of the first staff at 0), each with a `Role` and its measure; `MeasureBox` per measure and `StaffBox` per staff; `textWidth()`, the text metrics layout assumes. |
| `staff_layout.hpp` | `layoutSystem()`: staves in standard notation or tablature (`StaffSelection`, `StaffView`) on one line, one under the other, notes that start together aligned (ADR-0013). Standard notation: clef, key and time signatures, noteheads at their written pitch, accidentals, dots, ledger lines, stems, flags, beams, ties, articulations, rests and barlines; drum staves through the default drum map (ADR-0012). Tablature: fret numbers with dead, ghost, tied and harmonic notes, hammer-ons, pull-offs, slides and bends, and lanes for palm mute, let ring, vibrato, harmonics and pick strokes. `layoutStaff()`: one standard staff. |
| `svg.hpp` | `toSvg()`: a display list as a standalone SVG document. |
| `smufl.hpp` | SMuFL music fonts: `Glyph`, the symbols Elara draws, with their SMuFL names and code points (`glyphName()`, `codepoint()`, `glyphNamed()`); `EngravingFont`, read from a font's SMuFL metadata (`fromMetadata()`, `fromMetadataFile()`), with each glyph's bounding box, advance width and anchors (`metrics()`) and the font's engraving defaults (`defaults()`). |

The build downloads Bravura (the SMuFL reference font, SIL OFL 1.1) and puts
`Bravura.json`, `Bravura.otf` and the licence in `bin/fonts/` next to the
executables (`cmake/ElaraDependencies.cmake`). Code points are checked against
the SMuFL specification's glyph list before a glyph is added to the enum.

Engraving rules (spacing, stems, beams, accidentals) and their constants are
recorded in ADR-0012, tablature, techniques and systems in ADR-0013. Inside
`src/`, a `StaffWriter` per staff (`standard_staff.cpp`, `tab_staff.cpp`)
reports the ink of each measure's columns and draws at the positions the
system (`system_layout.cpp`) gives them. Tuplets, line breaks and pages come
next in milestone 3 (`docs/roadmap.md`).

- **May depend on:** `elara_core`, the standard library and yyjson (private:
  it never appears in public headers).
- **Must not include:** Qt, JUCE, `elara_io`, `elara_audio`.

Public headers live in `include/elara/notation/`, sources in `src/`, tests in
`tests/notation/`.
