# Performance

Elara keeps a small set of micro-benchmarks for its hot paths and reruns them
whenever a change touches one. Numbers are recorded here so regressions are
visible in review.

## Running

```
python scripts/elara.py bench                 # all benchmarks, release preset
python scripts/elara.py bench -- "[core]"     # only one module (Catch2 filter)
```

Benchmarks live in `tests/bench/`, build in the optimised `release` preset and are
never part of `check` or CI: shared CI runners are too noisy to compare timings.

## Rules

- Measure before and after; keep a change only when the gain is clear and the code
  stays readable. Record the result below with the reason for the change.
- Compare runs on the same machine. Report the mean and keep an eye on the
  standard deviation: a change smaller than it is noise.
- The audio thread has a hard budget (one buffer); everything else is judged by
  what a user would notice: opening a large song, editing, scrolling, rendering.

## Results

Reference machine: AMD Ryzen 9 5900X (12 cores, 24 threads), 32 GB RAM,
Windows 11, MSVC 19.44, `release` preset (RelWithDebInfo).

| Date | Benchmark | Mean | Note |
|---|---|---|---|
| 2026-11-23 | sum 10k mixed durations (one long voice) | 998 µs | first measurement |
| 2026-11-23 | sum 10k mixed durations (one long voice) | 358 µs | `Duration::length()` normalises once instead of chaining two multiplications (2.8x) |
| 2026-11-23 | sort 10k fractions | 28 µs | |
| 2026-11-23 | build a 20-track x 500-measure song (80k beats, 160k notes) | 9.2 ms | first measurement |
| 2026-11-23 | copy that song (full snapshot) | 5.2 ms | undo will use commands, not snapshots |
| 2026-11-23 | insert a measure at the start of that song | 0.29 ms | all 20 staves shift |
| 2026-11-23 | sum every voice length of that song | 1.6 ms | about 19 ns per beat |
| 2026-11-23 | seconds at the end of that song (tempo map walk) | 11.6 µs | O(measures); playback will precompute a timeline |
| 2026-11-23 | build the 20-track song, notes with effects attached | 16.8 ms | `Note` grew from 16 to 80 bytes |
| 2026-11-23 | build the 20-track song | 13.2 ms | harmonic fret in tenths (int16), `FretPosition` and tuplet counts in one byte each, fields ordered by size: `Note` 48 bytes, `Beat` 64 |
| 2026-11-23 | copy the 20-track song | 10.7 ms | was 14.8 ms with 80-byte notes; the rest is copying each note's (usually empty) bend curve |
| 2026-11-23 | validate the 20-track song (fills, frets, ties) | 3.0 ms | about 19 ns per note, half of it summing voice lengths; well under a 60 Hz frame, so the editor can validate the whole song after every edit without caching or threads |
| 2026-11-23 | edit one measure of that song and undo | 0.60 µs | the command swaps the measure in and out (ADR-0005); most of the time is building the new measure |
| 2026-11-23 | remove 100 measures from all 20 tracks and undo | 211 µs | 2000 measures and 100 headers moved out and back, nothing copied |
| 2026-11-23 | remove a 500-measure track and undo | 0.31 µs | the track is moved into the command and back; a copy would take about 0.5 ms |

`python scripts/elara.py bench -- "[memory]"` prints the size of the model types.
