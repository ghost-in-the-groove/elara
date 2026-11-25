# 0106. Recovery copies

- Status: Accepted
- Date: 2026-11-26

## Context

The window asks to save unsaved changes before it closes, opens another file or
makes a new song, but a crash, a killed process or a power cut skips the
question and loses every change since the last save. Saving writes the whole
song (a few milliseconds to some tens for a long score), so an editor can
afford to copy the song often; what it must not do is make typing wait for the
disk, or offer a copy back while the run that wrote it is still going, or
offer a copy that is no longer needed.

## Decision

- While the song has unsaved changes, the app keeps a copy of it, in the native
  format, in a recovery folder: the app's local data folder, `recovery`. Checks
  use a temporary folder, or the one `--recovery-folder` names.
- `RecoveryStore` (in `elara_app_view`) owns the folder. Each run writes one
  copy, `<id>.elara`, beside `<id>.json` (the song's own file and the title the
  window showed), and holds `<id>.lock` from its first write until it ends. The
  lock is a `QLockFile` that goes stale only once the process that took it is
  gone, or the computer restarted, however long the app runs.
- `ScoreDocument` writes the copy five seconds after the first unsaved change,
  and again five seconds after the first change since, so that a crash loses at
  most the last few seconds of edits. The song is shared, not copied, with a
  worker that encodes and writes it, so an edit never waits for the disk. One
  write runs at a time.
- The copy goes as soon as the song has no unsaved changes: it was saved, its
  changes were undone, or another song replaced it. A write that started
  before the removal is dropped: the store counts removals, and a write passes
  the count it read. When the window closes, its changes were saved or
  discarded, so the copy goes then too.
- At start the window lists the copies whose lock nobody holds, the newest
  first, and takes their locks, so that two runs started together do not both
  offer them. For each one it offers Recover or Discard; closing the dialog
  keeps the copies for the next start. A recovered song opens with its title
  and its own file, if it had one, and with unsaved changes
  (`core::Document::markUnsaved()`). Its copy goes once this run's own copy
  holds the song.

## Consequences

- A crash keeps the work. `app.smoke_recovery_crash` edits a new song and quits
  without cleaning up once its copy is written; `app.smoke_recovery` then
  starts with the same folder, finds the copy offered and recovers it.
- Every few seconds of editing write a small file the way Save writes one:
  in full beside it, synced to the disk, then renamed over it. Nothing is
  written while there are no unsaved changes.
- Two windows of the app keep a copy each. A copy is offered to whichever run
  starts first after its own has ended.
- A copy written by a later version of the format than the app reads fails to
  open like any other such file; it stays in the folder until discarded.
