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
- The same machine drifts between sessions by more than that. When a row looks
  off, build the commit that recorded it in a second worktree and run the old
  and new `elara_benchmarks` alternately; only that difference is the code's.
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
| 2026-11-24 | copy the 20-track song; insert a measure at its start; build it | 63 µs, 17 µs, 14.4 ms (11.6 ms, 260 µs, 13.1 ms before, same machine and run) | measures shared between copies until one changes (ADR-0051): a copy counts 10,000 references instead of copying 160k notes, and the staves shift 16-byte handles. The app takes this copy on the interface thread after every edit. Building pays an allocation per measure the first time it changes |
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
| 2026-11-24 | lay out notation and tablature of one track on A4 pages; all 20 staves | 2.9 ms, 23 ms (3.05 ms, 27.5 ms before, same machine and run) | pages filled in one pass: each system copied into its page already moved, the page's room reserved first, and pages filled on all cores when there are more than 200k items to copy (threads cost more than one track's 40k items) |
| 2026-11-24 | the same again after a one-note edit (measure 250 of the first track) | 0.46 ms, 4.1 ms | incremental relayout (ADR-0052): 2 of 500 measures measured, 1 of 125 systems laid out (2 of 126 for the 20 staves), line breaks found again, pages stacked again. Measures the edit left alone are shared (ADR-0051) and compare by address; compared by value, the 10,000 measures of the 20 staves took 7 ms more (11.4 ms) |
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
| 2026-11-24 | an edit in the app on that song: time until its pages show (median of 48 edits, release build) | 79 ms before (50 to 147); 50 ms (15 to 139) | the edit's sequence (about 74 ms) is built on a worker beside the layout instead of after it, so the pages no longer wait for it: plain edits show in about 18 ms instead of 50, edits that change the tracks in about 50 ms instead of 80 (the song panel's rows, made again on the interface thread, hold them). The layout itself takes 7 ms instead of 3 while the two share the cores. Measured with timers that are not kept |
| 2026-11-24 | the same: time the song's change signal takes, a plain edit and an edit that changes the tracks (add, remove, mute, volume) | 1.1 to 1.8 ms, 26 to 31 ms before; 2.9 to 4.0 ms | the song panel's tracks are a list model that changes, inserts or removes only the rows that differ, so its rows are kept instead of made again; the same with the panel open or closed. The copy of the song (9 ms) is what is left |
| 2026-11-24 | run 64 empty jobs on every core: what a parallel call costs before any job works | 27 µs (697 µs before) | `forEachIndex` hands its jobs to a pool of threads kept waiting, and the calling thread takes jobs too, instead of starting and joining threads on every call (ADR-0053) |
| 2026-11-24 | lay out one track of the large song on A4 pages | 2.8 to 3.1 ms (3.4 to 4.1 ms before, same machine, the two builds run alternately) | measuring and laying out the systems are two parallel calls; most of what they saved was starting 24 threads twice |
| 2026-11-24 | lay out all 20 staves on A4 pages; import from MIDI; export MusicXML; sequence the large song | within run-to-run noise (about ±10 % here) | each of their jobs takes milliseconds, so starting threads was a small part of them |
| 2026-11-25 | read an SFZ instrument of 64 samples (12 MB of 16-bit WAV, 2 s each at 48 kHz) | 5.1 ms | each sample read and made mono on a core of its own (ADR-0066) |
| 2026-11-25 | one second of 32 voices of the SFZ sampler at 48 kHz; each voice bending a whole step | 16.6 ms; 17.0 ms | first measurement: four-point Hermite interpolation of every frame, each of its four frames checked against the sample's end and its loop; three times the built-in plucked string |
| 2026-11-25 | same | 10.3 ms; 11.2 to 11.8 ms (16.3 to 17.1 ms; 16.9 to 17.4 ms before, same machine, the two builds run alternately) | inside the sample and its loop the four frames are read in a row; only the frames at the edges take the checks. The output is the same, frame for frame |
| 2026-11-25 | decode a minute of 16-bit stereo FLAC (libFLAC's default level); the same minute as WAV | 37.6 ms; 17.0 ms | about 1600 times real time (ADR-0067): a library's FLAC samples take about twice as long to read as WAV ones |
| 2026-11-25 | read the outlines of the glyphs Elara draws from `Bravura.otf` | 0.65 ms | first version (ADR-0074): the CFF charstrings of the glyphs run once; the app does it on its first PDF export |
| 2026-11-25 | write the 21 A4 pages of one 500-measure track (notation and tablature) as PDF; compressed | 60 ms; 89 ms | first version (ADR-0074): every number formatted with `std::format`. 2.1 MB, 0.3 MB compressed |
| 2026-11-25 | same | 1.7 ms; 4.1 ms (one thread: 8.8 ms; 45 ms) | numbers written with `std::to_chars` straight into the page's content, and each page written and compressed on a core of its own; the document is the same byte for byte. On one thread, zlib at its default level is most of the time left |
| 2026-11-25 | seconds at the end of the large song | 0.28 ms (1.58 ms before, same machine) | since fermatas (ADR-0085) every beat of every voice was walked to find them, its duration summed on the way: each voice is now looked over for a held beat first, and summing stops at the last one. The sequencer and the MIDI export ask for the same holds measure by measure |
| 2026-11-25 | lay out notation and tablature of one track on A4 pages, on one thread; all 20 staves, on one thread | 10.7 to 11.0 ms; 152 ms (13.2 ms; 178 ms before; the build before lyrics, run alternately with this one: 10.4 to 10.7 ms; 143 to 150 ms) | each staff writer looked through every beat of its staff for lyrics (ADR-0084) when it was made, and the page layout makes writers for every line: measures without lyrics are now skipped one by one, and the syllables held into a line are found when it is drawn |
| 2026-11-26 | export a song of 20 tracks, 1500 measures and 240k notes with `elara-cli export`, release build: as MusicXML; as compressed MXL (peak working set) | 158 MB; 121 MB before. Now 114 MB; 113 MB | the parts written on several threads are joined into the document one by one, each let go once copied, and an MXL packs them where they lie without joining them first; the archive is written straight into the result instead of a buffer of its own copied out. The MXL's peak is mostly reading the `.elara` file. The files are the same byte for byte, but the MXL's entries now carry their sizes in their headers instead of after their data. Times unchanged (0.2 s; 0.3 s) |
| 2026-11-26 | load the same song from its `.elara` file and save it again (`elara-cli edit --in --out`, release build): peak working set | 135 MB before; 125 MB | `song.json` is zipped from yyjson's own buffer instead of a copy of it (10 MB for this song). The file holds the same entries |
| 2026-11-26 | import a 22.9 MB MusicXML file (20 tracks, 3000 measures) with `elara-cli import`: peak working set; time | 189 MB, 4.4 s before; 167 MB, 4.4 s | the text read from the file, or unpacked from an `.mxl` or Guitar Pro container, is parsed in place (`load_buffer_inplace`), so the XML document holds no copy of it. The song and warnings are the same. Measured with the debug build |
| 2026-11-26 | one second of a paused player of the large song (20 tracks, built-in synthesizer) once nothing rings, in 512-frame blocks | 412 µs before; 44 µs | paused with no voice sounding, the block is silent and no track's sound runs; what is left is asking each sound whether it rings. A SoundFont's synthesizer, which works through its mixing buffers even with no voice, should save more per track; not measured |
| 2026-11-26 | with a copy of the large song alive, as the app keeps one for its layout and sequence: remove 100 measures from every track and undo; transpose every track a semitone and undo; edit one measure and undo (the copy taken each run: 66.5 µs of it) | 1.72 ms before; 57.8 ms; 72.0 µs. Now 0.114 ms; 27.6 ms; 66.5 µs | the commands keep the measures they take as the song shared them (`Staff::sharedMeasure()`, `swapMeasure()`) instead of asking for a measure to change, which copied every measure the copy of the song still held. Whether an edit changes the sound is told by comparing only the beats that differ, without copying the two measures |
| 2026-11-26 | the same without a copy alive: remove 100 measures and undo; edit one measure and undo | 236 µs before; 2.1 µs. Now 48 µs; 0.90 µs | the same: no measure is moved out, copied or compared as a whole |
| 2026-11-26 | open an SFZ instrument of 64 stereo samples (16-bit WAV, 10 s each at 44.1 kHz: 108 MB of files, 113 MB of samples once mono) with `elara-cli sfz`: peak working set; time | 338 MB, 570 ms before; 177 MB, 310 ms | each task that reads a sample file averages its channels in place as soon as it is decoded, so the stereo of a file is let go before the task reads the next one. Before, every file stayed decoded in stereo until the last was read, and a mono copy was made beside each: three times the samples kept. What is left is the samples kept and the files being read on 24 cores. The samples are the same, sample for sample. Measured with the debug build |
| 2026-11-26 | transpose every track of a song of 20 tracks, 1500 measures and 240k notes 20 times in a row (`elara-cli edit --in`, release build): peak working set; undo steps kept; time | 1151 MB, 20, 2.3 s before; 404 MB, 5, 1.6 s | the history lets its oldest steps go once they hold more than 256 MiB (ADR-0121); each of these steps keeps every measure it replaced, 49 to 58 MB. The newest step is always kept |
| 2026-11-26 | sequence the large song; the same with every four measures repeated; load its sequence into a player whose sounds are kept | 29.4 ms; 44.3 ms; 3.26 ms before. Now 22.3 ms; 28.9 ms; 2.27 ms | the sequence keeps its events by track, as each track makes them, and the player and the offline renderer read each track's own: the five rounds that merged 20 lists into one, and the pass that split it by track again, are gone. `events()` merges them only for the CLI and the tests |
| 2026-11-26 | the release build with link-time optimisation (MSVC `/GL`, `/LTCG`, through `ELARA_IPO`): the 72 benchmarks; the size of `elara-cli` | median unchanged (x1.00); importing the large song from MusicXML or GPIF 16 % slower, writing PDF pages 17 to 34 % slower, building a song 40 % faster, sequencing and rendering unchanged; 9.4 MB before, 7.1 MB | not turned on: a quarter smaller but no faster on this toolchain, and slower where it matters most. `cmake --preset release -DELARA_IPO=ON` turns it on to measure GCC and Clang, not measured yet |
| 2026-11-26 | the model's size on Windows x64 (MSVC), from `bench -- "[memory]"`: its value types; the large song (20 tracks, 500 measures, 80k beats, 160k notes) | Note 48 B, NoteEffects 40 B, BendCurve 24 B, Harmonic 4 B, Beat 144 B, BeatEffects 40 B, Voice 24 B, Measure 104 B; 20.6 MB of tracks, 129 bytes per note | measured with `approximateBytes()` (vectors by capacity, ADR-0121). Small next to what else is held for the same song (its sequence's events, an SFZ library's samples of 100 MB and more, an undo history of up to 256 MiB): no change to how the model is stored (ADR-0129) |
| 2026-11-26 | the scans for the tempo in force: `Song::tempoAt()` from the start at each of the large song's 500 measures, a tempo change in every one; one track on A4 pages without tempo changes and with one in every measure, every other one gradual | 103 µs for the 500 lookups (0.2 µs each); 2.52 ms and 2.55 ms | no index of tempo or dynamic context: layout carries the tempo along a system and looks it up once per system, the ramp ends and the dynamic before a system stop at the nearest change or struck note, and nothing asks `tempoAt()` for every measure. A score of thousands of measures with a change in each is where to measure again |
| 2026-11-26 | the geometry pages hold, vectors by capacity: notation and tablature of one track of the large song on A4, and all 20 staves; laying them out again after a one-note edit | 21 pages, 2.9 MB; 126 pages, 34.7 MB; 0.48 ms (1 of 125 systems laid out) and 3.78 ms (2 of 126) | the page cache keeps its systems besides the pages it returns, so the geometry is held twice, and an edit copies every system into new pages. Sharing it needs pages made of references to systems, which changes how the app paints them: not done while one track's pages hold a few MB and relaying them out takes half a millisecond; a full score of this size is where it would pay |
| 2026-11-27 | find the layout issues of all 20 staves of the large song on A4 pages (126 pages, none found) | 31.2 ms, 0.25 ms a page (57 ms with one sweep per system) | first version (ADR-0150): the boxes of the roles that must not touch, swept from the left in bands two staff spaces high, so that a box of a 20-staff system meets the few at its height instead of every staff's. On one thread; the app will look only at the pages an edit changed |

The painting benchmarks (`-- "[app]"`) live with the app tests, which need Qt;
`bench` builds and runs them after the others when the app is built.

`python scripts/elara.py bench -- "[memory]"` prints the size of the model types;
`-- "[io]"` prints the size of the large song as JSON, as a `.elara` file, as
MusicXML, as `.mxl`, as MIDI and as GPIF.

### Periodic runs

**2026-11-23, after the score's tempo marks, tuplets and rhythm under
tablature.** Every benchmark ran again; the notation ones had been tagged
`[!benchmark]`, which Catch2 hides unless asked, and now run with the others.
Paths whose code did not change came out 10 to 15 % slower than their rows:
the sounding notes take 12.7 ms against 11.2 ms, and the build of the commit
that recorded the sequencer's 27.9 ms sequences the song in 31.4 to 32.3 ms
today, as the current build does. The machine moved, not the code. The one
regression was opening a `.gpx` file (see its row above), now fixed.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 11.2 ms | 12.7 ms |
| sequence it; with every four measures repeated | 27.9 ms; 42 ms | 32.1 ms; 47.7 ms (the old build: 31.4 to 32.3 ms; 47.3 to 48.0 ms) |
| render its first 10 s offline; on one thread | 24.6 ms; 111 ms | 28.1 ms; 116 ms |
| build, copy and validate the 20-track song | 13.2 ms, 10.7 ms, 3.0 ms | 9.4 ms, 10.0 ms, 4.1 ms (± 1.2) |
| seconds at its end (tempo map walk) | 11.6 µs | 18.7 µs (± 5.1) |
| MusicXML import; export; export on one thread | 118 ms; 88 ms; 155 ms | 110 ms; 98 ms; 157 ms |
| MIDI export; import | 45 ms; 40.5 ms | 31.4 ms; 44.6 ms |
| GPIF import; on one thread | 125 ms; 211 ms | 125 ms; 238 ms |
| 2 tracks from a literal-only `.gpx` | 28 ms | 41 ms; 28 to 31 ms once unpacked once |
| lay out one 500-measure staff; 8 of its measures | 3.8 to 4.1 ms; 61 to 65 µs | 3.7 ms; 58 µs |
| lay out its tablature alone | 1.4 ms | 2.6 ms: the rhythm under it is new (ADR-0041) |
| notation and tablature: the staff; 8 measures; as SVG | 5.2 ms; 81 µs; 43 ms | 5.2 ms; 81 µs; 46 ms |
| one track on A4 pages; on one thread | 3.4 to 3.6 ms; 9.4 ms | 4.2 ms; 11.1 ms |
| the 20 staves on A4 pages; on one thread | 21 ms; 132 ms | 24.1 ms; 153 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.4, 8.5, 19, 11.5 ms | 5.6, 8.6, 19.3, 11.6 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.2, 8.3, 6.1, 4.8 ms | 5.4, 8.9, 6.1, 5.1 ms |
| one tuner window: low E, high E, noise | 0.39, 0.10, 1.07 ms | 0.40, 0.10, 1.12 ms |
| find the caret; pick it from a click | 0.37 µs; 1.5 µs | 0.37 µs; 1.6 µs |

**2026-11-25, after the SFZ sampler, the FLAC decoder and the techniques.**
Every benchmark ran again, the new ones for the sampler and the decoder with
them (rows above). Paths whose code did not change came out within the noise,
except three that allocate the song's measures, which took longer and varied
more than before (build: ± 4.6 ms); the code behind them has not changed
since their rows, so they are watched for the next run rather than chased.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 12.7 ms | 12.9 ms |
| sequence it; with every four measures repeated | 32.1 ms; 47.7 ms | 30.2 ms; 44.9 ms |
| render its first 10 s offline; on one thread | 28.1 ms; 116 ms | 23.2 ms; 116 ms |
| build, copy (shared measures) and validate the 20-track song | 14.4 ms, 63 µs, 4.1 ms | 18.3 ms (± 4.6), 81 µs, 2.9 ms |
| insert a measure at its start | 17 µs | 32 µs |
| seconds at its end (tempo map walk) | 18.7 µs | 12.0 µs |
| MusicXML import; export; export on one thread; `.mxl` | 110 ms; 98 ms; 157 ms; 391 ms | 110 ms; 74 ms; 153 ms; 374 ms |
| MIDI export; import | 31.4 ms; 44.6 ms | 32.2 ms; 42.4 ms |
| GPIF import; on one thread | 125 ms; 238 ms | 103 ms; 210 ms |
| 2 tracks from a literal-only `.gpx` | 28 to 31 ms | 30.9 ms |
| lay out one 500-measure staff; 8 of its measures | 3.7 ms; 58 µs | 3.7 ms; 61 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.2 ms; 81 µs; 46 ms | 5.0 ms; 88 µs; 53 ms |
| one track on A4 pages; on one thread | 2.8 to 3.1 ms; 11.1 ms | 2.2 ms; 10.3 ms |
| the 20 staves on A4 pages; again after an edit | 23 ms; 4.1 ms | 21.9 ms; 4.1 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.6, 8.6, 19.3, 11.6 ms | 5.5, 8.6, 18.9, 11.4 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.4, 8.9, 6.1, 5.1 ms | 5.5, 8.9, 6.3, 5.1 ms |
| one tuner window: low E, high E, noise | 0.40, 0.10, 1.12 ms | 0.42, 0.10, 1.14 ms |
| find the caret; pick it from a click | 0.37 µs; 1.6 µs | 0.39 µs; 1.6 µs |

**2026-11-25, after the grand staff, the included guitar SFZ and the
installers.** Every benchmark ran again on the build of `main` before the PDF
writer (its rows above were measured with it). Paths whose code did not
change came out within the noise, and the three that allocate the song's
measures, watched since the last run, are back at their rows (build: 10.4 ms
± 2.8). Three others moved without a change to their code: importing GPIF on
every core (132 ms ± 24, 103 ms last time and 125 ms before), painting a page
for a high-density screen (11.3 ms ± 2.3) and the tuner's window of noise
(1.54 ms ± 0.13; its code has not changed since it was measured at 1.07 ms).
The first two spread as widely as they moved; the third is watched for the
next run.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 12.9 ms | 12.2 ms |
| sequence it; with every four measures repeated | 30.2 ms; 44.9 ms | 32.1 ms; 45.5 ms |
| render its first 10 s offline; on one thread | 23.2 ms; 116 ms | 21.8 ms; 114 ms |
| build, copy (shared measures) and validate the 20-track song | 18.3 ms (± 4.6), 81 µs, 2.9 ms | 10.4 ms (± 2.8), 68 µs, 4.0 ms (± 1.1) |
| insert a measure at its start | 32 µs | 21 µs |
| seconds at its end (tempo map walk) | 12.0 µs | 12.4 µs |
| MusicXML import; export; export on one thread; `.mxl` | 110 ms; 74 ms; 153 ms; 374 ms | 103 ms; 100 ms; 165 ms; 393 ms |
| MIDI export; import | 32.2 ms; 42.4 ms | 25.2 ms; 45.4 ms |
| GPIF import; on one thread | 103 ms; 210 ms | 132 ms (± 24); 220 ms |
| 2 tracks from a literal-only `.gpx` | 30.9 ms | 29.7 ms |
| lay out one 500-measure staff; 8 of its measures | 3.7 ms; 61 µs | 4.1 ms (± 0.8); 65 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.0 ms; 88 µs; 53 ms | 5.3 ms; 84 µs; 48 ms |
| one track on A4 pages; on one thread | 2.2 ms; 10.3 ms | 2.1 ms; 10.6 ms |
| the 20 staves on A4 pages; again after an edit | 21.9 ms; 4.1 ms | 22.1 ms; 4.3 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.5, 8.6, 18.9, 11.4 ms | 5.7, 11.3 (± 2.3), 19.4, 11.5 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.5, 8.9, 6.3, 5.1 ms | 5.4, 8.8, 6.3, 5.1 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 10.3 ms; 11.2 to 11.8 ms | 10.3 ms; 11.8 ms |
| read an SFZ of 64 samples | 5.1 ms | 4.9 ms |
| decode a minute of FLAC; of WAV | 37.6 ms; 17.0 ms | 39.1 ms; 17.6 ms |
| one tuner window: low E, high E, noise | 0.42, 0.10, 1.14 ms | 0.41, 0.10, 1.54 ms |
| find the caret; pick it from a click | 0.39 µs; 1.6 µs | 0.38 µs; 1.6 µs |

**2026-11-25, after beat text, tapping and gradual tempo changes.** Every
benchmark ran again on the gradual tempo changes with their drawing and
editing. Paths whose code did not change came out within the noise, and the
tuner's window of noise, watched since the last run, is back at its row
(1.08 ms). One moved with its code: the seconds at the end of the song take
19.6 µs against 12.4 µs, as `Song::secondsAt()` now finds where the position
lies before walking the tempo changes, and does a little more for each change
to follow ramps (ADR-0082). It only serves the song's length in the
command-line reports, so it was left as it is. The release build of the same
work had stopped on MSVC's warning C4723 (a division by a constant zero slope
the optimiser saw); `rampSeconds()` now returns before it.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 12.2 ms | 10.6 ms |
| sequence it; with every four measures repeated | 32.1 ms; 45.5 ms | 30.8 ms; 48.1 ms |
| render its first 10 s offline; on one thread | 21.8 ms; 114 ms | 23.1 ms; 121 ms (± 11) |
| build, copy (shared measures) and validate the 20-track song | 10.4 ms (± 2.8), 68 µs, 4.0 ms (± 1.1) | 10.6 ms, 68 µs, 3.1 ms |
| insert a measure at its start | 21 µs | 22 µs |
| seconds at its end (tempo map walk) | 12.4 µs | 19.6 µs |
| MusicXML import; export; export on one thread; `.mxl` | 103 ms; 100 ms; 165 ms; 393 ms | 106 ms; 71 ms (± 22); 148 ms; 354 ms |
| MIDI export; import | 25.2 ms; 45.4 ms | 28.9 ms (± 5.2); 40.3 ms |
| GPIF import; on one thread | 132 ms (± 24); 220 ms | 94 ms (± 14); 203 ms |
| 2 tracks from a literal-only `.gpx` | 29.7 ms | 27.4 ms |
| lay out one 500-measure staff; 8 of its measures | 4.1 ms (± 0.8); 65 µs | 3.7 ms; 63 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.3 ms; 84 µs; 48 ms | 5.1 ms; 92 µs; 43 ms |
| one track on A4 pages; on one thread | 2.1 ms; 10.6 ms | 2.4 ms; 10.4 ms |
| the 20 staves on A4 pages; again after an edit | 22.1 ms; 4.3 ms | 25.9 ms (± 3.0); 4.2 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.7, 11.3 (± 2.3), 19.4, 11.5 ms | 5.3, 8.5, 19.9, 11.9 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.4, 8.8, 6.3, 5.1 ms | 5.1, 8.3, 6.1, 4.8 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 10.3 ms; 11.8 ms | 10.6 ms; 11.7 ms |
| read an SFZ of 64 samples | 4.9 ms | 4.8 ms |
| decode a minute of FLAC; of WAV | 39.1 ms; 17.6 ms | 35.6 ms; 16.2 ms |
| one tuner window: low E, high E, noise | 0.41, 0.10, 1.54 ms | 0.40, 0.10, 1.08 ms |
| find the caret; pick it from a click | 0.38 µs; 1.6 µs | 0.42 µs; 1.5 µs |

