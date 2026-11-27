# 0139. A song from a newer Elara says what it may lose

- Status: Accepted
- Date: 2026-11-27

## Context

ADR-0006 lets a newer Elara add optional data to `song.json` without raising
`formatVersion`: an older reader ignores the keys it does not know. That keeps
old versions able to open new files, but an older Elara that opens such a file
and saves it writes only what it read, so the newer data is gone, and nothing
said so. Once the first preview is out, files will travel between versions.

Raising `formatVersion` for every addition would make each new feature lock
its files away from every older version, even when the song reads well
without it (a fingering, a colour, a text style).

## Decision

- Optional data is still added within a version. An addition raises
  `formatVersion` (with a reader kept for the old one) only when ignoring it
  would misread what an older version does read: a key that changes the
  meaning of others, not one that adds to them.
- Every file names the Elara that saved it (`"generator": "Elara X.Y.Z"`,
  already written since ADR-0006). A file saved by a newer Elara opens with a
  warning, like an import's (`io::openNative()`, `io::readSongJson()`): "saved
  by Elara X.Y.Z, newer than this one (A.B.C): what it added is not shown, and
  saving here leaves it out". The app shows it with the import warnings; the
  CLI's `dump` lists it (`warnings` in `--json`).
- A generator that is not "Elara" and three numbers (a file written by hand or
  by another program) says nothing: there is no version to compare.

## Consequences

- Nothing is lost silently: the warning comes before the save that drops the
  data. It fires on any newer file, whether or not that one uses anything new,
  since the reader cannot tell an addition from a key it never knew.
- Keeping unknown keys through a save (so that nothing is lost at all) stays
  possible later; it would need the reader to keep what it skips beside the
  model, which no part of Elara does yet.
