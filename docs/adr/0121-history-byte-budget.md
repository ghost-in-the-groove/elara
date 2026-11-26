# 0121. The undo history is bounded by its size too

- Status: Accepted
- Date: 2026-11-26

## Context

`core::Document` kept up to 1000 undo steps (`kDefaultUndoLimit`) and dropped
the oldest beyond that, whatever each step held. Commands keep what the song no
longer holds (ADR-0005): a step that transposes every track, pastes over many
measures or removes them keeps every measure it replaced. On a large song one
such step holds as much as the song's measures, so a long session of them kept
many songs' worth of memory in a history nobody would undo that far. It needs
a memory budget beside the number of steps, with a policy for a single very
large step.

## Decision

- `Command::heldBytes()` says roughly how many bytes of song data a command
  holds at the moment, which the song does not: the measures and tracks it
  took. It is 0 unless the command holds such data: `RemoveMeasures`,
  `ReplaceMeasure`, `InsertTrack` (after an undo), `RemoveTrack` and
  `CommandGroup` (what its commands hold). `core::approximateBytes()` measures a
  measure or a track: the objects and what their vectors, strings and shared
  values hold, by capacity. A measure a copy of the song still shares is
  counted whole: it is the history's alone once the copy is gone.
- `Document` keeps each step's bytes, measured when the step is done or undone,
  and their total, `historyBytes()`. After `execute()` it drops the oldest
  steps while there are more than the undo limit or while the steps hold more
  than the byte budget, `kDefaultUndoBytes` (256 MiB) unless given. The newest
  step is always kept, however large, so the last edit can always be undone.
  Undo and redo drop nothing: they move a step between the two lists.
- `elara-cli edit --json` prints `historyBytes`.

## Consequences

- A long session of large edits keeps at most about the budget in its history
  (plus the newest step), and loses only undo steps far back.
- On a song of 20 tracks, 1500 measures and 240k notes, a step that transposes
  every track holds 49 to 58 MB, so 256 MiB keeps the last five such steps:
  20 of them in a row peaked at 404 MB instead of 1151 MB. On a song a tenth
  of that size it keeps about fifty,
  and the 1000 steps of edits of a few measures stay as before.
- The bytes are an estimate: allocator overhead and small-string storage are
  not counted. They are only used to choose which steps to drop.
- A command added later that holds song data must override `heldBytes()`, or
  its steps count as holding nothing.