**2026-11-25, after lyrics, fermatas and slurs.** Every benchmark ran again
on the slurs (ADR-0086). Two paths had moved with their code, and both are
fixed (rows above): the seconds at the end of the song, 80 times slower since
fermatas, and laying out pages on one thread, 27 % slower since lyrics, which
the same layout on every core hid within its noise (2.6 ms ± 1.4 against
2.4 ms). The other paths came out within the noise.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 10.6 ms | 10.6 ms |
| sequence it; with every four measures repeated | 30.8 ms; 48.1 ms | 31.0 ms; 45.8 ms |
| render its first 10 s offline; on one thread | 23.1 ms; 121 ms (± 11) | 21.0 ms; 109 ms |
| build, copy (shared measures) and validate the 20-track song | 10.6 ms, 68 µs, 3.1 ms | 10.6 ms, 67 µs, 3.1 ms |
| insert a measure at its start | 22 µs | 22 µs |
| seconds at its end (tempo map walk) | 19.6 µs | 1.58 ms; 0.28 ms fixed |
| MusicXML import; export; export on one thread; `.mxl` | 106 ms; 71 ms (± 22); 148 ms; 354 ms | 116 ms (± 16); 55 ms; 152 ms; 338 ms |
| MIDI export; import | 28.9 ms (± 5.2); 40.3 ms | 28.1 ms; 41.0 ms |
| GPIF import; on one thread | 94 ms (± 14); 203 ms | 93 ms (± 15); 206 ms |
| 2 tracks from a literal-only `.gpx` | 27.4 ms | 31.0 ms |
| lay out one 500-measure staff; 8 of its measures | 3.7 ms; 63 µs | 3.6 ms; 74 µs (± 12) |
| notation and tablature: the staff; 8 measures; as SVG | 5.1 ms; 92 µs; 43 ms | 4.9 ms; 108 µs (± 13); 43 ms |
| one track on A4 pages; on one thread | 2.4 ms; 10.4 ms | 2.6 ms (± 1.4); 13.2 ms, 10.7 ms fixed |
| the 20 staves on A4 pages; again after an edit | 25.9 ms (± 3.0); 4.2 ms | 27.5 ms; 4.7 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.3, 8.5, 19.9, 11.9 ms | 5.3, 8.2, 20.2, 11.0 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.1, 8.3, 6.1, 4.8 ms | 5.2, 8.4, 6.0, 4.8 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 10.6 ms; 11.7 ms | 9.9 ms; 10.7 ms |
| read an SFZ of 64 samples | 4.8 ms | 4.6 ms |
| decode a minute of FLAC; of WAV | 35.6 ms; 16.2 ms | 35.8 ms; 16.3 ms |
| read the glyph outlines of `Bravura.otf` | 0.65 ms | 0.68 ms |
| write a track's 21 pages as PDF; compressed; the same on one thread | 1.7 ms; 4.1 ms; 8.8 ms; 45 ms | 1.4 ms; 3.9 ms; 8.3 ms; 43.5 ms |
| one tuner window: low E, high E, noise | 0.40, 0.10, 1.08 ms | 0.39, 0.10, 1.07 ms |
| find the caret; pick it from a click | 0.42 µs; 1.5 µs | 0.37 µs; 1.5 µs |

