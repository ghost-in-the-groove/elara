# 0054. Worker results arrive from the event loop

- Status: Accepted
- Date: 2026-11-24

## Context

The app opens files, lays out pages, builds sequences and paints pages on
worker threads (ADR-0015, ADR-0031) and takes each result on the interface
thread with `QFuture::then(this, ...)`. A generation number drops the result of
any job a later edit has replaced, so one call that makes three edits (undoing
three steps, say) shows one layout: the last one.

`then(context, ...)` runs its continuation at once when the job has already
finished as it is attached. A layout started by the first of those edits could
then be shown inside the edit that started it, before the next edit replaces
it. The interface showed a layout nobody would see, and how many layouts a call
showed depended on how fast the workers were. The smoke tests count layouts,
and on CI runners with few cores they counted one too many now and then: a
thread scheduled away between starting the job and attaching its continuation
is all it takes. Holding the interface thread for 150 ms at that point made the
smoke test that opens a file show 67 layouts instead of 43.

## Decision

**A worker's result always reaches the interface thread from its event loop,
never inside the call that started the job.** `whenDone(job, context, done)`
attaches the continuation with `then(context, ...)` and posts `done` to
`context` as a queued call from there. Opening, laying out, sequencing and
painting all go through it.

## Consequences

- Each result waits one more pass of the event loop: microseconds, against the
  milliseconds of the job.
- A call that makes several edits shows the last one's layout and nothing in
  between, whatever the timing, so the smoke tests' layout counts hold on any
  machine.
- A context destroyed before its result arrives gets nothing, as before:
  `then(context, ...)` drops the continuation, and Qt drops a queued call to a
  deleted object.
- New worker jobs in the app use `whenDone` instead of `then(this, ...)`.
