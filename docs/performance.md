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
| 2026-11-23 | import the 20-track song from GPIF (39 MB: every bar, voice, beat and note under its own id) | 434 ms | first measurement; one thread |
| 2026-11-23 | same | 299 ms | each note's and beat's children and properties found in one pass instead of a search per feature (a note asked for a dozen properties) |
| 2026-11-23 | same | 240 ms | bars, voices, beats and notes looked up in tables indexed by id (Guitar Pro numbers them from 0) instead of hash maps |
| 2026-11-23 | same | 125 ms | each staff read on its own thread, warnings merged in reading order (ADR-0010): the same song, warnings and errors on any number of threads. One thread: 211 ms; most of what is left is the XML parse. Parsing without end-of-line and attribute normalisation gained nothing |
| 2026-11-23 | import 2 of those tracks from a `.gpx` container written as literal bytes only (4.4 MB) | 55 ms | the same GPIF alone: 17 ms, so unpacking took 38 ms, reading the stream bit by bit |
| 2026-11-23 | same | 28 ms | bits taken from a 64-bit buffer with a shift and a mask; back references copied as a block |
| 2026-11-23 | same | 28 to 31 ms (38 to 46 ms before, same machine and run) | reading the part configuration had unpacked the container a second time; it is now unpacked once for both files. The GPIF alone: 17 ms |
| 2026-11-23 | load the Bravura SMuFL metadata | 10.5 ms | yyjson parse of a 1.2 MB file; done once per run |
| 2026-11-23 | lay out one 500-measure staff of the large song (4000 two-note chords, 2000 beams) | 3.5 ms | first version, one thread: about 0.9 µs per chord; 12k glyphs, 15.5k lines |
| 2026-11-23 | lay out 8 measures of that staff (a screenful) | 54 µs | relayout after an edit costs far less than a frame |
| 2026-11-23 | lay out all 20 staves of the large song | 78 ms | one thread; staves are independent, so systems can lay them out in parallel when it shows |
| 2026-11-23 | write the 500-measure staff as SVG (30k elements) | 29 ms | `std::format` per element; only tests and the CLI write SVG |
| 2026-11-23 | lay out one 500-measure staff, same song | 3.8 to 4.1 ms (3.84 ms before, same machine and run) | staff writers spaced by a system (ADR-0013), ties, articulations and technique lanes; per-measure buffers reused, the first staff moved instead of copied, drawing helpers inline |
| 2026-11-23 | lay out 8 measures of that staff | 61 to 65 µs (54 µs before) | about 8 µs of fixed cost per call: the writer, its lanes and the system's buffers |
| 2026-11-23 | lay out the tablature of that staff | 1.4 ms | 8000 fret numbers |
| 2026-11-23 | lay out its notation and tablature together | 5.2 ms | the two staves cost about what they cost apart: the shared spacing pass is cheap |
| 2026-11-23 | lay out 8 measures of notation and tablature | 81 µs | |
| 2026-11-23 | write notation and tablature of the staff as SVG (46k elements) | 43 ms | text and knockout rectangles for every fret number |
| 2026-11-23 | lay out notation and tablature of the 500-measure staff on A4 pages (125 systems, 21 pages) | 3.4 to 3.6 ms | first version (ADR-0014): every measure measured, line breaks by dynamic programming, systems justified and laid out on every core. One thread: 9.4 ms, so measuring costs about as much as the layout itself |
| 2026-11-23 | lay out all 20 staves of the large song on A4 pages | 21 ms | one thread: 132 ms (the 20 staves on one line: 84 to 90 ms); 6x faster on every core, the same pages on any number of threads |
| 2026-11-23 | lay out one 500-measure staff, notation and tablature, 8 measures of each | 3.8 ms, 1.3 ms, 5.8 ms, 74 µs, 83 µs | the same layout with springs (ADR-0014); within run-to-run noise of the rows above |
| 2026-11-23 | paint an A4 page of notation and tablature (588 glyphs, 840 lines, 391 texts) at 100 % (6.6 px per staff space) | 28 ms | first version (ADR-0015): all glyphs in one path, all letters in one path |
| 2026-11-23 | the same | 5.4 ms | each glyph filled on its own, outlines made once per glyph and size: Qt's rasteriser fills many small paths much faster than one large one (glyphs alone: 22 ms to 2.8 ms) |
| 2026-11-23 | the same on a high-density screen (2 device pixels per pixel), and at 200 % | 8.5 ms, 19 ms | 70 ms and 131 ms before; pages are painted on worker threads, so this is latency, not stutter |
| 2026-11-23 | paint an A4 page of the 20-staff score (1960 glyphs, 2781 lines, 1282 texts), high density | 11.5 ms | 132 ms before |
| 2026-11-23 | sequence the 20-track song (500 measures, 160k notes, 320k events) | 74 ms | first version (ADR-0017): every note timed through the play order with fraction arithmetic, then one sort of all events |
| 2026-11-23 | the same with every four measures repeated (1000 measures and 320k notes played) | 133 ms | |
| 2026-11-23 | sequence the 20-track song | 42.5 ms | each written note timed once in its measure (a repeat only adds a sum), tempo as precomputed segments in doubles instead of fractions per call; events made track by track on all cores. Repeated: 62 ms |
| 2026-11-23 | the same | 33 ms | the track lists merged pairwise, each round on all cores, instead of through a heap (20 ms of the 42); notes timed in blocks on all cores. Repeated: 45.5 ms |
| 2026-11-23 | sounding notes of the 20-track song | 10.7 ms | 16 ms before: each voice read on its own thread and the lists merged pairwise instead of one stable sort of every note. The sequencer starts from them: 26 ms, repeated 40 ms (2.8x and 3.3x the first version) |
| 2026-11-23 | one second of 32 voices of the built-in synthesizer at 48 kHz: plucked strings, piano partials, sawtooths, drums | 5.2, 8.5, 6.0, 5.1 ms | first version (ADR-0018): 3.4 to 5.5 ns per voice and frame, so 20 tracks of 32 voices take under a fifth of one core. Keeping the four partials in local arrays so that they could be one vector operation made the piano slower (11.6 ms) and was dropped |
| 2026-11-23 | render the first 10 s of the large song (20 guitar tracks) offline | 26.4 ms | tracks on every core, mixed in track order (ADR-0018): 380 times faster than real time. One thread: 113 ms |
| 2026-11-23 | sequence the 20-track song, articulations applied (ADR-0022, ADR-0023) | 32.3 ms | each note's effects read from the score, and passes for let ring, legato and slides. 27.1 ms on the same machine just before. Repeated: 47 ms (41 before) |
| 2026-11-23 | the same | 27.9 ms | the written note's beat and note kept in 32 bits, so a sounding note stays 80 bytes (sounding notes: 13.2 to 11.2 ms), and the legato and slide passes skipped when no note moves: 3 % over the sequencer without articulations. Repeated: 42 ms |
| 2026-11-23 | the synthesizer able to glide (plucked, piano, sawtooth, drums), and the 10 s offline render | 5.2, 8.3, 6.1, 4.8 ms; 24.6 ms | a string reads its delay line from a moving position, so its length can change while it rings: no measurable cost. One thread: 111 ms |
| 2026-11-23 | detect the pitch of one tuner window (48 kHz, 30 to 1600 Hz, 3201 samples) of a guitar's low E | 1.61 ms | first version (ADR-0028): the YIN difference function at every lag up to 1601, one multiply-add per sample and lag |
| 2026-11-23 | the same; a high E; a window of noise | 0.39 ms, 0.10 ms, 1.07 ms | lags past the first dip under the threshold are no longer computed (YIN never looks at them), and each difference is four independent sums. Noise has no dip, so every lag is still computed: the worst case, 3 % of one core for a window every 33 ms |
| 2026-11-23 | find the keyboard entry caret on the 21 A4 pages of one 500-measure track (last measure), and the caret a click on a page picks | 0.37 µs, 1.5 µs | first version (ADR-0032): a linear search of each page's measure boxes and columns; nothing to gain beside a 3.5 ms relayout |
| 2026-11-23 | an edit in the app on a 20-track, 500-measure song (160k notes): time the interface thread spends on it, an edit that adds or removes a track | 41 to 60 ms, 99 to 119 ms before; 10 to 25 ms, 37 to 40 ms | the edit's sequence built and compared on the layout's worker thread instead (ADR-0031), and a track's tuning fields made only when they open. What is left is the copy of the song (10 ms) and, when the tracks change, the song panel's rows made again. Measured in a release build with timers that are not kept |
| 2026-11-24 | the same: time the song's change signal takes, a plain edit and an edit that changes the tracks (add, remove, mute, volume) | 1.1 to 1.8 ms, 26 to 31 ms before; 2.9 to 4.0 ms | the song panel's tracks are a list model that changes, inserts or removes only the rows that differ, so its rows are kept instead of made again; the same with the panel open or closed. The copy of the song (9 ms) is what is left |

The painting benchmarks (`-- "[app]"`) live with the app tests, which need Qt;
`bench` builds and runs them after the others when the app is built.

`python scripts/elara.py bench -- "[memory]"` prints the size of the model types;
`-- "[io]"` prints the size of the large song as JSON, as a `.elara` file, as
MusicXML, as `.mxl`, as MIDI and as GPIF.