**2026-11-25, after tenuto, octave lines, trills, mordents and turns.** Every
benchmark ran again on the mordents and turns (ADR-0091). A beat grew from 136
to 144 bytes with its octave line (ADR-0089) and no path moved with it: all
came out within the noise. MIDI import, a little slower at first (44.2 ms
± 5.7), ran again alone at 41.4 ms.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 10.6 ms | 10.0 ms |
| sequence it; with every four measures repeated | 31.0 ms; 45.8 ms | 30.6 ms; 45.6 ms |
| render its first 10 s offline; on one thread | 21.0 ms; 109 ms | 20.0 ms; 107 ms |
| build, copy (shared measures) and validate the 20-track song | 10.6 ms, 67 µs, 3.1 ms | 10.2 ms, 68 µs, 2.8 ms |
| insert a measure at its start | 22 µs | 22 µs |
| seconds at its end (tempo map walk) | 0.28 ms | 0.26 ms |
| MusicXML import; export; export on one thread; `.mxl` | 116 ms (± 16); 55 ms; 152 ms; 338 ms | 97 ms (± 14); 54 ms; 153 ms; 335 ms |
| MIDI export; import | 28.1 ms; 41.0 ms | 30.6 ms (± 4.8); 41.4 ms |
| GPIF import; on one thread | 93 ms (± 15); 206 ms | 96 ms (± 14); 206 ms (± 16) |
| 2 tracks from a literal-only `.gpx` | 31.0 ms | 28.6 ms |
| lay out one 500-measure staff; 8 of its measures | 3.6 ms; 74 µs (± 12) | 3.8 ms; 64 µs |
| notation and tablature: the staff; 8 measures; as SVG | 4.9 ms; 108 µs (± 13); 43 ms | 5.1 ms; 88 µs; 40 ms |
| one track on A4 pages; on one thread | 2.6 ms (± 1.4); 10.7 ms | 2.2 ms; 10.0 ms |
| the 20 staves on A4 pages; again after an edit | 27.5 ms; 4.7 ms | 23.5 ms (± 2.3); 3.5 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.3, 8.2, 20.2, 11.0 ms | 5.2, 8.0, 18.4, 10.8 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.2, 8.4, 6.0, 4.8 ms | 5.2, 8.4, 5.9, 4.8 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 9.9 ms; 10.7 ms | 10.0 ms; 10.7 ms |
| read an SFZ of 64 samples | 4.6 ms | 4.2 ms |
| decode a minute of FLAC; of WAV | 35.8 ms; 16.3 ms | 35.8 ms; 16.0 ms |
| read the glyph outlines of `Bravura.otf` | 0.68 ms | 0.68 ms |
| write a track's 21 pages as PDF; compressed; the same on one thread | 1.4 ms; 3.9 ms; 8.3 ms; 43.5 ms | 1.4 ms; 4.0 ms; 8.4 ms; 43.0 ms |
| one tuner window: low E, high E, noise | 0.39, 0.10, 1.07 ms | 0.39, 0.10, 1.06 ms |
| find the caret; pick it from a click | 0.37 µs; 1.5 µs | 0.37 µs; 1.5 µs |

