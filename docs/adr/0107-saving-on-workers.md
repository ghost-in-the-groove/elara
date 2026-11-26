# 0107. Saving and PDF export on workers

- Status: Accepted
- Date: 2026-11-26

## Context

`ScoreDocument::save()` encoded and wrote the song on the interface thread, and
`exportPdf()` laid the pages out again, wrote them as PDF, compressed them and
wrote the file there too. Encoding the 20-track benchmark song takes about
45 ms (`docs/performance.md`), and writing it now waits for the disk to hold
the data before the rename (ADR-0106's copies write the same way). Laying out
and writing a long track as PDF takes some tens of milliseconds more. During
all of that the window neither draws nor answers, and a slow or network disk
stretches it to seconds.

Moving the work to a worker raises three questions. Edits made while a save is
written must not be marked saved. Two saves to one file must not finish out of
order, leaving the older song on the disk. And whatever waited for the save,
closing the window or opening another file, must wait for it to end.

## Decision

- `save()` and `exportPdf()` start the work and return. The song, an immutable
  snapshot the pages already share, goes to the worker as it is, without a
  copy. Saves run in a queue of their own, one at a time and in the order
  asked; exports run in another. `saveFinished(file, saved)` and
  `exportFinished(file, written)` follow, on the interface thread, after
  `editFailed()` with the reason when the work failed. `saving` is true while a
  save is being written.
- `core::Document::revision()` names each state of the song: undo and redo
  return to a state and its revision, and no other state ever takes it. A save
  reads the revision as it starts and marks that state saved
  (`markSaved(revision)`) as it ends. Edits made in between leave the song
  modified, and undoing them comes back to the state saved. If another song
  replaced the one saved meanwhile, the save marks nothing.
- The window runs what waited for a save once `saveFinished` says it is on the
  disk. If the song changed again while it was written, the window asks again
  whether to save. A failed save drops what waited. Anything that would replace
  the song, or close the window, while a save is written waits for every save
  asked to end, and only then asks whether to save, if the song changed
  meanwhile. Work that cannot start, with no song to save, reports
  `editFailed()` and its `...Finished` signal after the call returns, as work
  that ran does. When the document goes, its queues finish the
  jobs left in them first.

## Consequences

- The interface thread spends microseconds on a save or an export, however
  large the score or slow the disk. The work itself costs the same.
- The smoke test edits the song while it is being saved. It checks that the
  file holds the state saved, that the song is still modified, and that undoing
  the edit returns to an unmodified song. The PDF check waits for
  `exportFinished`.
- Exporting MusicXML and MIDI still runs on the interface thread. Both are
  quick next to PDF; they can move to the export queue the same way when they
  show.
