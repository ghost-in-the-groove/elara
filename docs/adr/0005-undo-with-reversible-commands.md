# 0005. Undo with reversible commands

- Status: Accepted
- Date: 2026-11-23

## Context

Every edit must be undoable, from changing one fret to removing a track, and the
editor will run hundreds of edits per session on songs with 20 tracks and
hundreds of measures. Three common designs:

- **Snapshots** of the whole song per edit. Simple, but copying the 20-track
  benchmark song takes 10.7 ms and more than 10 MB (`docs/performance.md`); a
  hundred steps would hold a gigabyte.
- **Persistent data structures** (shared, copy-on-write subtrees). Cheap
  snapshots, but every model type would become a handle with reference counts,
  and the plain value types of ADR-0004 would be lost.
- **Reversible commands**: each edit knows how to apply and revert itself.

## Decision

Edits go through `Document`, which owns a `Song` and its history and hands out
only a `const Song&`. An edit is a `Command` with `apply(Song&)`, `revert(Song&)`
and a `name()` for the Edit menu. `Document::execute()` applies a command and
records it; `undo()` and `redo()` revert and re-apply it.

- **Commands exchange data with the song instead of copying it.** Replacing a
  measure *swaps* the stored measure with the song's, so apply and revert are
  the same operation; removing a track *moves* it into the command and undo moves
  it back. The history holds exactly what the song no longer holds: nothing is
  stored twice.
- **Coarse, generic commands.** Note-level edits are expressed as replacing the
  measure that contains them (`ReplaceMeasure`, about 1 KB per step) instead of
  one command class per edit. Structure has its own commands: insert/remove
  measures and tracks, set a measure header, song info or start tempo.
  `CommandGroup` makes several commands one undo step.
- **All or nothing.** A command checks its arguments before changing anything;
  if it throws, the song is unchanged and nothing is recorded. A group that fails
  part-way reverts the commands it already applied.
- **Bounded history.** The oldest steps are dropped beyond a limit (1000 by
  default). A save point makes `isModified()` exact, including after undo, redo
  and dropped steps.

## Consequences

- Undo and redo are cheap at any song size, and memory grows with what was
  edited, not with the song.
- Code that edits a song must go through a command; the UI, CLI and importers of
  a live document cannot mutate it directly. Building a song before it becomes a
  document (importers, tests) still uses the `Song` API.
- A new kind of edit needs a command whose revert restores the song exactly.
  The property test (random edits, then undo all) checks this with `Song ==`.
- Merging consecutive edits (typing several digits into one fret) and selection
  state in undo steps are left to the editor milestone.