**2026-11-25, after the sustain pedal.** Every benchmark ran again once the
pedal was read from MIDI files (ADR-0092). As first merged, the sequencer's
pedal pass sorted every measure of every track, pedal marks or not: 34.8 ms to
sequence the 20-track song, which has none, against 31.0 ms without the pass.
It now looks for marks one track per core and stops when there are none:
31.8 ms. The full run came out up to 15 % slower on paths nothing changed
(FLAC, the synthesizers, painting), the machine busier than for the last row;
the groups that ran again alone came back to the last row's times, and the
table gives the faster of the two runs. Building the 20-track song stayed at
16.8 ms (± 2.1) against 10.2 ms, though no code it runs has changed since the
last row: a run on a quiet machine is due before reading anything into it.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 10.0 ms | 10.6 ms |
| sequence it; with every four measures repeated | 30.6 ms; 45.6 ms | 31.8 ms; 45.4 ms |
| render its first 10 s offline; on one thread | 20.0 ms; 107 ms | 20.4 ms; 108 ms |
| build, copy (shared measures) and validate the 20-track song | 10.2 ms, 68 µs, 2.8 ms | 16.8 ms (± 2.1), 64 µs, 3.2 ms |
| insert a measure at its start | 22 µs | 18 µs |
| seconds at its end (tempo map walk) | 0.26 ms | 0.26 ms |
| MusicXML import; export; export on one thread; `.mxl` | 97 ms (± 14); 54 ms; 153 ms; 335 ms | 120 ms (± 26); 55 ms; 164 ms (± 22); 351 ms |
| MIDI export; import | 30.6 ms (± 4.8); 41.4 ms | 28.7 ms; 42.5 ms |
| GPIF import; on one thread | 96 ms (± 14); 206 ms (± 16) | 100 ms (± 20); 218 ms (± 36) |
| 2 tracks from a literal-only `.gpx` | 28.6 ms | 28.8 ms |
| lay out one 500-measure staff; 8 of its measures | 3.8 ms; 64 µs | 4.1 ms; 69 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.1 ms; 88 µs; 40 ms | 5.4 ms; 95 µs; 43 ms |
| one track on A4 pages; on one thread | 2.2 ms; 10.0 ms | 2.2 ms; 11.0 ms |
| the 20 staves on A4 pages; again after an edit | 23.5 ms (± 2.3); 3.5 ms | 24.3 ms (± 3.5); 4.4 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.2, 8.0, 18.4, 10.8 ms | 5.3, 9.0 (± 1.6), 20.9 (± 2.8), 11.0 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.2, 8.4, 5.9, 4.8 ms | 5.9, 8.7, 6.0, 4.9 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 10.0 ms; 10.7 ms | 10.9 ms; 10.8 ms |
| read an SFZ of 64 samples | 4.2 ms | 3.5 ms |
| decode a minute of FLAC; of WAV | 35.8 ms; 16.0 ms | 40.8 ms (± 7.8); 17.7 ms |
| read the glyph outlines of `Bravura.otf` | 0.68 ms | 0.70 ms |
| write a track's 21 pages as PDF; compressed; the same on one thread | 1.4 ms; 4.0 ms; 8.4 ms; 43.0 ms | 1.5 ms; 3.9 ms; 8.7 ms; 43.7 ms |
| one tuner window: low E, high E, noise | 0.39, 0.10, 1.06 ms | 0.39, 0.10, 1.07 ms |
| find the caret; pick it from a click | 0.37 µs; 1.5 µs | 0.36 µs; 1.5 µs |

