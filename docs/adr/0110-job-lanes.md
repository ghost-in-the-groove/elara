# 0110. One running job and the newest request per flow

- Status: Accepted
- Date: 2026-11-26

## Context

Every edit asked for a layout and a sequence, and each request started a
worker job at once (ADR-0015, ADR-0031). Generations dropped the results of all
but the newest request, but only once those jobs had run. A burst of edits,
holding a key down or undoing many steps quickly, therefore ran as many layouts
and sequences as edits. The layout cache (ADR-0052) holds a mutex through each
layout, so those jobs mostly waited for one another, each holding a worker to
make a state nobody would see.

## Decision

- A `JobLane` (`src/app/src/job_lane.hpp`) runs one flow of jobs whose
  results matter only while they are the newest asked for. At most one job
  runs. A request made meanwhile waits, replacing the one already waiting, and
  starts once the running job ends. A result reaches the interface thread only
  if no request came after it and the lane did not drop its work since. A burst
  of requests runs two jobs, the first and the last, however long the burst.
- `ScoreDocument` has two lanes. Opening a file and laying the score out share
  one, as they shared a generation, so that whichever was asked last wins.
  Sequencing has the other. A new song, or a file opened, drops what the lanes
  held for the song before. `busy` is true while either lane runs a job.
- A job cannot be stopped once it runs: the lane only stops waiting for it.
  Results between the first and the last of a burst are not shown, as before.
  Showing them would show pages of a song that is no longer the one edited.
- Each lane counts requests, jobs run, requests skipped and results that came
  too late. The smoke test prints the counts, so a change that makes the app
  run needless jobs shows up in its output.

## Consequences

- A burst of edits costs two layouts and two sequences instead of one each per
  edit. The last edit's layout no longer waits behind the mutex for layouts
  nobody will see.
- A request made while a job runs waits for it to end, even when that job is
  already stale. Opening a file while an edit's layout runs starts a few
  milliseconds later. Stopping a running layout between systems would need the
  layout to check for that, which can come later.
- The counts give later changes, such as skipping the sequence when an edit
  does not change the sound, a figure the smoke test can check.
