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
| 2026-11-23 | write `song.json` of the 20-track song (7.9 MB of JSON) | 42.8 ms | yyjson document, then compact text (ADR-0006) |
| 2026-11-23 | read that `song.json` back into a song | 35.3 ms | parsing is a small part; most is building the model and parsing pitches and durations |
| 2026-11-23 | save the 20-track song as a `.elara` file (JSON + deflate) | 70.4 ms | 41 KB: the benchmark song repeats one pattern, real songs compress far less |
| 2026-11-23 | open that `.elara` file | 51.3 ms | inflate + read; fast enough for open/save. If it ever shows, write tracks in parallel or emit JSON directly |
| 2026-11-23 | import the 20-track song from MusicXML (33 MB, string and fret on every note) | 265 ms | first version; pugixml alone parses the text in about 100 ms |
| 2026-11-23 | same | 238 ms | each `<note>`'s children found in one pass instead of a dozen `child()` scans; per-measure buffers reused; beats and chords reserved once |
| 2026-11-23 | same | 118 ms | parts after the first read on all cores (ADR-0007); what is left is mostly the XML parse. The result equals the song built in memory and does not depend on the number of threads |
| 2026-11-23 | export the 20-track song as MusicXML (124 MB of indented text: standard and TAB staff for every guitar) | 155 ms | one thread; imports back to the same song |
| 2026-11-23 | same | 88 ms | one part per thread into separate buffers, joined in score order (ADR-0008); same text on any number of threads |
| 2026-11-23 | export it as `.mxl` | 669 ms, 1.09 MB | deflate of the indented text dominates |
| 2026-11-23 | same | 391 ms, 566 KB | archives get compact XML (no indentation), which halves what deflate reads; deflate level 1 was only 40 ms faster but gave 5.5 MB |
| 2026-11-23 | export the 20-track song as MIDI (1.2 MB, 160k notes) | 84.7 ms | first version, one thread; the sounding notes alone take 15 ms |
| 2026-11-23 | same | 45 ms | each track encoded on its own thread into its own chunk, joined in order (ADR-0009): the same bytes on any number of threads. One thread: 74 ms. What is left is mostly the sounding notes and the slowest track |
| 2026-11-23 | import the 20-track song from MIDI | 357 ms | first version: grids fitted per beat on every staff, voices, rhythms spelled |
| 2026-11-23 | same | 287 ms | plain durations and grid spellings computed once instead of normalising fractions for every note |
| 2026-11-23 | same | 40.5 ms | tracks written on all cores, warnings merged in track order: the same song on any number of threads. One thread still takes 287 ms, mostly fraction arithmetic in the grids: the place to look if single-core machines matter |
| 2026-11-23 | load the Bravura SMuFL metadata | 10.5 ms | yyjson parse of a 1.2 MB file; done once per run |
| 2026-11-23 | lay out one 500-measure staff of the large song (4000 two-note chords, 2000 beams) | 3.5 ms | first version, one thread: about 0.9 µs per chord; 12k glyphs, 15.5k lines |
| 2026-11-23 | lay out 8 measures of that staff (a screenful) | 54 µs | relayout after an edit costs far less than a frame |
| 2026-11-23 | lay out all 20 staves of the large song | 78 ms | one thread; staves are independent, so systems can lay them out in parallel when it shows |
| 2026-11-23 | write the 500-measure staff as SVG (30k elements) | 29 ms | `std::format` per element; only tests and the CLI write SVG |

`python scripts/elara.py bench -- "[memory]"` prints the size of the model types;
`-- "[io]"` prints the size of the large song as JSON, as a `.elara` file, as
MusicXML, as `.mxl` and as MIDI.