**2026-11-25, after fingerings, staccatissimo, breath marks and split
beats.** Every benchmark ran again, on a quiet machine, once fingerings
(ADR-0093), staccatissimo (ADR-0094), breath marks (ADR-0095) and the beats
MusicXML import splits under chord symbols and words (ADR-0096) were in.
Nothing moved beyond run-to-run noise, and building the 20-track song is back
to 11.0 ms: the 16.8 ms of the last row was the busier machine. The split
costs nothing on files without marks between beats, since the importer only
looks for a beat to split where no beat starts at a mark.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 10.6 ms | 10.2 ms |
| sequence it; with every four measures repeated | 31.8 ms; 45.4 ms | 29.1 ms; 44.2 ms |
| render its first 10 s offline; on one thread | 20.4 ms; 108 ms | 20.9 ms; 111 ms |
| build, copy (shared measures) and validate the 20-track song | 16.8 ms (± 2.1), 64 µs, 3.2 ms | 11.0 ms, 74 µs, 3.2 ms |
| insert a measure at its start | 18 µs | 22 µs |
| seconds at its end (tempo map walk) | 0.26 ms | 0.26 ms |
| MusicXML import; export; export on one thread; `.mxl` | 120 ms (± 26); 55 ms; 164 ms (± 22); 351 ms | 106 ms (± 17); 54 ms; 150 ms; 339 ms |
| MIDI export; import | 28.7 ms; 42.5 ms | 28.4 ms; 41.5 ms |
| GPIF import; on one thread | 100 ms (± 20); 218 ms (± 36) | 98 ms (± 14); 215 ms (± 18) |
| 2 tracks from a literal-only `.gpx` | 28.8 ms | 29.6 ms |
| lay out one 500-measure staff; 8 of its measures | 4.1 ms; 69 µs | 4.1 ms; 70 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.4 ms; 95 µs; 43 ms | 5.5 ms; 97 µs; 42 ms |
| one track on A4 pages; on one thread | 2.2 ms; 11.0 ms | 2.2 ms; 11.0 ms |
| the 20 staves on A4 pages; again after an edit | 24.3 ms (± 3.5); 4.4 ms | 25.1 ms (± 2.0); 3.9 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.3, 9.0 (± 1.6), 20.9 (± 2.8), 11.0 ms | 5.4, 8.6, 19.5, 11.6 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.9, 8.7, 6.0, 4.9 ms | 5.2, 8.4, 6.0, 4.9 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 10.9 ms; 10.8 ms | 9.9 ms; 10.8 ms |
| read an SFZ of 64 samples | 3.5 ms | 3.7 ms |
| decode a minute of FLAC; of WAV | 40.8 ms (± 7.8); 17.7 ms | 35.7 ms; 16.2 ms |
| read the glyph outlines of `Bravura.otf` | 0.70 ms | 0.71 ms |
| write a track's 21 pages as PDF; compressed; the same on one thread | 1.5 ms; 3.9 ms; 8.7 ms; 43.7 ms | 1.5 ms; 4.1 ms; 8.5 ms; 44.0 ms |
| one tuner window: low E, high E, noise | 0.39, 0.10, 1.07 ms | 0.39, 0.10, 1.10 ms |
| find the caret; pick it from a click | 0.36 µs; 1.5 µs | 0.38 µs; 1.5 µs |

