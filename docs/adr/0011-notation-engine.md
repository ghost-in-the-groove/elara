# 0011. Notation engine

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 3 turns the model into something people can read: standard notation
and tablature, laid out on systems and pages. The layout must run in the app
(drawn with the Qt scene graph), in `elara-cli` (written as SVG for snapshot
tests, ADR-0003) and later on worker threads for large scores, so it cannot
depend on Qt. Music symbols come from a font: SMuFL (Standard Music Font
Layout) is the open standard that fixes which code point holds which symbol
and how a font publishes its metrics, and Bravura is its reference font,
under the SIL Open Font License 1.1 (compatible with AGPL and redistributable
with the program).

## Decision

**A module of its own.** `elara_notation` (`src/notation`) depends on the core
and the standard library, plus yyjson to read font metadata. It knows nothing
of Qt, JUCE or file formats; it turns a song into a display list (glyphs and
lines at positions) that renderers draw.

**Units.** Every length is in staff spaces (the distance between two staff
lines), as SMuFL measures glyphs. Renderers choose the size of a staff space.
SMuFL metrics measure up from a glyph's origin; they are kept that way in the
font API, and layout converts them when it places glyphs.

**Glyphs in code, metrics from the font.** The glyphs Elara draws are an enum,
each with the name and code point the SMuFL specification gives it (public
facts, checked against the specification's glyph list). Their metrics come at
run time from the font's SMuFL metadata file: bounding box, advance width, the
anchors layout needs (stem attachment, cut-outs, optical centre) and the
engraving defaults (line thicknesses and separations). `EngravingFont` reads
that file; a glyph the file does not describe keeps an empty box and is listed
by `missingGlyphs()`, so another SMuFL font can be tried without crashing.

**Bravura, pinned.** The build downloads three files of a tagged Bravura release
(`Bravura.json`, `Bravura.otf`, `OFL.txt`), each checked against its SHA-256,
and copies them to `bin/fonts/` next to the executables, where `elara-cli` and
the app find them. The download is cached with the other third-party archives.
Updating Bravura means changing the tag and the three hashes; the tests that
read the real metadata say what moved.

## Consequences

- Layout code and its tests run anywhere the core runs; `elara-cli font --json`
  shows what the engine reads from the font.
- A configure step needs the network once (as for JUCE), then the cache.
- Glyphs not in the enum cannot be drawn until they are added, with their code
  point checked against the specification.
- Installers (milestone 7) must ship `fonts/` with the font's licence.
