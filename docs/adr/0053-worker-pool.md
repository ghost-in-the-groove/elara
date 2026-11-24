# 0053. A worker pool for parallel jobs

- Status: Accepted
- Date: 2026-11-24

## Context

Import, export, sequencing, offline rendering and notation layout split their
work into independent jobs (parts, tracks, systems) and run them through
`core::forEachIndex`. Each call started its threads and joined them at the
end: about 0.7 ms with every core on the benchmark machine before any job did
work. A full layout of one staff makes two or three such calls, so a third of
its 3 ms went to starting threads, and the work thresholds below which callers
stay on one thread had to be set high to make up for it.

## Decision

**One pool of threads, kept waiting.** `core::WorkerPool::shared()` starts on
first use with one thread fewer than the cores and lives until the program
exits. `forEachIndex` queues up to `threads - 1` copies of one task on it and
takes jobs on the calling thread too; every thread takes the next index until
none is left.

**The caller always works.** A pool thread joins a call only while the caller
has not finished; once the caller runs out of indices it waits for the pool
threads that joined and keeps the rest out (the job may live on its stack).
So a call never waits for a free pool thread: nested calls, and several
callers at once, finish even when every pool thread is busy, with fewer
helpers.

**Errors stop the call.** Once a job throws, no further jobs start, and the
first exception is rethrown on the calling thread after the others stop.

**It stays in `core`**, in standard C++ only, so every module keeps one
`forEachIndex` and the CLI, the app and the tests share the same behaviour.
The audio callback never uses it: queueing work locks and allocates.

## Consequences

- A parallel call costs about 27 µs instead of 0.7 ms; per-call thresholds
  can be lowered where measurements show a gain.
- Thread-local caches (MIDI import's grid spellings) now live across calls,
  which only saves work, since they memoise pure functions.
- Asking for more threads than the pool has queues the extra copies; they
  join while there is work left, as with a busy pool.