The app's own memory was measured for the first time: the peak working set
and private bytes of the release build while it opens a song and takes a
screenshot, in a 2880 x 1935 pixel window.

| Song | Working set | Private bytes |
|---|---|---|
| one page (a corpus file) | 221 MB | 216 MB |
| the same, Qt Quick's software renderer instead of Direct3D | 160 MB | 119 MB |
| 8 tracks, 177 measures, all tracks as notation and tablature | 247 MB | 278 MB |
| the same song, its first track only | 241 MB | 273 MB |

About 100 MB is the graphics driver's, and most of the rest is Qt Quick
itself. The larger song costs the same with one track shown as with eight, so
its extra memory follows the pages the view keeps loaded (the visible ones and
a page's height either side), each painted into a 32-bit image of about 8 MB
at this size before it becomes a texture, not the song. Painting pages into
8-bit images (the ink is one colour) would save the images' share of it;
not worth doing at these sizes.

**2026-11-25, after tremolo picking on the stem and measure repeats.** Every
benchmark ran again once tremolo picking through the stem (ADR-0101) and
measure repeats (ADR-0102), with their MusicXML and caret editing, were in.
Playback now reads each measure through `repeatSource()`, and the sounding
notes and the sequence of the 20-track song take what they took. Two parallel
paths came out slower, the MusicXML export (79 ms) and the GPIF import
(114 ms), while their one-thread times stayed put. Run again, the export took
98, 97 and 84 ms; the build of `main` from before these changes, run the same
way right after, took 98, 56 and 92 ms. On this machine the parallel export
swings between about 55 and 98 ms from run to run whatever the code, so its
rows compare best on the one-thread time.

| Benchmark | Last row | This run |
|---|---|---|
| sounding notes of the 20-track song | 10.2 ms | 9.7 ms |
| sequence it; with every four measures repeated | 29.1 ms; 44.2 ms | 29.3 ms; 44.8 ms |
| render its first 10 s offline; on one thread | 20.9 ms; 111 ms | 19.2 ms; 108 ms |
| build, copy (shared measures) and validate the 20-track song | 11.0 ms, 74 µs, 3.2 ms | 10.6 ms, 67 µs, 3.2 ms |
| insert a measure at its start | 22 µs | 23 µs |
| seconds at its end (tempo map walk) | 0.26 ms | 0.28 ms |
| MusicXML import; export; export on one thread; `.mxl` | 106 ms; 54 ms; 150 ms; 339 ms | 90 ms; 79 ms (84 to 98 ms again, `main`'s build 56 to 98 ms); 153 ms; 354 ms |
| MIDI export; import | 28.4 ms; 41.5 ms | 26.1 ms; 41.6 ms |
| GPIF import; on one thread | 98 ms; 215 ms | 114 ms; 216 ms |
| 2 tracks from a literal-only `.gpx` | 29.6 ms | 28.7 ms |
| lay out one 500-measure staff; 8 of its measures | 4.1 ms; 70 µs | 4.0 ms; 69 µs |
| notation and tablature: the staff; 8 measures; as SVG | 5.5 ms; 97 µs; 42 ms | 5.4 ms; 94 µs; 42 ms |
| one track on A4 pages; on one thread | 2.2 ms; 11.0 ms | 2.1 ms; 11.1 ms |
| the 20 staves on A4 pages; again after an edit | 25.1 ms; 3.9 ms | 24.1 ms; 3.9 ms |
| paint an A4 page: 100 %, high density, 200 %, the 20-staff page | 5.4, 8.6, 19.5, 11.6 ms | 5.8, 8.5, 18.3, 10.9 ms |
| 1 s of 32 voices: plucked, piano, sawtooth, drums | 5.2, 8.4, 6.0, 4.9 ms | 5.2, 8.3, 6.0, 4.8 ms |
| 1 s of 32 SFZ voices; each bending a whole step | 9.9 ms; 10.8 ms | 9.8 ms; 10.6 ms |
| read an SFZ of 64 samples | 3.7 ms | 3.8 ms |
| decode a minute of FLAC; of WAV | 35.7 ms; 16.2 ms | 35.8 ms; 16.1 ms |
| read the glyph outlines of `Bravura.otf` | 0.71 ms | 0.70 ms |
| write a track's 21 pages as PDF; compressed; the same on one thread | 1.5 ms; 4.1 ms; 8.5 ms; 44.0 ms | 1.3 ms; 3.8 ms; 8.3 ms; 43.4 ms |
| one tuner window: low E, high E, noise | 0.39, 0.10, 1.10 ms | 0.39, 0.10, 1.07 ms |
| find the caret; pick it from a click | 0.38 µs; 1.5 µs | 0.37 µs; 1.5 µs |

## Heavy use

The benchmarks time one function on a song built in memory. `scripts/usage.py`
runs the packaged programs as a person would, on large songs it writes to disk
(self-made guitar tracks), and records what each run costs the machine: wall
and processor time (all threads), peak working set and committed private
memory, bytes read and written, and the size of the programs and the install
folder. With `--hold` and `--play` the app stays open on its welcome pane, on
an open song, or playing it, and the script samples its processor and memory
use meanwhile.

```
python scripts/elara.py usage                 # pack the app, then measure it
python scripts/elara.py usage --no-package    # measure the last package again
python scripts/usage.py --bin build/release/bin --no-app --only export
```

The large song has 20 guitar tracks of 500 measures (121k notes, 24.6 MB of
MusicXML, 0.32 MB as `.elara`); the huge one 1500 measures (360k notes,
73.3 MB of MusicXML, 0.95 MB as `.elara`). Each scenario runs three times and
reports the median.

**2026-11-27, Windows package.** Before: the package of `main` that morning.
After: with the release build optimised as MSVC's release (ADR-0144), the
packages trimmed (ADR-0145), XML element text kept in its element on import,
and MusicXML files written from their parts. Both packages measured one after
the other on the reference machine. Times: wall s, CPU s; memory: peak working
set / peak private MB.

| Scenario | Before | After |
|---|---|---|
| CLI: import the huge MusicXML, save `.elara` | 0.82, 1.41; 579 / 572 | 0.72, 1.27; 412 / 407 |
| CLI: open the huge `.elara`, save it again | 0.82, 0.81; 209 / 342 | 0.65, 0.66; 206 / 341 |
| CLI: transpose the huge song 20 times | 2.63, 2.64; 395 / 398 | 2.01, 1.98; 391 / 397 |
| CLI: export the huge song as MusicXML (274 MB) | 0.66, 1.92; 353 / 609 | 0.56, 1.69; 337 / 334 |
| CLI: export it as `.mxl` | 1.56, 2.48; 227 / 374 | 1.56, 3.59; 224 / 373 |
| CLI: export it as MIDI | 0.42, 0.94; 194 / 219 | 0.40, 0.70; 191 / 219 |
| CLI: render the large song's 1000 s of audio, built-in synthesizer | 3.28, 35.5; 72 / 75 | 3.34, 34.4; 69 / 75 |
| the same through MuseScore General | 34.6, 478; 397 / 403 | 35.4, 478; 394 / 402 |
| App: start, show the welcome pane, quit | 1.66, 0.70; 161 / 150 | 1.63, 0.72; 159 / 150 |
| App: open the large song, first track | 1.77, 1.70; 258 / 336 | 1.77, 1.42; 266 / 341 |
| App: open it with all its tracks | 1.91, 2.59; 354 / 418 | 1.91, 2.27; 347 / 417 |

The app held open for 10 s (after only; `--hold` is new), with the share of
one core it used over the last 4.5 s, once opening has settled, and its
working set then. In the first seconds after a song opens the app paints the
pages around the view (about 12 % of a core for 5 s on the large song).

| App state | CPU | Working set |
|---|---|---|
| welcome pane, idle | 0.0 % | 132 MB |
| the large song open, idle | 0.0 % | 250 MB |
| playing it, built-in synthesizer | 9.0 % | 238 MB |
| playing it through MuseScore General | 19.4 % | 562 MB |
| playing it, guitars through the included SFZ | 14.9 % | 261 MB |

Programs: `elara.exe` 16.7 to 8.4 MB, `elara-cli.exe` 9.1 to 3.1 MB. Install
folder: 162.2 MB in 172 files to 112.4 MB in 120; zip 83.0 to 68.4 MB, of which
the SoundFont is 38.1 MB.

What the numbers point at next:

- MuseScore General costs the app about 325 MB for 20 tracks. Rendering
  through it with `elara-cli` takes 105 MB for one track and about 12 MB for
  each track more: every track has a synthesizer of its own, and each loads
  the SoundFont's 3861 preset and instrument zones again (the samples are
  shared).
- Opening a `.elara` file commits 341 MB for 206 MB used: yyjson reserves
  room for as many values as the text could hold.
