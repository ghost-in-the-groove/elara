# Test corpus

Real-world files used by importer, notation and playback tests (milestone 2+).

## Rules

- Only files we created ourselves or that carry a licence allowing
  redistribution in an AGPL-3.0 repository (CC0 preferred).
- Never add files exported from commercial songbooks, tab sites or other
  copyrighted arrangements.
- Record every file in `SOURCES.md`: path, author, licence, how it was
  produced and what it exercises.
- Keep files small; one feature per file where possible
  (e.g. `gp5/bends.gp5`, `musicxml/tuplets.musicxml`).

## Import and render snapshots

Every MusicXML, MIDI and Guitar Pro file here is imported by a CTest case
(`cli.snapshot_<folder>_<name>`) that compares `elara-cli --json import` with
`tests/snapshots/import/<folder>/<name>.json`.

Every file is also laid out on A4 pages, all tracks as notation and
tablature, by `cli.render_snapshot_<folder>_<name>`, which compares
`elara-cli render --svg` with `tests/snapshots/render/<folder>/<name>.svg`
(`<name>-1.svg`, `<name>-2.svg`... for several pages); `guitar-techniques.gp`
is also rendered as notation only and as tablature only. Coordinates may
differ by 0.002 staff spaces, since compilers may round the last bit of a
computation differently; anything else must match.

A new file gets its snapshots, and an intended change to the importers or to
the layout updates them, with

    ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot

(in PowerShell: `$env:ELARA_UPDATE_SNAPSHOTS=1; py scripts/elara.py test -R snapshot`,
then `Remove-Item Env:ELARA_UPDATE_SNAPSHOTS`),

after which the diff of the snapshots is reviewed like code: it shows exactly
what the change does to every file. The SVG snapshots can also be opened in a
browser; they show the music glyphs once `Bravura.otf` sits next to them.
