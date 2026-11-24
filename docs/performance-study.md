# Performance study of Elara: speed, concurrency and memory

Date: 2026-11-24. Code read: branch `main` (4c32790), as of the restyle of the interface.

## 1. Conclusion

The algorithms on the hot paths are already well worked and measured
(`docs/performance.md`). What is left to gain is not in tuning loops, but in
**where and when the work runs**:

1. **The UI thread does heavy work on every edit.** Every keystroke copies the
   whole song and sequences it again synchronously, and also reloads the
   player. With the reference song that is about 39 ms of blocking per
   keystroke, before layout even starts. Saving is also synchronous.
2. **There is no thread pool and no cancellation.** `core::forEachIndex` creates and destroys
   `std::thread` on every call (13 call sites in `src`), nested inside Qt's
   global pool, which also paints the pages. A stale layout or paint runs to
   completion and only its result is discarded.
3. **Everything is recomputed from scratch.** Every edit sequences the whole song
   again and lays out all the pages; there is no per-measure cache and no notion of "what
   has changed" in the commands.
4. **There are avoidable copies and memory peaks**: full-page textures that
   grow with zoom² × dpr², duplicated buffers when parsing XML/JSON, the final join
   of the MusicXML exporter, and several `struct`s with 64-bit fields for values
   of 8-16 bits.
5. **The "optimised" binary is `RelWithDebInfo` without LTO.** There is no real
   `Release` preset.

On small songs none of this is noticeable: the "high" impact applies to large files
(dozens of tracks, hundreds of measures).

## 2. Method and limits

- **Static, read-only** study: no benchmark was built or run. The figures in ms come from `docs/performance.md` (reference song:
  20 tracks × 500 measures, 160k notes) or are estimates derived from
  them; none is a new measurement.
- Eight subsystems were analysed separately (app-documento, pintado/QML, notation,
  core, audio, io-xml, io-gp, io-midi-nativo). Each finding was then checked against
  the code a second time, with the aim of refuting it.
- Result: **70 findings**: 30 confirmed, 40 partial
  (the code is as described, but the second check lowered the impact or qualified
  the proposal) and 0 refuted.
- The build configuration (section 3.3) was checked directly and did not
  go through the second check.
- Before applying any change: measure before and after with
  `python scripts/elara.py bench`, as `docs/performance.md` requires.

## 3. Priority proposals

### 3.1 Move heavy work off the UI thread (high impact)

| What | Where | Cost today (large song) | Proposal |
|---|---|---|---|
| Song copy + sequencing per edit | `src/app/src/score_document.cpp:877-880` (`edited()`) | 10.7 ms + 28 ms per keystroke | A single worker task that builds `Sequence` and layout together, as `openAndLayOut` already does; `apply()` publishes both |
| Player reload per edit | `src/app/src/audio_controller.cpp:87` (`Player::load`) | Distributes 320k events and creates one `Synth` per track, even if nothing is playing | Load lazily on play, or in the same worker |
| Saving `.elara` | `src/app/src/score_document.cpp:843` (`save()`) | ~70 ms (JSON + deflate) plus the disk | Serialise in a worker on the `shared_ptr<const Song>` snapshot |
| Copy on open | `src/app/src/score_document.cpp:502` | 10.7 ms once | Move the song into the `Document` instead of copying it |

Care is needed when moving the sequencing: `AudioController::songEdited` chooses between
"update mix" and "reload" by comparing the sequence at the moment of
`songChanged`. If the sequence is published later, a signal of its own is needed
(`sequenceChanged`), and the comparison `*sequence != *sequence_` must also move
to the worker.

The fundamental solution is for the model to offer **cheap snapshots**
(measures shared by `shared_ptr`, copy-on-write) and for each command to
declare **what has changed** (`src/core/include/elara/core/document.hpp:47`). With
that, the 10.7 ms copy disappears and the door opens to incremental recomputation.

### 3.2 Concurrency: shared pool, coalescing and cancellation (medium-high impact)

- **Persistent thread pool** in `src/core/include/elara/core/parallel.hpp:41-56`.
  Today every `forEachIndex` creates up to `hardware_concurrency()` threads and the
  calling thread just waits. A sequencing run chains several batches (notes, events and
  each merge round) and a layout two more. Proposal: a single pool, the caller
  takes part in the work, and a parallelism threshold by size (today
  `kParallelMeasures = 48` launches threads for microsecond-sized jobs).
- **A single layout in flight, "last one wins"** in `ScoreDocument::relayout()`
  (`src/app/src/score_document.cpp:466-485`). Today N quick edits launch N full
  layouts, each with its own copy of the song. `addTrack` launches two in a row
  and throws the first away.
- **Cooperative cancellation**: pass a `std::stop_token` to
  `notation::layoutPages` (`page_layout.hpp:30`) and to page painting
  (`src/app/src/score_page.cpp:119-133`), checked between systems and pages.
- **Separate pools**: the layout already parallelises internally; launching it from Qt's
  global pool, which also paints one task per page, oversubscribes the cores.
- **Robustness**: if creating a thread fails, the destructor of the `std::thread`
  vector calls `std::terminate` (`parallel.hpp:41`); use `std::jthread`.

The audio thread is well solved: lock-free exchange of the song, atomic
levels, no allocations or locks in `render()`. It should not be touched.

### 3.3 Build (medium impact, low effort)

- `CMakePresets.json` defines only `dev` (Debug) and `release` (**RelWithDebInfo**).
  There is no `build/release/CMakeCache.txt` on the machine the benchmarks were recorded on to check the effective flags; CMake's default value for MSVC in that configuration is `/Zi /O2 /Ob1 /DNDEBUG`, with limited inlining (`/Ob1`).
- There is no `CMAKE_INTERPROCEDURAL_OPTIMIZATION` and no architecture flags in any
  `CMakeLists.txt`, and the five modules are **static** libraries: calls
  between modules (`core` ↔ `notation` ↔ `audio`) cannot be inlined.

Proposal: add a `dist` preset with `CMAKE_BUILD_TYPE=Release`, LTO enabled
and, on MSVC, `/Ob2 /Gy /Gw` with `/OPT:REF,ICF` at link time. Consider `/arch:AVX2`
only for `elara_audio` (the synthesizer's per-sample loops) and PGO with the
benchmarks as training load. Do not use `/fp:fast`: the synthesizer and the
snapshots depend on deterministic results. The benchmarks in
`docs/performance.md` should be repeated with that preset.

### 3.4 Memory (medium impact)

| What | Where | Proposal |
|---|---|---|
| Full-page texture, grows with zoom² × dpr²; `cacheBuffer` keeps off-screen pages | `src/app/src/score_page.cpp:23-24`, `ScoreView.qml:279` | Paint in tiles or limit the resolution to the visible area; free off-screen textures |
| Texture with an alpha channel while being opaque | `src/app/src/score_painter.cpp:292` | `Format_RGB32` and an opaque texture: saves full-page blending |
| `load_buffer` copies the whole text before parsing | `src/io/src/musicxml.cpp:1994`, `src/io/src/guitar_pro.cpp:1355` | `load_buffer_inplace` on the buffer we own |
| One PCDATA node per text leaf in the GPIF DOM | `src/io/src/guitar_pro.cpp:1354` | `parse_embed_pcdata` |
| The MusicXML exporter's join copies the text and keeps two copies | `src/io/src/musicxml_export.cpp:1730-1732` | Write each part into its slice of the final buffer or flush part by part |
| Full mutable yyjson DOM + copy of the text when saving | `src/io/src/native_format.cpp:104-108` | Emit JSON directly into a buffer, track by track |
| The sequencer merges the per-track lists and the player splits them again | `src/audio/src/sequence.cpp:696-711`, `src/audio/src/playback.hpp:35-50` | Keep the per-track lists and hand them to the player |
| Pairwise merge that reallocates and reinitialises in every round | `src/core/src/performance.cpp:142` | Two alternating buffers reserved once |
| `SoundingNote` 80 B, `PlayedNote` 64 B, `NoteEvent` 32 B | `core/performance.hpp:18`, `audio/sequence.hpp:31-66` | 8-16 bit fields: 48, 32 and 24 B |
| Undo history bounded by steps (1000), not by memory | `src/core/include/elara/core/document.hpp:42` | Add a cap on retained bytes |
| `Bravura.otf` loaded twice | `src/app/src/main.cpp:203`, `score_painter.cpp:54-57` | Share a single `QByteArray` |

### 3.5 Recompute only what changes (medium-high impact, high effort)

- **Incremental layout** (`src/notation/src/page_layout.cpp:289-304`): a cache of
  per-measure metrics with invalidation, and repaint only the pages whose content
  changes. Today a mix change (volume, mute) also triggers a full layout
  even though the notation does not read the mix.
- **Incremental sequencing**: do not build a new `Sequence` just to check
  whether it "sounds the same" (`score_document.cpp:881`); classify the commands
  (mix / information / notes) and resequence only the measures touched.
- **Finer signals**: `songChanged` notifies 12 properties and rebuilds the
  track list on every note (`score_document.cpp:891`).

## 4. Recommended order of work

1. **Quick and safe (days)**: `dist` preset with LTO; `load_buffer_inplace` and
   `parse_embed_pcdata`; opaque texture; `reserve` where it is missing; compact
   `SoundingNote`/`NoteEvent`/`PlayedNote`; coalesce `relayout()`; avoid the
   layout when only the mix changes.
2. **Structural (1-2 weeks)**: shared thread pool with caller participation;
   sequencing, player loading and saving off the UI thread;
   layout and paint cancellation; per-track event lists all the way to the player.
3. **Fundamental (weeks)**: snapshots with structural sharing and commands
   that declare their scope; incremental layout and sequencing; tiled textures.

Benchmarks that are missing to be able to measure these improvements: UI-thread latency
of "edit a note" from end to end, number of threads created per edit,
peak resident memory when importing/exporting, and texture memory per zoom
level.

## 5. Index of findings

| Impact | Verdict | Axis | Effort | Subsystem | Finding | Where |
|---|---|---|---|---|---|---|
| high | confirmed | speed | M | app-documento | Every edit copies the whole song and sequences it again on the UI thread (app-documento/A1) | `src/app/src/score_document.cpp:880` |
| high | confirmed | concurrency | L | core | The model offers no cheap snapshot or change extent: every edit copies the whole song and resequences it on the UI thread (core/A1) | `src/core/include/elara/core/document.hpp:47` |
| medium | confirmed | concurrency | M | app-documento | Player::load runs on the UI thread on every edit, even when playback is stopped (app-documento/A5) | `src/app/src/audio_controller.cpp:87` |
| medium | confirmed | concurrency | M | app-pintado-qml | Stale paints are not cancelled (only their result is discarded) and a page not yet painted launches a full paint on every size change (app-pintado-qml/P2) | `src/app/src/score_page.cpp:120` |
| medium | confirmed | memory | M | notation | Nested vectors per beat and per column that are freed and requested again in every measure (notation/N4) | `src/notation/src/standard_staff.cpp:484` |
| medium | confirmed | memory | M | core | soundingNotes: each merge round allocates new buffers, initialises and copies them; the lists grow without reserve through a reference to the shared vector (core/A4) | `src/core/src/performance.cpp:142` |
| medium | confirmed | speed | M | audio | The sequencer merges the per-track lists into one global list that the player splits again per track (audio/A1) | `src/audio/src/sequence.cpp:696` |
| medium | confirmed | memory | S | io-gp | DOM with default flags: a separate PCDATA node for every text leaf of the GPIF (io-gp/GP3) | `src/io/src/guitar_pro.cpp:1354` |
| medium | confirmed | concurrency | M | io-midi-nativo | Saving .elara runs entirely on the UI thread (JSON + deflate level 6 + disk write) (io-midi-nativo/N1) | `src/app/src/score_document.cpp:843` |
| medium | partial | concurrency | M | app-documento | relayout() without coalescing or cancellation: N quick edits = N full layouts and N live copies (app-documento/A2) | `src/app/src/score_document.cpp:472` |
| medium | partial | memory | L | app-pintado-qml | Whole-page texture: memory grows with zoom² x dpr² and cacheBuffer keeps whole off-screen pages alive (app-pintado-qml/P1) | `src/app/src/score_page.cpp:24` |
| medium | partial | speed | L | notation | layoutPages measures and lays out the whole document again on every edit: there is no per-measure or per-system cache (notation/N1) | `src/notation/src/page_layout.cpp:289` |
| medium | partial | concurrency | M | notation | Two batches of std::thread created and destroyed per layout: there is no pool and the parallelism threshold is very low (notation/N2) | `src/notation/src/page_layout.cpp:298` |
| medium | partial | concurrency | M | core | forEachIndex creates and destroys std::thread on every call, with no pool or global cap (core/A2) | `src/core/include/elara/core/parallel.hpp:44` |
| medium | partial | speed | L | io-xml | Importing MusicXML: pugixml parsing is single-threaded and is already ~85% of the time (io-xml/X1) | `src/io/src/musicxml.cpp:1994` |
| medium | partial | speed | L | io-xml | Exporting .mxl: single-threaded deflate over the whole text, plus two copies of the resulting archive (io-xml/X2) | `src/io/src/archive.cpp:73` |
| medium | partial | concurrency | M | io-xml | forEachIndex creates and destroys threads on every call and the calling thread does not work (io-xml/X6) | `src/core/include/elara/core/parallel.hpp:44` |
| medium | partial | memory | S | io-gp | pugixml copies the whole GPIF: load_buffer instead of load_buffer_inplace on the std::string that is already ours (io-gp/GP1) | `src/io/src/guitar_pro.cpp:1355` |
| medium | partial | memory | M | io-midi-nativo | The native writer builds a full mutable yyjson DOM and then duplicates the text (io-midi-nativo/N2) | `src/io/src/native_format.cpp:108` |
| low | confirmed | memory | S | app-documento | Startup: Bravura.otf is loaded twice and Bravura.json (1.2 MB) is parsed lazily on the first open (app-documento/A9) | `src/app/src/main.cpp:203` |
| low | confirmed | speed | S | app-pintado-qml | Delegate recycling: the reused page shows another page's texture until its paint arrives, and the pooled items keep repainting (app-pintado-qml/P5) | `src/app/src/score_page.cpp:67` |
| low | confirmed | speed | M | app-pintado-qml | Playback cursor: new QVariantMap, linear search and five bindings plus a JS handler per tick of a 16 ms QTimer that is not synchronised with vsync (app-pintado-qml/P6) | `src/app/qml/ScoreView.qml:27` |
| low | confirmed | speed | S | app-pintado-qml | The page texture is created with an alpha channel even though it is opaque: full-page blending and three layers of overdraw per page (app-pintado-qml/P7) | `src/app/src/score_painter.cpp:292` |
| low | confirmed | speed | S | notation | stretchFor does 60 bisections per tight system, serially, on a piecewise-linear function (notation/N8) | `src/notation/src/page_layout.cpp:185` |
| low | confirmed | speed | S | notation | toSvg creates one std::string per number and another per element, with no reserve of the result (notation/N9) | `src/notation/src/svg.cpp:16` |
| low | confirmed | memory | S | core | SoundingNote takes 80 bytes because it uses size_t and int in fields that fit in 8-16 bits (core/A7) | `src/core/include/elara/core/performance.hpp:18` |
| low | confirmed | speed | M | core | Transposing, retuning and changing dynamics copy and compare measure by measure in series and create a heap-allocated command per measure (core/A8) | `src/core/src/transpose.cpp:138` |
| low | confirmed | concurrency | S | core | forEachIndex terminates the process if creating a thread fails and does not cancel the remaining work after the first exception (core/A9) | `src/core/include/elara/core/parallel.hpp:41` |
| low | confirmed | speed | S | audio | noteOn of a plucked string: three passes over the delay line, a division and sin() per sample on the audio thread (audio/A7) | `src/audio/src/synth.cpp:734` |
| low | confirmed | memory | S | audio | The retired Loaded is not freed while playback is paused (audio/A8) | `src/audio/src/player.cpp:161` |
| low | confirmed | concurrency | S | audio | position() can return the pre-seek position during a block (audio/A9) | `src/audio/src/player.cpp:166` |
| low | confirmed | memory | M | io-xml | Parallel MusicXML export: the final join copies the 124 MB and keeps two copies alive (io-xml/X3) | `src/io/src/musicxml_export.cpp:1731` |
| low | confirmed | memory | S | io-xml | Import: load_buffer duplicates the text even though the buffer is already ours and disposable (io-xml/X4) | `src/io/src/musicxml.cpp:2026` |
| low | confirmed | speed | S | io-xml | Read buffers are zero-filled before being entirely overwritten (io-xml/X7) | `src/io/src/archive.cpp:40` |
| low | confirmed | speed | S | io-xml | std::format with an allocation per percussion note (and per bend/tempo) in the writer (io-xml/X9) | `src/io/src/musicxml_export.cpp:1332` |
| low | confirmed | speed | S | io-gp | intOf()/trimmed() create a temporary std::string for every integer and every text property (io-gp/GP7) | `src/io/src/guitar_pro.cpp:87` |
| low | confirmed | speed | S | io-gp | Duration cache in an unordered_map per staff when the Rhythm ids are dense (io-gp/GP8) | `src/io/src/guitar_pro.cpp:972` |
| low | confirmed | speed | M | io-midi-nativo | MIDI import: Fraction arithmetic with repeated gcd in the hot loops of grids and rhythms (io-midi-nativo/N3) | `src/io/src/midi_import.cpp:750` |
| low | confirmed | speed | S | io-midi-nativo | When writing, every pitch and duration is formatted with std::format/std::to_string into a temporary std::string and copied again (io-midi-nativo/N5) | `src/io/src/native_format.cpp:310` |
| low | confirmed | memory | S | io-midi-nativo | Opening .elara keeps two copies of the 7.9 MB JSON in addition to the DOM and the model (io-midi-nativo/N9) | `src/io/src/native_format.cpp:948` |
| low | partial | speed | M | app-documento | songChanged is too coarse: every note rebuilds tracks(), the track Repeater and the combo (app-documento/A3) | `src/app/src/score_document.cpp:891` |
| low | partial | speed | M | app-documento | To find out whether it 'sounds the same', another full Sequence is built and compared element by element; the mix also triggers a relayout (app-documento/A4) | `src/app/src/score_document.cpp:881` |
| low | partial | memory | L | app-documento | Three or more full copies of core::Song alive at once; on open, the copy is made on the UI thread (app-documento/A6) | `src/app/src/score_document.cpp:502` |
| low | partial | speed | M | app-documento | Playback cursor driven by a precise 16 ms QTimer, not by vsync, and with a QVariantMap + linear search per tick (app-documento/A7) | `src/app/src/audio_controller.cpp:16` |
| low | partial | concurrency | M | app-documento | Pitch detection (YIN) runs on the UI thread inside the 30 ms poll (app-documento/A8) | `src/app/src/tuner_controller.cpp:176` |
| low | partial | speed | M | app-pintado-qml | The ListView model is an integer: when the number of pages changes, all delegates and their textures are destroyed (app-pintado-qml/P3) | `src/app/qml/ScoreView.qml:272` |
| low | partial | speed | M | app-pintado-qml | Every page paint recreates the fonts from their bytes and extracts all the outlines again: the outline cache lives only within one call (app-pintado-qml/P4) | `src/app/src/score_painter.cpp:130` |
| low | partial | speed | S | notation | Three temporary std::vectors per chord on the hot path of StandardWriter (notation/N3) | `src/notation/src/standard_staff.cpp:496` |
| low | partial | concurrency | M | notation | Page assembly is serial: it shifts and copies all the document's items on the calling thread and without reserve (notation/N5) | `src/notation/src/page_layout.cpp:327` |
| low | partial | concurrency | M | notation | A stale layout cannot be cancelled: layoutPages accepts no stop signal and every relayout launches another using all the cores (notation/N6) | `src/notation/include/elara/notation/page_layout.hpp:30` |
| low | partial | memory | M | notation | DisplayList items with avoidable padding and size_t for the measure index (notation/N7) | `src/notation/include/elara/notation/display_list.hpp:57` |
| low | partial | speed | S | core | Fraction computes 2-3 64-bit gcds per addition/subtraction and normalises already-normalised values again (core/A3) | `src/core/include/elara/core/fraction.hpp:50` |
| low | partial | memory | M | core | Every note and every beat carry a 24-byte std::vector for a curve that is almost always empty, and every beat has its own allocation for its notes (core/A5) | `src/core/include/elara/core/effects.hpp:58` |
| low | partial | memory | M | core | The undo history is bounded by number of steps (1000), not by retained memory (core/A6) | `src/core/include/elara/core/document.hpp:42` |
| low | partial | memory | S | audio | eventsByTrack without reserve and duplicated events (FrameEvent) relative to Sequence (audio/A2) | `src/audio/src/playback.hpp:40` |
| low | partial | concurrency | M | audio | Every forEachIndex of the sequencer and of the offline render creates and destroys its threads (audio/A3) | `src/audio/src/sequence.cpp:698` |
| low | partial | speed | S | audio | Muted tracks (mute, or not solo) are fully synthesised and then discarded (audio/A4) | `src/audio/src/player.cpp:209` |
| low | partial | speed | M | audio | Per-sample voice loops: state in memory, branches and early exit prevent vectorisation (audio/A5) | `src/audio/src/synth.cpp:325` |
| low | partial | memory | M | audio | NoteEvent (32 B) and PlayedNote (64 B) use size_t and int for 8-16 bit values (audio/A6) | `src/audio/include/elara/audio/sequence.hpp:32` |
| low | partial | memory | S | io-xml | Export: the reservations do not take compact mode or tracks without TAB into account (up to 2x too much) (io-xml/X5) | `src/io/src/musicxml_export.cpp:1726` |
| low | partial | speed | S | io-xml | layoutPart walks all the notes of every pitched part only to discard <unpitched> (io-xml/X8) | `src/io/src/musicxml.cpp:482` |
| low | partial | memory | S | io-gp | In .gpx the decompressed container stays alive during the whole parse and the file is extracted without reserve (io-gp/GP2) | `src/io/src/guitar_pro.cpp:1417` |
| low | partial | speed | S | io-gp | intsOf() returns a new std::vector<int> per measure, voice and beat (io-gp/GP4) | `src/io/src/guitar_pro.cpp:125` |
| low | partial | concurrency | M | io-gp | Serial phase before the threads: the five ById tables are built one after another, with an intermediate vector without reserve (io-gp/GP5) | `src/io/src/guitar_pro.cpp:978` |
| low | partial | speed | S | io-gp | The file is read byte by byte with istreambuf_iterator and without a prior size (io-gp/GP6) | `src/io/src/guitar_pro.cpp:1449` |
| low | partial | speed | S | io-gp | gp3-5 binary reader: discarded texts that are decoded to UTF-8 and measures per track without reserve (io-gp/GP9) | `src/io/src/guitar_pro_binary.cpp:670` |
| low | partial | concurrency | M | io-midi-nativo | The native reader processes the tracks serially over an immutable yyjson document (io-midi-nativo/N4) | `src/io/src/native_format.cpp:451` |
| low | partial | speed | S | io-midi-nativo | gridSpelling queries a thread_local std::map with a tuple key for every candidate, cell and iteration (io-midi-nativo/N6) | `src/io/src/midi_import.cpp:453` |
| low | partial | memory | M | io-midi-nativo | MIDI import: temporary vectors per measure, per chord and per cell (hundreds of thousands of concurrent allocations) (io-midi-nativo/N7) | `src/io/src/midi_import.cpp:705` |
| low | partial | memory | M | io-midi-nativo | MIDI export goes through a 48-byte MidiEvent with a std::string for every note-on/off, without reserve, and computes the ticks with fractions (io-midi-nativo/N8) | `src/io/src/midi_export.cpp:277` |


## 6. Detail by subsystem

Each finding includes the cited evidence, the proposal and the verification note,
which takes precedence when it qualifies or corrects the original finding.

### App: document and controllers

#### app-documento/A1 · Every edit copies the whole song and sequences it again on the UI thread

- **Where:** `src/app/src/score_document.cpp:880`
- **Axis:** speed · **Impact:** high · **Effort:** M · **Verdict:** confirmed

```cpp
song_ = std::make_shared<const core::Song>(document_->song());
...
auto sequence = std::make_shared<const audio::Sequence>(*song_);
```

**Why it matters.** ScoreDocument::edited() runs synchronously on every key (typeDigit, deleteAtCaret, undo/redo...). With the times measured on the large song: copy 10.7 ms (line 877) + sequencing 28 ms (line 880) = ~39 ms of UI thread blocked per keystroke, that is 2-3 frames lost at 60 Hz before the layout even starts. In addition Sequence::Sequence calls core::forEachIndex twice (sequence.cpp:469 and 638), which creates and joins hardware_concurrency std::thread per call: the UI thread launches and waits for ~2xN threads per key, competing with the layout and the paint of the previous edit that are still in the QtConcurrent pool.

**Proposal.** Take the copy+sequence off the UI thread: edited() only takes the snapshot (see A6 to make it cheaper) and launches ONE worker task that builds Sequence and layout together (as openAndLayOut already does at line 167); apply() publishes sequence_ and emits songChanged when done. duration() and cursorAt() keep using the previous sequence until then, as already happens with pages_. Keep only clampCaret and historyChanged on the UI thread.

**Verification note.** Checked in src/app/src/score_document.cpp:875-894: edited() copies the song (l.877) and builds audio::Sequence (l.880) synchronously; it is called by execute() (l.869), undo/redo (l.823/830) and typeDigit (l.1121), all Q_INVOKABLE from QML, that is, the UI thread. With the measured times it is ~39 ms per keystroke on the large song (on normal songs it is negligible: the 'high' applies only to large files). The thread count is underestimated: besides sequence.cpp:469 and 638, there is a forEachIndex in every mix round (sequence.cpp:698, ~5 rounds with 21 tracks) and in core::soundingNotes (performance.cpp:118/134); parallel.hpp:41-56 creates and joins std::thread on every call, without a pool. Qualifications to the proposal: (1) AudioController::songEdited (audio_controller.cpp:64-71) decides 'updateMix or load' by comparing document->sequence() with loaded_ at the very moment songChanged is emitted; if sequence_ is published later, a signal of its own (sequenceChanged) is needed or songChanged must be emitted twice, otherwise the audio keeps the old sequence. (2) The comparison *sequence != *sequence_ (l.881) that preserves identity for ADR-0039 must also move to the worker. (3) Today results with a stale generation are discarded (l.476): if the sequence travels with the layout, typing continuously would never publish; it has to be combined with A2. (4) newSong (l.668-669) and apply (l.510-511) also sequence on the UI thread. It does not affect the audio thread or determinism.

#### app-documento/A5 · Player::load runs on the UI thread on every edit, even when playback is stopped

- **Where:** `src/app/src/audio_controller.cpp:87`
- **Axis:** concurrency · **Impact:** medium · **Effort:** M · **Verdict:** confirmed

```cpp
player_->pause();
player_->seek(0.0);
if (document_ && document_->song() && document_->sequence()) {
    player_->load(*document_->song(), *document_->sequence());
```

**Why it matters.** songEdited() is connected directly to songChanged, so it runs inside ScoreDocument::edited(), before control returns to the event loop. Player::load (player.cpp:71) distributes the 320k events per track converting them to frames (detail::eventsByTrack), computes the headroom by walking them, builds one Synth and one buffer per track (21 on the large song) and frees the previous Loaded with tidy(): on the order of milliseconds of allocation and copying per keystroke, on the UI thread, once the user has pressed play at some point (player_ != nullptr). If the user types 50 notes while paused, 49 loads are built and thrown away. Side effect: every edit rewinds to 0 (seek(0.0)) and stops playback.

**Proposal.** Lazy loading: in songEdited(), if not playing, only mark stale_=true and release loaded_; do the load in togglePlayback()/seek() when needed. If playing, prepare the Loaded in a worker (split Player::load into a 'prepare' with no shared state that returns the Loaded and a 'commit' on the control thread that does the exchange of pending_, which is already lock-free) and keep the position instead of seek(0).

**Verification note.** Checked: songChanged is connected with a direct connection to songEdited (audio_controller.cpp:36-37, same thread), which calls loadSong() (l.81-96) -> pause, seek(0) and Player::load (l.87) inside edited(). Player::load (player.cpp:71-103) distributes all the events with push_back without reserve (playback.hpp:36-50), creates one Synth and one buffer per track and frees the previous Loaded. Qualifications: it is not 'on every edit' but on every edit that changes the sequence; if the sequence is the same (mix, title) songEdited only does updateMix (l.65-69). And only since player_ exists (first play, l.102-107). The cost is not measured: by size (320k events, 21 synthesizers) it is milliseconds added to the ~39 ms of A1, 'medium' is reasonable. The rewind to 0 and the pause on edit are documented functional behaviour in the comment at l.80, not a cost: changing it is a product decision. Feasibility: lazy loading is correct if (1) the Player's duration_ is updated anyway, because seek() clamps with it (player.cpp:137-140), and (2) the Loaded held by the audio thread is not 'released': it can only be retired through the existing pending_/retired_ exchange. Separating prepare (worker) and commit (control thread) is safe: latest_ and tidy() belong to the control thread only and render() still neither allocates nor blocks; Loaded is private to Player, so an opaque type has to be exposed.

#### app-documento/A2 · relayout() without coalescing or cancellation: N quick edits = N full layouts and N live copies

- **Where:** `src/app/src/score_document.cpp:472`
- **Axis:** concurrency · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** partial

```cpp
const std::uint64_t generation = ++generation_;
setPending(+1);
QtConcurrent::run([song = song_, track = track_, view = view_] {
    return layOut(song, track, view);
```

**Why it matters.** generation_ only discards the RESULT (line 476); the stale work runs to completion. With key autorepeat (~30 Hz) and the all-tracks view (21 ms using all the cores, 132 ms of CPU) layouts are queued faster than they finish: the latency until the last edit is seen grows linearly and every queued task retains its own Song snapshot (captured shared_ptr, tens of MB on the large song). Each layoutPages also creates its own threads (core::forEachIndex) inside a thread of the global pool, the same pool where ScorePage paints one task per page: 2 overlapping layouts + painting = oversubscription of 2-3x the cores. There are also free double layouts: addTrack (lines 808-809) does execute()->edited()->relayout() and then immediately setTrack()->relayout(); the first is computed and thrown away.

**Proposal.** Single-flight with 'last one wins': if a layout is in flight, set relayoutPending_=true and do not enqueue; when the current one finishes, launch a single one with the most recent snapshot. Coalesce calls from the same event-loop turn (QTimer::singleShot(0) or a queued QMetaObject::invokeMethod) so that addTrack/newSong generate a single layout. Pass a std::stop_token/atomic<bool> to notation::layoutPages, checked between systems/pages and triggered when generation_ is incremented. Launch the layout in a dedicated 1-thread QThreadPool (it already parallelises internally) so as not to steal threads from painting.

**Verification note.** The code is as described: relayout() (score_document.cpp:466-485) increments generation_ and always launches QtConcurrent::run in the global pool; the generation only discards the result (l.476), there is no cancellation or coalescing; every task captures its shared_ptr<const Song>. ScorePage uses the same global pool (score_page.cpp:120) and layoutPages creates its own threads (page_layout.cpp:298, system_layout.cpp:593). The double layout in addTrack is real (l.808-809: execute->edited->relayout and then setTrack->relayout). newSong does NOT have it (a single relayout, l.682). What is inflated: the 'latency that grows linearly' is not supported by the measured times; today the UI thread takes >=39 ms per edit (A1) and the layout 3.5-21 ms, so the bottleneck is the UI thread and at most 1-2 layouts and 1-2 extra snapshots overlap; the unbounded queueing would only appear AFTER fixing A1. The real effect that does follow from the code is a different one: with edits faster than the layout, all the results arrive with an old generation and are discarded, so the page does not refresh until the key is released. That is why the 'single-flight + last one wins' proposal must also allow applying intermediate results, and today apply() does not allow it: with layout.song != song_ (l.508-511) it would roll song_ back and sequence on the UI thread; only pages_/pagesSong_ would have to be applied. stop_token in layoutPages and a dedicated 1-thread pool are feasible and do not affect determinism (the cancelled result is thrown away). Coalescing with singleShot(0) changes the moment of busyChanged/layoutChanged: check the tests that wait for those signals.

#### app-documento/A9 · Startup: Bravura.otf is loaded twice and Bravura.json (1.2 MB) is parsed lazily on the first open

- **Where:** `src/app/src/main.cpp:203`
- **Axis:** memory · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Bravura.otf"));
```

**Why it matters.** main() registers the font synchronously on the UI thread before loading the QML, and ScoreDocument::painter() (score_document.cpp:496, ScoreFonts::fromDirectory in score_painter.cpp:258-261) reads the whole Bravura.otf again into a QByteArray of its own together with Arimo: two copies of the music font resident for the whole session. musicMetrics() (score_document.cpp:77-90) reads and parses the 1.2 MB JSON inside the first layOut, so that cost is added to the time until the first page of the first file opened; and since placeCaret (line 1085) and placeCaretBox (line 1248) also call musicMetrics() from the UI thread, a key or click during that first layout blocks the UI waiting for the static initialisation.

**Proposal.** Warm up in parallel with engine.loadFromModule: right after creating QGuiApplication, QtConcurrent::run([]{ musicMetrics(); ScoreDocument::painter(); }) (expose a static ScoreDocument::warmUp()). Share the buffer: read Bravura.otf only once and register with QFontDatabase::addApplicationFontFromData(fonts.music) the same QByteArray that ScorePainter uses. Optional: precompile at build time the metrics that the layout uses into a binary/constexpr table so as not to parse 1.2 MB of JSON on every startup.

**Verification note.** Checked: main.cpp:203 registers :/fonts/Bravura.otf synchronously before loading the QML, and ScoreDocument::painter() (score_document.cpp:495-498) reads Bravura.otf and the two Arimo again into QByteArrays of its own (score_painter.cpp:54-57, 258-262): there are two copies of the music font in memory. musicMetrics() (l.76-92) is a lazy static that reads and parses Bravura.json inside the first layOut (l.125), in the worker; placeCaret (l.1085) and placeCaretBox (l.1248) call it from the UI thread, so a key during that first layout waits for the initialisation (or the UI thread itself does it after newSong if it arrives before the worker). It is all a one-time, unmeasured cost: the duplicated copy is hundreds of KB and the parse is paid once per session; 'low' is correct. The sizes (1.2 MB) cannot be checked: the fonts come from ELARA_FONT_DIR, outside the repo. The proposal is feasible with two conditions: the font registration must still complete before engine.loadFromModule (the logo and the UI symbols use it), so the warmUp in a worker only serves musicMetrics() and the painter, not addApplicationFontFromData; and sharing the QByteArray works because it is implicitly shared. Precompiling the metrics at build time is no longer effort S.

#### app-documento/A3 · songChanged is too coarse: every note rebuilds tracks(), the track Repeater and the combo

- **Where:** `src/app/src/score_document.cpp:891`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
clampCaret();
emit caretChanged();
emit songChanged();
emit historyChanged();
relayout();
```

**Why it matters.** songChanged is the NOTIFY of 13 properties (score_document.hpp:53-73: title, artist, trackNames, measureCount, duration, tempo, time signature/key signature, tracks...). After every key QML re-evaluates all of them. tracks() (line 569) rebuilds a QVariantList with a 12-key QVariantMap per track and calls trackTuning(), which tries the 15 presets building core::Tuning::preset(id) on each turn (lines 628-629): 20 tracks x 15 = 300 Tuning constructions + hundreds of QVariant/QString per edit. Worse: SongPane.qml:336 uses 'model: root.document.tracks' (JS array), so the Repeater destroys and recreates the 20 rows (sliders, buttons) on every note, and SongPane is always instantiated (Main.qml:463, only width 0 when hidden), so this is paid even when the panel is closed. Main.qml:354 '[qsTr("All tracks")].concat(score.trackNames)' also replaces the combo's model. Estimate: several ms of UI thread per key (20 complex delegates), added to A1. In addition caretChanged (9 NOTIFY properties) is emitted 2 times per edit: here and in placeCaretBox() (line 1260), which always emits even if the page and rectangle do not change.

**Proposal.** Split the signal: infoChanged (title/artist), signaturesChanged, tracksChanged, durationChanged, and emit each only if the value changed (keep the previous QStringList/descriptor and compare; a typed note changes none except duration). Expose the tracks as a QAbstractListModel with dataChanged per row/role instead of a QVariantList. Cache the preset that matches each tuning (or compare against a static table of already-built Tuning). In placeCaretBox() emit caretChanged only if caretPage_/caretRect_/caret_ change, and remove the duplicate emit from edited(). Wrap the content of SongPane in Loader { active: shown }.

**Verification note.** True: emit songChanged in edited() (l.891); it is the NOTIFY of 12 properties, not 13 (score_document.hpp:53-57 and 67-73); tracks() (l.569-594) rebuilds a 12-key QVariantMap per track; SongPane is always instantiated (Main.qml:463-472, width 0/visible false) with 'model: root.document.tracks' (SongPane.qml:336); Main.qml:354 concatenates trackNames. Inflated or not demonstrable: (a) '300 Tuning constructions' is an upper bound: trackTuning stops at the first preset that matches (l.628-632; 'guitar' is the first) and tracks without strings exit earlier (l.624); Tuning is a fixed-capacity type with no allocation and preset() parses a string of ~20 characters: microseconds in total. (b) The key claim, that the Repeater destroys and recreates the 20 rows on every note, cannot be confirmed from the repo's code and is probably false: QQuickRepeater::setModel and QQuickComboBox::setModel return without doing anything if the new QVariant is equal to the previous one, and a QVariantList of maps with the same content compares equal; a typed note does not change name/mix/tuning. The recreation does happen when changing mix, name or tracks (once per action). It must be measured (a counter in the delegate's Component.onCompleted) before investing in a QAbstractListModel. Residual cost per key: 12 bindings + building and comparing the list, far below 1 ms. (c) caretChanged is not emitted twice in the same turn: once in edited() (l.890) and another deferred in apply()->placeCaretBox() (l.532, l.1260) when the layout arrives; the second is necessary (caretRect changes). It is true that placeCaretBox emits without comparing. Proposal: do NOT remove the emit from edited(): caretStatus/caretFretted/caretMeasure read document_ and change immediately, and if the layout is discarded because of its generation there would be no notification at all; better to guard the placeCaretBox emit with a comparison. Loader{active: shown} would break the closing animation (Behavior on width): use active: shown || width > 0.

#### app-documento/A4 · To find out whether it 'sounds the same', another full Sequence is built and compared element by element; the mix also triggers a relayout

- **Where:** `src/app/src/score_document.cpp:881`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
if (!sequence_ || *sequence != *sequence_) {
    sequence_ = std::move(sequence);
}
```

**Why it matters.** Releasing a fader (setTrackMix), or changing the title or artist (SetSongInfo), pays the 10.7 ms of copy + 28 ms of sequencing just to discover, with a default operator== over 160k PlayedNote + 320k NoteEvent + measures (sequence.hpp:132), that nothing changed, and to throw the result away. During the comparison two Sequence are alive at once (double memory peak: hundreds of thousands of duplicated events). Afterwards, line 893 launches an unconditional relayout(): a volume/pan/mute/solo change recalculates all the pages (21 ms x all the cores) to obtain identical display lists that ScorePage then discards by comparing.

**Proposal.** Have core::Command declare what it invalidates (bitmask: Playback, Layout, Mix, Info) and have Document expose it for the last operation applied/undone. edited() then: Mix -> neither sequence nor relayout (only updateMix); Info -> relayout without sequencing; the rest -> both (in a worker, see A1). The deep comparison and the second temporary Sequence are eliminated. If the comparison is to be kept as a safety net, do it in the worker.

**Verification note.** True in the code: edited() always builds a new Sequence and compares it with the default operator== (score_document.cpp:880-883; sequence.hpp:24/44/65/132), and calls relayout() unconditionally (l.893); notation does not read the mix (no reference to mix/mute/solo/audible in src/notation), so the layout after a mix change produces identical pages that ScorePage discards by comparing (score_page.cpp:111). Inflated impact: it happens once per discrete action, not per frame or per key; the fader drag goes through previewTrackMix (l.954-967, without copy or sequence) and setTrackMix only runs on release or when pressing mute/solo (SongPane.qml:487-522) and returns if the mix does not change (l.907). It is ~40 ms + one wasted layout per gesture on the large song; the comparison itself is a linear walk over vectors (a few ms) and the double memory peak is transient. A title/artist change does need a relayout (the title is drawn), only the sequencing is superfluous. The proposal is feasible but risky: the deep comparison is today the guarantee of ADR-0039 that playback continues only if it sounds the same; a mask declared by each core::Command (and by undo/redo and composite commands) that is set wrongly leaves stale audio with nothing to detect it. It is advisable to keep the comparison as an assertion in debug/tests. Even with Mix, song_ must be refreshed for tracks().

#### app-documento/A6 · Three or more full copies of core::Song alive at once; on open, the copy is made on the UI thread

- **Where:** `src/app/src/score_document.cpp:502`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** L · **Verdict:** partial

```cpp
if (layout.opened) {
    document_.emplace(*layout.song);
```

**Why it matters.** The worker already has the song (line 159) but apply() copies all of it on the UI thread on open: 10.7 ms blocked just when the first page is about to be shown. In editing mode the following coexist: document_ (the editable one), song_ (snapshot, line 877), pagesSong_ (snapshot of the last layout applied, different from song_ until the relayout finishes, header line 340 and line 527) and one snapshot for every relayout in flight (capture at line 472). Notes and beats alone are 160k x 48 B + 80k x 64 B = ~12.8 MB per copy, plus vectors and heap headers: 3-4 copies = 50-80 MB transient when typing fast on the large song, in addition to the 10.7 ms of copy per key from A1.

**Proposal.** Short term (S): in openAndLayOut also build the editable copy in the worker and move it (ScoreLayout with an editable core::Song; document_.emplace(std::move(...))), and with A2 limit to one layout in flight to bound the snapshots to 3. Fundamental (L): structural sharing in the model: have Staff hold std::shared_ptr<const Measure> (or Track via COW) so that the snapshot is copying ~10k pointers (tens of microseconds) and only the edited measure is duplicated; it eliminates both the 10.7 ms cost per edit and the duplicated copies.

**Verification note.** True: apply() does document_.emplace(*layout.song) (score_document.cpp:502), a full copy on the UI thread on open (core::Document receives Song by value, document.hpp:45). But it is 10.7 ms only once per open, negligible compared with reading and laying out the file. The count of copies is inflated: at rest pagesSong_ and song_ are THE SAME shared_ptr (apply assigns both from layout.song, l.509 and 527), so there are 2 copies (document_ + snapshot), 3 only while a relayout is in flight after an edit, and more only if layouts overlap (see A2, bounded today by the UI thread). The sizes of 48 B/64 B per note/beat cannot be verified without building; tens of MB transient only on the giant song. The real benefit of structural sharing (removing 10.7 ms of copy per key) is already counted in A1. Short proposal: feasible, but ScoreLayout reaches the QFuture continuation as const& (l.453); the editable copy must be carried in a non-const std::shared_ptr<core::Song> and moved from the pointer; putting a Song by value in ScoreLayout would add copies. Long proposal (shared_ptr<const Measure>/COW): correct if every mutation clones before writing; it touches core and every Command (apply/revert), real effort L and high risk for the gain.

#### app-documento/A7 · Playback cursor driven by a precise 16 ms QTimer, not by vsync, and with a QVariantMap + linear search per tick

- **Where:** `src/app/src/audio_controller.cpp:16`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
transportTimer_.setInterval(16ms);
transportTimer_.setTimerType(Qt::PreciseTimer);
```

**Why it matters.** 16 ms is 62.5 Hz against a 60 Hz display: the cursor repeats or skips a step periodically (beating), and on 120/144 Hz displays it updates on fewer than half of the frames; PreciseTimer on Windows also forces a 1 ms timer resolution while sound is playing. Every tick emits positionChanged and QML re-evaluates 'document.cursorAt(cursorSeconds)' (ScoreView.qml:27-28): score_document.cpp:377 calls notation::cursorAt, which walks all the pages and their MeasureBox from the first one (cursor.cpp, double for; up to 500 boxes per tick at the end of the song) and returns a QVariantMap of 4 QString keys (lines 382-385) that is converted into a JS object: 4 QString + 4 QVariant + map node allocated 62 times per second, plus the 4 bindings of the playhead, the clock text (Main.qml:632) and the slider (Main.qml:654).

**Proposal.** Drive the cursor from the frame: a FrameAnimation in ScoreView (or QQuickWindow::afterAnimating) that reads audio.position on demand while playingSong, and leave a coarse timer (100-250 ms) only for the clock/slider and for detecting the end. In ScoreDocument, build in apply() a measure -> (page, MeasureBox*) index (a vector of measureCount entries) so that cursorAt is O(log onsets), and return a Q_GADGET or properties (cursorPage:int, cursorRect:QRectF) instead of a QVariantMap. Emit the clock only when the whole second changes.

**Verification note.** True: transportTimer_ of 16 ms with Qt::PreciseTimer (audio_controller.cpp:16-17); ScoreView.qml:27-28 re-evaluates document.cursorAt(cursorSeconds) on every positionChanged; cursorAt returns a 4-key QVariantMap (score_document.cpp:382-385) and notation::cursorAt does a linear double for over pages and MeasureBox (cursor.cpp:35-41). Inflated impact: the timer only runs while sound is playing (it stops in pollTransport, l.164-166, and positionChanged is only emitted if the position changes, l.156); playedMeasureAt is already a binary search (sequence.cpp:735) and so is xAt (cursor.cpp:21); walking <=500 boxes and allocating a 4-entry map 62 times per second are microseconds compared with paints of 5-19 ms. What remains is visual quality (62.5 Hz against vsync), not performance. The 1 ms timer on Windows is Qt behaviour, not verifiable in the repo, and only during playback. An important qualification to the proposal: Player::position() returns position_, which the audio thread publishes only at the end of each render() (player.cpp:146-150, 189): it advances in jumps of one audio block. A FrameAnimation (available, the project requires Qt 6.12) reading that position per frame does not give a smoother cursor unless it is interpolated on the UI thread from the last position and its instant; reading the atomic per frame is safe for the audio thread. The measure->box index and the Q_GADGET are correct but the gain is imperceptible.

#### app-documento/A8 · Pitch detection (YIN) runs on the UI thread inside the 30 ms poll

- **Where:** `src/app/src/tuner_controller.cpp:176`
- **Axis:** concurrency · **Impact:** low · **Effort:** M · **Verdict:** partial

```cpp
for (std::size_t got = input_->read(block_); got > 0; got = input_->read(block_)) {
    changed = tuner_->push(std::span(block_).first(got)) || changed;
}
```

**Why it matters.** Tuner::push (tuner.cpp:28-45) shifts the window and calls detector_.detect(window_) every time there is half a new frame, all on the main thread: the tuner shares a thread with the edits (A1 blocks ~40 ms) and with the QML render, so the needle stutters when the UI is working and vice versa. After a UI-thread block the loop drains the whole backlog of the ring with several consecutive pushes, each with its full detect, just when the thread is already late; only the last window yields a useful reading. showReading() (line 220) also notifies 6 properties and reassigns noteName_ (a new QString) even if the note has not changed.

**Proposal.** Move Tuner + polling to a QObject in a QThread of its own (or to a thread that waits on the ring) and publish the reading by a queued signal with a small struct; the UI thread only paints. In the meantime: in poll() accumulate the backlog and do a single push with the last frameSize samples (push already takes the tail when samples.size() >= size), and emit readingChanged only if note/cents change above a threshold (e.g. 0.5 cents).

**Verification note.** True: poll() (tuner_controller.cpp:174-190) runs with a 30 ms QTimer on the UI thread (l.82-83) and Tuner::push (tuner.cpp:28-46) calls detector_.detect for every half new frame. The cost is low: YIN with early exit at the first minimum (pitch_detector.cpp:83-96); worst case ~maxLag x window = 1600 x 1600 operations at 48 kHz, on the order of 1 ms every ~33 ms. What does not hold: 'several consecutive pushes, each with its detect' after a block. block_ has 8192 samples (l.149) and frameSize at 48 kHz is ~3201, so a backlog of up to ~170 ms comes out in a single read -> a single push -> a single detect (push already keeps the tail when samples.size() >= size, tuner.cpp:30-32). There would only be several detects with larger backlogs, bounded by the input ring. Therefore the proposed mitigation ('a single push with the last samples') in fact already happens. readingChanged is the NOTIFY of 7 properties (tuner_controller.hpp:27-33), not 6, and showReading emits without comparing (l.208-221): true but cheap. Moving the tuner to a thread of its own is feasible (a single consumer of the ring; setOptions and takePeak must go to that thread), but the stutter cited is caused by A1, not by the tuner: fixing A1 removes almost all the interest in this. Filtering readingChanged by a threshold is correct and trivial.

**Observations on this area (what is already well solved).**

Scope: read-only (nothing was edited, built or run). Files read in full: score_document.cpp/.hpp, audio_controller.cpp, tuner_controller.cpp, main.cpp, startup.cpp, score_page.cpp; read by sections: player.cpp, sequence.cpp/.hpp, caret.cpp, cursor.cpp, tuner.cpp, parallel.hpp, document.cpp and the QML files that consume the document (Main.qml, ScoreView.qml, SongPane.qml).
Well solved, leave alone:
- Opening a file: reading, Sequence and layout are done in the worker (openAndLayOut) and generation_ prevents an old result from overwriting a new one (also in newSong, line 666).
- ScoreDocument::page() returns a shared_ptr with aliasing over the page vector: a relayout cannot free a page that is half painted. ScorePage keeps the texture if the display list did not change.
- Player: lock-free song exchange (atomic pending_/retired_), the old Loaded is freed by the control thread in tidy(), updateMix writes atomic levels; no allocations or locks were found in render()/renderBlock(). No data races were found in the files of this area: all the state of ScoreDocument/AudioController is touched only from the UI thread.
- transportTimer_ stops only when sound stops (pollTransport) and the 50 ms pollTimer_ only lives during the test tone; there are no idle timers.
- TunerController reuses block_ (8192 samples, no allocation per poll), the microphone read is a noexcept ring and the peak is an atomic; the microphone is closed when the tuner is deactivated.
- musicMetrics() and painter() are statics with thread-safe initialisation and are built only once.
- startup.cpp is only the smoke-test/capture harness: the Startup object and its 30 ms timer exist only with --smoke-test/--screenshot/--track/--view/--seek and the timer stops when the steps finish; it adds no cost to a normal startup.
- placeCaretBox/clampCaret/caretAt are small linear searches (measure boxes and columns of one page): microseconds per key; the problem there is the extra emitted signal (A3), not the computation. Sequence::secondsAt is linear but is only called on a click.
Suggested order of attack: A2 (coalesce) and A4 (classify commands) have the best impact/effort ratio; A1 depends on deciding whether songChanged can be asynchronous; A6 (structural sharing) is the fundamental change that removes the 10.7 ms per edit at the root.

### App: painting and QML

#### app-pintado-qml/P2 · Stale paints are not cancelled (only their result is discarded) and a page not yet painted launches a full paint on every size change

- **Where:** `src/app/src/score_page.cpp:120`
- **Axis:** concurrency · **Impact:** medium · **Effort:** M · **Verdict:** confirmed

```cpp
QtConcurrent::run([page = std::move(page), scale, ratio] {
        return ScoreDocument::painter().render(*page, scale, ratio);
```

**Why it matters.** The only staleness control is `if (ticket != ticket_) { return; }` (line 123): the task already queued or running in the global QThreadPool runs to completion, allocates its full QImage (3.6-134 MB, see P1) and retains the old layout with its shared_ptr, only to throw it away at the end. Two paths trigger it: (a) `schedule()` (lines 88-92) only uses the 120 ms timer `if (painted_)`; while the first paint has not come back it does an immediate `repaint()`, so every page that enters during the zoom animation (Behavior on shownZoom, 180 ms, ~11 frames with a different size in each one) queues one paint per frame until the first arrives; (b) when zooming in jumps with pauses >120 ms, or typing fast on a visible page, every generation queues 3-6 pages while the previous one is still painting. At high zoom that is tens of ms and tens of MB per useless task, and since the queue is FIFO they delay precisely the paint that will be shown. In addition the pages of the cacheBuffer compete with the visible one at the same priority. Aggravating factor: the layout does not use that pool but new `std::thread` per call up to hardware_concurrency (src/core/include/elara/core/parallel.hpp:41), so a relayout that coincides with paints leaves ~2N runnable threads on N cores.

**Proposal.** 1) Keep in ScorePage a cancellation token per paint (std::shared_ptr<std::atomic_bool> or QPromise/QFuture::cancel) that `repaint()` sets before launching the next one and that `ScorePainter::paint` checks between phases (lines, beams, every N glyphs, texts) and before allocating the QImage. 2) In `schedule()`, if there is a paint in flight (pending ticket) start `settle_` instead of repainting right away, even if `painted_` is false: a single initial paint and the rest coalesced. 3) Launch with `QThreadPool::start(runnable, priority)` giving more priority to the pages that cut the viewport than to those of the buffer, or use a paint pool of its own with a bounded maxThreadCount so as not to overlap with the layout threads.

**Verification note.** Checked in score_page.cpp:87-93 (schedule only uses settle_ if painted_), :119-133 (ticket; the stale result is discarded at :123 but the task runs to completion and allocates its QImage) and :100/score_document.cpp:492 (the aliased shared_ptr retains all the pages of the old layout). Path (a) is even worse than described: since every frame with a different size increments ticket_, ALL the previous results are discarded and painted_ stays false, so a newly created page paints once per frame during the whole animation or gesture (not only 'until the first arrives') and shows nothing until the size settles. Concrete scenario: Main.qml:118-121 calls fitPaper() on sourceChanged, which is emitted (score_document.cpp:514) before layoutChanged (:530); when opening another file with a different zoom, the new delegates are born in the middle of the 180 ms animation: ~11 paints per page, 10 thrown away, and the first visible paint is delayed. Qualifications: there are no duplicates at the same scale (the comparison at :111 filters them, also in flight); reused delegates have painted_=true and go through settle_; in (b) when typing only the pages that change are repainted, not 3-6; everything happens in the pool, not on the UI thread. The thread aggravating factor is true (page_layout.cpp:286-298 and system_layout.cpp:593 use core::forEachIndex, parallel.hpp:41-44 creates std::thread per call, and the layout also runs inside QtConcurrent::run), but it lasts 3.5-21 ms. Proposal feasible: (2) is the most profitable and effort S, but an 'in flight' state is needed that does not exist today (ticket_ does not say whether anything is pending); an even simpler alternative: accept a stale result while painted_ is false and show it stretched. (1) correct with atomic_bool or the QtConcurrent::run overload with QPromise; (3) requires QThreadPool::start with priority and ScorePage knowing its visibility. Nothing touches the audio thread.

#### app-pintado-qml/P1 · Whole-page texture: memory grows with zoom² x dpr² and the cacheBuffer keeps whole off-screen pages alive

- **Where:** `src/app/src/score_page.cpp:24`
- **Axis:** memory · **Impact:** medium (initially proposed: high) · **Effort:** L · **Verdict:** partial

```cpp
constexpr double kMaxPixels = 32.0 * 1024 * 1024;
```

**Why it matters.** Each ScorePage paints the COMPLETE page at device resolution into an ARGB32 QImage (4 B/px, score_painter.cpp:292) and uploads it as a single texture. A4 at 96 ppi = 794 x 1123 logical px. MB per page: zoom 1/dpr 1 = 3.6 MB; zoom 1/dpr 2 (or zoom 2/dpr 1) = 14.3 MB; zoom 4/dpr 1 = 3175 x 4490 = 57 MB; zoom 4/dpr 1.5 = 128 MB; zoom 4/dpr 2 hits the cap kMaxPixels = 134 MB (and already looks blurry). If the 21 pages were all alive it would be 75 MB / 300 MB / 1.2 GB / 2.8 GB; they are not, because the ListView virtualises, but ScoreView.qml:279 sets `cacheBuffer: Math.max(0, Math.round(root.pageHeight))`, which grows with zoom: at zoom 4 there are always 3-4 live delegates (1-2 visible + previous and next), that is 170-230 MB of texture at dpr 1 and 400-540 MB at dpr 2 to show a viewport that only needs ~8 MB (1920x1080) or ~33 MB (dpr 2). In addition every repaint after the zoom settles paints those 3-4 whole pages again (the measured time of 5-19 ms scales with the pixels: x16 at zoom 4) and during the handover a CPU QImage + old texture + new texture coexist per page; uploading 57-134 MB per texture happens on the render thread and costs frames.

**Proposal.** Short step (S): bound the buffer to the view, e.g. `cacheBuffer: Math.round(Math.min(root.pageHeight, list.height))`, so that at high zoom there are no whole preloaded pages. Fundamental step (L, already noted in the comment on kMaxSide and in ADR-0015): tiles. Have ScorePage receive the visible rectangle (mapRectFromItem of the ListView viewport) and paint only tiles of e.g. 1024x1024 device px that cut it plus a margin ring, with one child QSGNode per tile and an LRU with a cap in MB; `ScorePainter::render` already admits a shifted origin (painter.translate) and a clip would be enough to add. With that the memory is bounded by the viewport (~10-40 MB) at any zoom, the 8192 px cap disappears and the repaint after a zoom costs the visible part, not the page. Complementary alternative: an 8-bit texture (ink as alpha) with a material of its own, 4x less memory.

**Verification note.** Checked: score_page.cpp:23-24 (kMaxSide 8192, kMaxPixels 32 Mi), score_painter.cpp:292 (ARGB32_Premultiplied, whole page), ScoreView.qml:17 (maxZoom 4), :279 (cacheBuffer = pageHeight) and A4 with staffSpace 1.75 mm (score_document.cpp:58-71). The numbers add up: 3.6 MB at zoom 1/dpr 1, 57 MB at zoom 4, 128 MB at zoom 4/dpr 1.5 and a cap of 134 MB. With cacheBuffer = pageHeight at high zoom 3-4 delegates stay alive (the previous and next page always cut the buffer). Qualifications that lower the impact: (1) it is bounded by design (a per-page cap + virtualisation, ADR-0015:38-40 and :67-69 already document it as a known limitation); the real worst case is ~4 x 134 MB and only at zoom >= 2 on HiDPI displays; at zoom 1 it is 11-15 MB. (2) The 'x16 at zoom 4' is inflated: docs/performance.md:86-87 measures 5.4 ms at 100%, 8.5 ms with 4x pixels and 19 ms at 200%, that is, it scales sublinearly with the pixels. (3) Painting is on a worker thread (latency, not stutter); what does touch frames is the texture upload on the render thread. Proposal: the short step `cacheBuffer: Math.round(Math.min(root.pageHeight, list.height))` is feasible and correct (in exchange, at high zoom the neighbouring pages will appear blank more often because painting takes longer there). Tiles: feasible, but `render()` today accepts neither origin nor clip (it only does translate(0,-top) at :296); the signature must be extended using `paint()`. The 8-bit texture requires a material of its own and a review of the knockouts, which paint a fixed Qt::white (score_painter.cpp:203). It does not affect determinism or the audio thread.

#### app-pintado-qml/P5 · Delegate recycling: the reused page shows another page's texture until its paint arrives, and the pooled items keep repainting

- **Where:** `src/app/src/score_page.cpp:67`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
index_ = index;
    emit indexChanged();
    repaint();
```

**Why it matters.** With `reuseItems: true` (ScoreView.qml:277) the same ScorePage receives another `index`. `setIndex` launches the asynchronous paint but `painted_` stays true and the node keeps the previous texture, so during the 5-19 ms (much more at high zoom) the delegate shows the content of ANOTHER page at opacity 1; it normally happens inside the cacheBuffer and is not seen, but when dragging the scroll bar or with cursor following at high zoom it is. In addition an item in the pool stays connected to `layoutChanged` (line 55): every relayout makes it compare and, if its page changed, paint and upload a texture that nobody sees. And if its index ends up outside the new count, `repaint()` exits at line 101-103 without releasing `shown_`, which, being a shared_ptr with aliasing (score_document.cpp:492), retains ALL the display lists of the previous layout until the item is destroyed or reused.

**Proposal.** In `setIndex`, invalidate what is shown: `shown_.reset()`, `painted_ = false` with `emit paintedChanged()` (the existing white paper and the fade cover the gap) and mark the node to release the texture in the next updatePaintNode. Handle `ListView.onPooled`/`onReused` in the delegate (or an `active` property on ScorePage) to disconnect from `layoutChanged`, release the texture and `shown_` when entering the pool and repaint when leaving it. In the early exit for a nonexistent page, also do `shown_.reset()`.

**Verification note.** Checked: ScoreView.qml:277 `reuseItems: true`; setIndex (score_page.cpp:61-68) only calls repaint(), which is asynchronous; painted_ is not reset and the node keeps the previous texture (updatePaintNode :136-152 only changes it when image_ arrives), so the reused delegate shows another page at opacity 1 until the paint arrives (ScoreView.qml:331). The connection to layoutChanged (:55) is not cut when entering the pool: there are no pooled/reused handlers. Corrections: (1) the retention of shown_ for an out-of-range index (:101-103) is today unreachable in practice: if the count changes, the whole model is replaced and the delegates are destroyed (P3); it will only matter if P3 is adopted. (2) The life of an item in the pool is short (Qt drains it), so the repaints 'that nobody sees' are marginal; not verifiable without Qt sources. Low impact is correct: it is mostly a visual defect of 1-2 frames, greater at high zoom. Proposal feasible (effort S), with two warnings: setting painted_=false in setIndex puts the page on the immediate per-frame paint path of P2(a) if it coincides with a zoom, and every reused page will go through blank paper + a 160 ms fade, which during a fast scroll may be more noticeable than the current fault; it is advisable to hide it without animation.

#### app-pintado-qml/P6 · Playback cursor: new QVariantMap, linear search and five bindings plus a JS handler per tick of a 16 ms QTimer that is not synchronised with vsync

- **Where:** `src/app/qml/ScoreView.qml:27`
- **Axis:** speed · **Impact:** low · **Effort:** M · **Verdict:** confirmed

```cpp
readonly property var cursor: cursorSeconds >= 0 && document.pageCount > 0
                                  ? document.cursorAt(cursorSeconds) : ({})
```

**Why it matters.** `audio.position` changes every 16 ms (audio_controller.cpp:16, `transportTimer_.setInterval(16ms)`). On every tick: `ScoreDocument::cursorAt` builds a QVariantMap with 4 QString keys (score_document.cpp:382) that is converted to a JS object; `notation::cursorAt` linearly walks the MeasureBox of all the pages until it finds the measure (src/notation/src/cursor.cpp:35-37, up to 500 comparisons at the end of the song); `cursorShown`, x/y/height of the playhead are re-evaluated, `onCursorChanged` runs (lines 220-239) and so do the clock `window.clock(audio.position)` and the Slider in Main.qml (632, 654). On the CPU that is tens of µs per tick, little; what is noticeable is something else: a 16 ms QTimer (62.5 Hz) is not in phase with the display, so at 60 Hz there are repeated or skipped frames (the cursor jitters) and on 120/144 Hz displays the cursor advances at half the cadence. It also generates garbage for the QML GC at 60 objects/s.

**Proposal.** Move the sampling to the frame: a `FrameAnimation { running: audio.playingSong; onTriggered: ... }` (Qt 6.4+) in ScoreView that reads the player position once per frame, or interpolate between ticks. Expose the cursor as typed C++ properties (cursorPage: int, cursorRect: QRectF with its signal, emitted only if it changes) instead of a `var` with a map, and remember in ScoreDocument the last (page, MeasureBox) found to check it first (O(1) except when changing measure; measure->page index built in `apply()`). The clock can be bound to `Math.floor(audio.position)` so it is evaluated once per second.

**Verification note.** Checked: audio_controller.cpp:16-17 (16 ms QTimer, PreciseTimer), :149-159 (emits positionChanged only if it changes), Main.qml:447/632/654, ScoreView.qml:27-28 and 220-239, score_document.cpp:367-386 (4-key QVariantMap per call), cursor.cpp:35-37 (linear search). The CPU cost is negligible and the finding acknowledges it; the linear search part is superfluous: docs/performance.md:101 measures 0.37 µs for an equivalent search over the 21 pages ('nothing to gain'), and playedMeasureAt is already binary (sequence.cpp:735), so the O(1) cache and the measure->page index are not worth it. The only thing of value is the 62.5 Hz offset against vsync. An important qualification for the proposal: Player::position() (player.cpp:146-150) reads an atomic of frames that the audio thread updates per block, so sampling per frame with FrameAnimation without interpolation would still advance in jumps the size of the audio buffer; it is necessary to extrapolate from (last position, timestamp). Reading that atomic from the UI is safe and neither blocks nor allocates on the audio thread; the call to player_->tidy() must stay on the UI thread (pollTransport). FrameAnimation is available (the project requires Qt 6.12, src/app/CMakeLists.txt:3); check that the smoke test with QSG_RENDER_LOOP=basic still passes.

#### app-pintado-qml/P7 · The page texture is created with an alpha channel even though it is opaque: full-page blending and three layers of overdraw per page

- **Where:** `src/app/src/score_painter.cpp:292`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
```

**Why it matters.** The image is filled with opaque white, but since it has an alpha format `window()->createTextureFromImage(image_)` (score_page.cpp:145, without options) produces a texture marked with alpha: the scene graph draws it with blending and outside the opaque batch, on top of the white `paper` Rectangle of the same size (ScoreView.qml:318-325) and, in dark theme, under another translucent full-page Rectangle (342-347). That is 2-3 fill-rate passes per page pixel on every frame while there is animation (cursor during playback, scroll, zoom). Irrelevant on a dedicated GPU, noticeable on integrated graphics with a 4K window and high zoom.

**Proposal.** Paint in `QImage::Format_RGB32` (same size, Qt's rasterizer is equal or faster without alpha) or pass `QQuickWindow::TextureIsOpaque` when creating the texture, so that the node goes into the opaque batch and the paper underneath is discarded by depth. For the dark theme, move the tint to the node itself (material colour or a QSGNode with the factor) instead of a translucent Rectangle on top; the white `paper` can remain only as a placeholder while `painted` is false.

**Verification note.** Checked: score_painter.cpp:292-293 (ARGB32_Premultiplied filled with opaque white), score_page.cpp:145 (`createTextureFromImage(image_)` without options) and the per-page stack in ScoreView.qml:309-347 (RectangularShadow, Rectangle paper with radius, ScorePage and, in dark theme, a translucent Rectangle). `QQuickWindow::TextureIsOpaque` exists in the installed Qt (qquickwindow.h:65); that without this option the texture inherits the alpha of the format and is drawn with blending is known Qt behaviour, not verifiable here for lack of sources. Qualifications: the overdraw is bounded by the viewport (the ListView clips with clip:true, :276), it does not grow with zoom, so 'noticeable at high zoom' is inflated; it only counts when the scene is redrawn (playback, scroll, zoom). In favour: an opaque page would also discard by depth the fragments of the shader shadow that remain underneath, not only the paper. The TextureIsOpaque proposal (or Format_RGB32) is one line and correct: everything is painted source-over on opaque white, the alpha stays at 255, and during the fade-in Qt still blends by the item's opacity. Moving the dark tint to the node is NOT effort S: QSGImageNode does not modulate colour (a material of its own would be needed), or else paint with a paper colour according to the theme, which forces changing the fixed Qt::white of the knockouts (score_painter.cpp:203) and repainting when the theme changes.

#### app-pintado-qml/P3 · The ListView model is an integer: when the number of pages changes, all delegates and their textures are destroyed

- **Where:** `src/app/qml/ScoreView.qml:272`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
model: root.document.pageCount
```

**Why it matters.** With a numeric model, a different value is a new model: QQuickItemView::setModel empties the view (clear + position to the start) and QQmlDelegateModel removes all the rows and inserts the new ones. An edit that takes the song from 21 to 22 pages (or the other way round) cancels precisely ScorePage's optimisation of keeping the texture of the pages that do not change (`*page == *shown_`, score_page.cpp:111): all the live textures are freed, the delegates are recreated, all the visible and buffer pages are repainted (3-6 paints of 5-19 ms each instead of 1) and each one reappears from the blank paper with the 160 ms fade, that is, a perceptible flicker; the scroll position depends on `revealCaret` or cursor following recovering it. It was not run: the scroll jump should be confirmed in the app, the destruction of delegates is deduced from Qt's documented behaviour with integer models. When the count does not change nothing happens (the binding is re-evaluated with layoutChanged but the value is equal).

**Proposal.** Expose in ScoreDocument a minimal QAbstractListModel (rowCount = pages) that in `apply()` emits beginInsertRows/beginRemoveRows only for the tail when the count changes, and use it as `model`. The existing delegates survive, ScorePage compares and keeps its texture, and only the new page is created or destroyed. As a side benefit, `pageCount` can have its own signal instead of hanging from `layoutChanged`.

**Verification note.** The code exists: ScoreView.qml:272 `model: root.document.pageCount`, and pageCount notifies with layoutChanged (score_document.hpp:61). That a different integer is equivalent to a new model (QQuickItemView::setModel does clear() and repositions at the start) is known Qt behaviour, but it could not be verified here: the local Qt 6.12.1 installation ships no sources, and the code was not built or run when the study was written. Impact inflated as a performance finding: it only happens when an edit changes the number of pages (infrequent: overflowing or emptying the last page, inserting or removing measures at the boundary); the cost is 3-6 asynchronous paints of 5-19 ms plus recreating a few delegates, similar to a zoom settling. When opening a file or changing track or view, the full repaint is legitimate. What matters is the visible effect (fade from white and a possible scroll jump to the start, which revealCaret corrects with a 160 ms animation): it must be confirmed in the app before investing effort M. Proposal correct and feasible; take care of the order in apply() (begin/endInsertRows or RemoveRows around `pages_ = layout.pages`, score_document.cpp:526) and use a model reset when opening another song. Note: adopting it makes reachable the leak of shown_ for an out-of-range index described in P5.

#### app-pintado-qml/P4 · Every page paint recreates the fonts from their bytes and extracts all the outlines again: the outline cache lives only within one call

- **Where:** `src/app/src/score_painter.cpp:130`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
Outlines outlines(QRawFont(musicFont, kStaffSpacesPerEm * scale, QFont::PreferNoHinting));
```

**Why it matters.** `Outlines` is a local variable of `paintGlyphs`, and in `paintTexts` (lines 158-160) the same happens with `const QRawFont upright = textFont(fonts.text, false);` and the `std::map` of faces. On every render Bravura (OTF/CFF of hundreds of KB) and Arimo are instantiated from the QByteArray, `glyphIndexesForString` is resolved and `pathForGlyph` is called for every distinct glyph on the page (dozens of symbols plus the alphabet used by each text size). That fixed cost is paid per page and per repaint: 3-6 times on open, on every zoom settling and on every key that changes a page, and it weighs more the smaller the painted page. It is not measured; by order of magnitude it is hundreds of µs to a few ms on top of a 5-19 ms paint, and it is the same work repeated. The good thing about the current design is that there is no mutex or contention between paint threads, and that must be preserved.

**Proposal.** A persistent and bounded cache without locks on the hot path: since PreferNoHinting is requested the outlines scale linearly, so it is enough to store each glyph once at a reference em (e.g. 1000 px) in a `thread_local` table per pool thread (or an immutable shared table published with an atomic std::shared_ptr) keyed by (font, glyph index), and apply scale + translation when filling with the QTransform that is already used at lines 84-87. The size is bounded by the number of distinct glyphs (a few hundred paths, <1 MB) and does NOT grow with zoom, unlike a cache by pixel size. Also store the reference QRawFont and the codepoint->index table next to the cache. Measure it with the paint benchmark before accepting it.

**Verification note.** Checked: `Outlines` is local to paintGlyphs (score_painter.cpp:130) and in paintTexts (:158-173) a QRawFont is created from bytes and a std::map of faces per call; ScoreDocument::painter() (score_document.cpp:495-498) only keeps the QByteArrays. It is true that every render instantiates the fonts again and extracts outlines. But the cost is not measured (the finding itself admits it) and is included in the 5.4-19 ms already measured (docs/performance.md:86-88), which run on worker threads ('latency, not stutter'). Without measurement 'medium' does not hold. Reservations about the proposal: (1) scaling from a reference em does not give pixels identical to the current ones; the kerned advances (`advancesForGlyphIndexes`, :185-186), on which the centring and the width of the knockouts depend, must also be validated per platform. (2) `thread_local` on threads of the global QThreadPool yields little: the threads expire after 30 s of inactivity and each one warms its own table; moreover a QRawFont in thread_local or static would be destroyed when the thread ends, possibly after QGuiApplication has been torn down. It is better to store only plain data (QPainterPath, indices, advances). (3) A table of QPainterPath shared between threads is only safe if const methods that cache (boundingRect, etc.) are never called on it; the current use (`translated`, `QTransform::map`) copies first and is safe. It does not affect the determinism of the core or the audio thread. Recommendation: measure first with the [app] benchmark.

**Observations on this area (what is already well solved).**

Scope: read-only (nothing was built or run); the orders of magnitude of P4, P6 and P7 are estimates to be confirmed by measurement, and in P3 the scroll jump should be checked in the app.
Read: src/app/src/score_page.cpp/.hpp, score_painter.cpp/.hpp, ScoreView.qml and Elevation.qml in full; Main.qml, TunerPane.qml, score_document.cpp, audio_controller.cpp, main.cpp, src/notation/src/cursor.cpp and src/core/include/elara/core/parallel.hpp by sections. The rest of the QML (SongPane, EntryBar, IconButton, SegmentedControl, WelcomePane, Logo, Icon...) only by pattern search; src/app/CMakeLists.txt and qtquickcontrols2.conf were not opened.
Well solved, leave alone:
- ScorePage is NOT a QQuickPaintedItem: it is a QQuickItem with a QSGImageNode whose texture is painted on a worker thread; the UI thread only compares and hands over the QImage (which is released after creating the texture, score_page.cpp:146).
- The ListView virtualises: only the visible pages plus one page height of buffer are alive, so at zoom <= 1 the texture memory is a few tens of MB even if the song has 21 pages. Pages outside that range are indeed freed.
- A relayout does NOT repaint all the pages: `*page == *shown_` (score_page.cpp:111) keeps the texture of those that do not change, and in addition only the live delegates are evaluated.
- Zoom stretches the texture and repaints a single time 120 ms after the size settles (except for the case of P2).
- There is no `layer.enabled`, MultiEffect or ShaderEffect in src/app/qml: the shadows (Elevation.qml and the paper one) are analytic RectangularShadow, with no intermediate textures.
- Caret and playback cursor are QML items over the texture: moving them does not repaint the page.
- Painting shares no mutable state between threads (font objects per call, no mutex): zero contention; the improvement of P4 must preserve it.
- The search for the measure being played is binary (Sequence::playedMeasureAt).
- The tuner's infinite dot animation only runs with the tuner visible and listening.
Outside the scope of this section; worth checking separately for `score_document`: `ScoreDocument::edited()` (src/app/src/score_document.cpp:877-880) copies the song (`std::make_shared<const core::Song>(document_->song())`) and builds a new `audio::Sequence` on the UI thread on every edit; with the measured times of the large song that is about 10.7 + 28 ms per key before launching the relayout. And `relayout()` (line 472) does not cancel stale layouts either, it only discards their result by `generation`.

### Notation (layout)

#### notation/N4 · Nested vectors per beat and per column that are freed and requested again in every measure

- **Where:** `src/notation/src/standard_staff.cpp:484`
- **Axis:** memory · **Impact:** medium · **Effort:** M · **Verdict:** confirmed

```cpp
chord.heads.push_back(headOf(note, beat.duration.value()));
```

**Why it matters.** Chord has `std::vector<Head> heads` (line 235) and Column has `std::vector<std::size_t> chords` (line 261). `chords_.clear()` and `columns_.clear()` (lines 449-450) destroy those inner vectors, so the capacity of chords_ is kept but every beat and every column allocate again in every measure, and twice per layout (measuring and drawing). The same happens in tablature with TabColumn::notes and ::beats (tab_staff.cpp:67-68, `column.beats.push_back(&beat);` at 145) and with beamGroups(), which returns a vector<vector<size_t>> per voice and copies each group (`groups.push_back(group);`, layout_common.cpp:662). In addition `std::vector<std::size_t> order` is created and sorted twice per measure with the same key (lines 511 and 586). Estimated at 3-5 allocations per beat and pass: several hundred thousand per layout of the large song.

**Proposal.** Flat storage per writer: a member `std::vector<Head> heads_` and Chord stores {first, count} (std::span when used). The columns are consecutive runs of the already sorted `order`: keep order_ as a member (computed once and shared with accidentals()) and Column becomes {first, count}. The same in TabWriter with flat notes_/beats_. beamGroups writes into a flat caller buffer (indices plus offsets) instead of returning vector<vector>.

**Verification note.** All checked: Chord::heads (src/notation/src/standard_staff.cpp:235) and Column::chords (261) are inner vectors that chords_.clear()/columns_.clear() (449-450) destroy in every measure. chord.heads grows with push_back without reserve (484), so a chord with several notes reallocates several times. In tablature, TabColumn::notes and ::beats (src/notation/src/tab_staff.cpp:67-68, push_back at 145 and 149) do the same. beamGroups returns vector<vector<size_t>> and copies each group with groups.push_back(group) (src/notation/src/layout_common.cpp:662). 'order' is built and sorted twice (511-517 and 586-590). Everything happens in measuring and in drawing. Qualifications: the two sorts are not identical, because the one at 511 excludes whole-measure rests and accidentals() returns early in percussion, so sharing order_ requires computing it outside accidentals(). The real category is speed (temporary allocations), not resident memory. The number of allocations is an estimate consistent with the code but unmeasured, and the benefit is a part of a 3.5-21 ms layout that runs off the UI thread. The proposal is feasible and does not affect determinism or threads (writers per job). Almost free preliminary step: chord.heads.reserve(beat.notes.size()) and groups.push_back(std::move(group)). Flat storage touches a lot of StandardWriter and TabWriter code (real effort M).

#### notation/N1 · layoutPages measures and lays out the whole document again on every edit: there is no per-measure or per-system cache

- **Where:** `src/notation/src/page_layout.cpp:289`
- **Axis:** speed · **Impact:** medium (initially proposed: high) · **Effort:** L · **Verdict:** partial

```cpp
const std::vector<MeasureWidth> widths =
        detail::measureWidths(song, staves, font, first, last, threads);
```

**Why it matters.** The app calls notation::layoutPages(*song, staves, *font, pageOptions()) with the whole song on every relayout (src/app/src/score_document.cpp:135). Changing one note costs the same as opening the file: 3.5 ms on all the cores (9.4 ms of CPU) with 1 track and 21 ms (132 ms of CPU) with 20. In addition every measure is read twice per layout: collect() in measureRange (system_layout.cpp:539) and again in layoutLine (system_layout.cpp:658), with springsOf recomputed (line 662). The width of a measure is independent of context: measureRange calls header(m)/changes(m)/collect(m)/barline(m) and clearItems() per measure, and depends only on measure m and on the headers m-1 and m. With a cache, an edit would touch 1 measured measure, the line-break DP over doubles and 1-3 systems: estimated at less than 0.5 ms on a single thread (estimate, not measured).

**Proposal.** 1) A MeasureWidth cache per measure, keyed by (staff selection, measure index, revision of the measure and of the headers m-1/m), kept outside layoutPages (a LayoutCache object passed as a parameter). The editing commands invalidate the measures they touch; inserting or deleting measures shifts indices. 2) Always rerun LineBreaker (cheap: the vectors natural_/tightest_/ideal_ are already precomputed). 3) A DisplayList cache per system without offset, keyed by (first, last, stretch, numbered, named) plus the revision of its measures and of the neighbours first-1 and last (tiesOut reads the next measure; dynamicBefore looks backwards); layoutLine is called only for the systems whose key changes. 4) Stacking into pages is redone entirely because it is O(systems).

**Verification note.** Code confirmed: layoutPages (src/notation/src/page_layout.cpp:289-304) measures all the measures and lays out all the systems on every call; there is no cache. The app calls it with the whole song (src/app/src/score_document.cpp:135) and every measure goes twice through collect() and springsOf (system_layout.cpp:539/551 and 658/662). Inflated impact: the layout runs on a QtConcurrent thread (score_document.cpp:472), not the UI one, and costs 3.5-21 ms. On every edit, edited() (score_document.cpp:877-880) copies the song and sequences it on the UI thread (10.7 + 28 ms measured), which is the dominant cost. The '<0.5 ms' with a cache is an unmeasured estimate. The proposal is not correct as it stands: (a) there is no per-measure 'revision' in core (grep with no results) and the app copies the whole Song by value on every edit, so new invalidation infrastructure is needed; (b) the system key with neighbours first-1 and last is insufficient: dynamicBefore (layout_common.cpp:209) goes back an arbitrary number of measures until it finds attacked notes, so editing a distant measure can change the dynamics of a system whose key does not change; (c) the width and the drawing also depend on song.measureCount() (final barline, system_layout.cpp:543 and 679) and on track properties that the key does not capture (clef, tuning, writtenOffset, name); (d) the cache would be mutated from the worker thread, and relayout() can have several layouts in flight, so it needs exclusive ownership or synchronisation. Feasible but with a high risk of stale results; effort L at the least.

#### notation/N2 · Two batches of std::thread created and destroyed per layout: there is no pool and the parallelism threshold is very low

- **Where:** `src/notation/src/page_layout.cpp:298`
- **Axis:** concurrency · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** partial

```cpp
core::forEachIndex(0, lines.size(),
                       std::min<unsigned>(threads, static_cast<unsigned>(lines.size())),
```

**Why it matters.** forEachIndex creates the threads on every call (src/core/include/elara/core/parallel.hpp:44 `workers.emplace_back([&, t] {`) and joins them when finished. layoutPages calls it twice: measureWidths (system_layout.cpp:593) and the systems (page_layout.cpp:298). That is up to 2 x hardware_concurrency threads created per layout; creating and joining a thread on Windows costs tens of microseconds, comparable to the work of a system of 3-4 measures. It fits the measured scale: 9.4 ms on one thread against 3.5 ms on all (2.7x). With kParallelMeasures = 48 (page_layout.cpp:31), a song of 60 measures already launches ~15 threads for ~15 tiny systems. The layout already runs on a QtConcurrent thread (score_document.cpp:472) and page painting uses that same pool (score_page.cpp:120), so the layout's threads add to Qt's: oversubscription.

**Proposal.** A persistent pool in core/parallel.hpp: a lazy singleton with hardware_concurrency-1 workers, in which the calling thread also executes jobs; forEachIndex enqueues into it, keeping the distribution by atomic index and the order of the exceptions. In notation: a single parallel region per layout when possible, a minimum grain per task (group systems up to ~16-32 measures per job, like kMeasuresPerJob) and a parallelism threshold based on real work (measures x staves) instead of 48 measures.

**Verification note.** Facts confirmed: forEachIndex creates and joins std::thread on every call (src/core/include/elara/core/parallel.hpp:41-56) and layoutPages invokes it twice (system_layout.cpp:593 and page_layout.cpp:298). kParallelMeasures = 48 (page_layout.cpp:31), so 60 measures launch min(cores, number of systems) threads for the systems; measureWidths only launches min(cores, jobs of 32 measures), that is 2 in that example, so '2 x hardware_concurrency' only holds for long songs. The layout runs in Qt's pool (score_document.cpp:472), like painting (score_page.cpp:120). Inflated impact: the cost of creating threads is not measured, and the figure '9.4 ms on one thread' contradicts N1, which gives 9.4 ms as total CPU with all the cores, so the 2.7x proves nothing about thread creation. In the 21 ms case it is at most ~1-2 ms, off the UI thread. A point in favour that the finding does not mention: forEachIndex is also used by audio::Sequence (src/audio/src/sequence.cpp:469, 638, 698) and core/performance.cpp:118/134, and Sequence is built on the UI thread on every edit, so a pool would help there. Risks of the proposal: the singleton would be shared by the UI thread (sequencing) and the layout worker, so the UI may wait behind layout jobs unless the caller executes jobs; reentrancy must be supported without deadlock, the 'thread order' of the exceptions must be redefined as index order, and static destruction must be handled with care. It does not affect determinism (each job writes into its own slot) or the audio thread. Before the pool there is a fix of effort S: limit the threads by real work (e.g. measures/32) and raise the threshold.

#### notation/N8 · stretchFor does 60 bisections per tight system, serially, on a piecewise-linear function

- **Where:** `src/notation/src/page_layout.cpp:185`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
for (int i = 0; i < kBisections; ++i) {
            const double middle = (low + high) / 2;
            if (lineWidth(widths_, line.first, line.last, middle) > sums.room) {
```

**Why it matters.** best() calls stretchFor for each line on the calling thread (line 113). Every system that has to be tightened evaluates lineWidth 60 times, and each evaluation walks all the springs of its measures: about 60 x 4 measures x ~10 springs = 2-3 thousand springLength per system. With ~150 systems per track and half of them tightened that is on the order of 200k evaluations, a few tenths of a ms serially out of the 3.5 ms of the 1-track layout (estimate). 60 iterations over an interval of 0.2 also exceed the precision of a double (~52 bits).

**Proposal.** Solve exactly: the width is monotonic and piecewise linear in stretch, with a breakpoint per spring at s_i = ink_i/ideal_i - 1. Collect the breakpoints within [-kMaxShrink, 0], sort them and walk them accumulating the slope (sum of ideal of the springs not yet saturated) until room is reached: O(k log k) and without iterating. Minimal alternative: stop when high - low < 1e-9 (~28 iterations). Both change the last bits of the result: check the reference tests.

**Verification note.** Confirmed: stretchFor (src/notation/src/page_layout.cpp:169-194) does kBisections = 60 iterations (line 27) only for systems that must be tightened (natural > room and tightest < room), and each one calls lineWidth, which walks the springs of the measures of the line (MeasureWidth::width, src/notation/src/layout_common.hpp:465-471). It runs serially from best() (line 113) or every() (123). With kMaxShrink = 0.2 (layout_common.hpp:427) the last iterations no longer move the midpoint, so they are superfluous without being incorrect. The cost is on the order of tenths of a ms at most and off the UI thread; low impact is correct. On the proposal: the exact solution is valid (monotonic piecewise-linear width, breakpoint at ink/ideal - 1), but it must preserve the current invariant that the result never exceeds room (today 'low' is returned, which always satisfies lineWidth <= room) and handle springs with ideal = 0. Either variant changes the last bits of stretch and therefore all the coordinates of the system: the reference outputs will have to be regenerated. It remains deterministic.

#### notation/N9 · toSvg creates one std::string per number and another per element, with no reserve of the result

- **Where:** `src/notation/src/svg.cpp:16`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
std::string text = std::format("{:.3f}", value);
```

**Why it matters.** Each <line> calls number() 5 times (6 with a dashed stroke) (line 131), each beam <polygon> 8, each <text> 2-3; each one goes through std::format with parsing of the format string and returns a std::string. Then another std::format builds the element in a temporary string that exceeds the SSO (heap allocation) and is concatenated with `svg +=`. Since svg has no reserve, a page of ~5k items (~0.5 MB of text) is reallocated ~20 times. Only the CLI uses it (src/cli/src/render_commands.cpp:406 `file << notation::toSvg(pages[p], svg);`), not the UI: it affects the export of many pages, not interactive latency.

**Proposal.** svg.reserve(256 + 110 * (lines + beams + glyphs + texts + curves + polygons)). Replace number() with `void appendNumber(std::string& out, double v)` using std::to_chars(buf, buf + 32, v, std::chars_format::fixed, 3) on a stack buffer and trimming zeros in place. Write each element with std::format_to(std::back_inserter(svg), ...) or with direct appends, without intermediate strings; point() and escapeAttribute() write into the output. Estimated at 3-5x on SVG export (not measured).

**Verification note.** Confirmed: number() creates a std::string with std::format for every value (src/notation/src/svg.cpp:15-24), and every element is assembled with another temporary std::format before 'svg +=' (127-185): 5 calls per <line> (6 with dash), 8 per beam, 2-3 per text. toSvg does no reserve. Its only caller is the CLI (src/cli/src/render_commands.cpp:406); it plays no part in the UI or the audio, so low impact is correct. The '3-5x' is not measured. The proposal is feasible: std::to_chars with chars_format::fixed and precision 3 rounds the same as std::format("{:.3f}"), so the output does not change if the zero trimming and the '-0' case are preserved (including '-0.000' coming from values such as -0.0004). Two cautions: a 32-byte buffer is not enough for a huge double in fixed format, so the ec of to_chars must be checked and an alternative path provided; and if compiling for macOS with libc++, floating-point to_chars requires a recent deployment target.

#### notation/N3 · Three temporary std::vector per chord on the hot path of StandardWriter

- **Where:** `src/notation/src/standard_staff.cpp:496`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
chords_[c].stemUp = stemUpFor({c});
```

**Why it matters.** stemUpFor takes `const std::vector<std::size_t>&` (line 641), so `{c}` builds a heap vector for every chord without a beam. placeAccidentals creates `std::vector<std::size_t> columnOf(chord.heads.size(), 0);` (line 752) per chord even if it has no accidentals (the early return comes later, at 773). draw() creates `std::vector<double> dotYs;` (line 803) and does push_back per head even if duration.dots() is 0 and dots() does nothing. read() runs twice per measure (measuring and drawing), so the first two are duplicated. In the reference song (80k beats) that is on the order of 300-400k malloc/free pairs per full layout, spread over all the cores on the same process heap, which adds contention and limits scaling.

**Proposal.** stemUpFor(std::span<const std::size_t>) and call with `std::span(&c, 1)`. In placeAccidentals, exit early if no Head has an accidental and turn columns/columnOf/rightEdge into reused member vectors (clear() keeps the capacity). In draw(Chord&), fill dotYs only if chord.duration.dots() > 0 and use a member working vector.

**Verification note.** The three allocations exist: stemUpFor({c}) builds a std::vector because the signature is const std::vector<std::size_t>& (src/notation/src/standard_staff.cpp:496 and 641); columnOf(chord.heads.size(), 0) is created before the early return (752 versus 773); dotYs is always filled (803-822) even though dots() returns if count == 0 (853-856). read() runs in both passes because collect() calls read() (1229-1230). Qualifications: stemUpFor({c}) only affects chords without a beam; columnOf and dotYs do not affect rests (shape() and draw() return earlier); dotYs only happens in the drawing pass. The 300-400k pairs are therefore an upper bound, not a measurement. The claim of heap contention limiting scaling is not backed by any data. Bounded gain: a fraction of a 3.5-21 ms layout that runs off the UI thread. The finding omits a fourth identical temporary: dots(x + chord.headRight, {y - 0.5}, ...) at line 800 creates a vector for every rest. The proposal is correct and safe: each job creates its own writers with makeWriters, so the member vectors are not shared between threads, and std::span(&c, 1) is valid. Worth doing because it is S, but with low impact.

#### notation/N5 · Page assembly is serial: it shifts and copies all the document's items on the calling thread and without reserve

- **Where:** `src/notation/src/page_layout.cpp:327`
- **Axis:** concurrency · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
detail::shift(system, options.margin, placed[k].y + extra * static_cast<double>(k));
            detail::append(page, std::move(system));
```

**Why it matters.** After the parallel region, flush() walks on a single thread all the items of all the systems: shift() rewrites every point and append() copies them into the page with `into.insert(into.end(), std::make_move_iterator(...` (layout_common.cpp:164) without reserving. With ~8-10 systems per A4 page the page's vectors grow geometrically: each item is moved ~2 times on average and the surplus capacity (up to ~50% with MSVC's 1.5x growth) stays alive as long as the document lives, because the pages are kept in pages_. Each item was already shifted per staff in layoutLine (system_layout.cpp:711) and again by the indent (line 750). For 20 tracks that is tens of MB of items walked serially: estimated at 3-6 ms of the 21 ms, and it is the part that does not scale with cores (estimate, not measured).

**Proposal.** Separate placement from assembly: the current loop only computes, per page, the list Placed{system, y} (O(systems), serial). Then core::forEachIndex over pages: each page sums the sizes of its systems, does an exact reserve of glyphs/lines/beams/texts/curves/polygons/measures/columns and applies shift+append. Credits and page numbers are added at the end as now. Optional: have layoutLine receive the vertical offset of each staff and apply it when emitting, saving a shift pass.

**Verification note.** Confirmed that flush() (src/notation/src/page_layout.cpp:318-332) applies shift() and append() on the calling thread after the parallel region, and that append inserts without reserve (src/notation/src/layout_common.cpp:164); the first system of each page is moved without a copy (160-162). Inaccuracies: the indent shift (system_layout.cpp:750) only happens on the first system of the document (options.named = s == 0), not on every system; the per-staff shift (711) does nothing for the first staff (dy = 0) and both are already inside the parallel region. The '3-6 ms of 21' is an unmeasured estimate; in the 1-track case (3.5 ms) it is negligible and everything happens off the UI thread. The surplus capacity retained in pages_ is real. Errors in the proposal: (1) the title credits are not added 'at the end as now': addCredits runs before placing the systems (line 310) and its result 'top' conditions the placement; only the page numbers and the copyright go at the end (357-370). Moving them would change the order of the texts on page 1 and with it the DisplayList equality that the app uses to avoid repainting (src/app/src/score_page.cpp:111) and the reference outputs. (2) With an exact reserve, the shortcut 'if (into.empty()) into = std::move(items)' in append would discard the reserved capacity, so append has to change. (3) It would be a third batch of threads per layout while there is no pool (N2). The cheap and safe part is only the exact reserve per page (S); parallelise only if a measurement justifies it.

#### notation/N6 · A stale layout cannot be cancelled: layoutPages accepts no stop signal and every relayout launches another using all the cores

- **Where:** `src/notation/include/elara/notation/page_layout.hpp:30`
- **Axis:** concurrency · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
unsigned threads = 0; ///< 0 lays out on every core when the song is long enough
```

**Why it matters.** PageLayoutOptions has no stop_token or cancellation flag, and neither measureRange nor the systems loop checks anything between jobs. The app discards the old result only when it finishes (score_document.cpp:476 `if (generation != generation_) { return; }`) and launches a QtConcurrent::run for every relayout() (line 472) without merging requests. With consecutive edits (held key, ~30 Hz) and 21 ms layouts that use all the cores, several layouts overlap, each one with hardware_concurrency threads of its own; each discarded layout throws away up to 132 ms of CPU on the large song and delays the one that will actually be shown.

**Proposal.** Add to PageLayoutOptions a `std::stop_token stop` (or `const std::atomic<bool>* cancelled`) that is checked at the start of each measureWidths job (32 measures) and each system; if it is set, layoutPages returns empty or throws an exception of its own. In the app: request a stop of the previous layout when calling relayout() and merge requests (at most one in flight and one pending, the last one wins).

**Verification note.** Facts confirmed: PageLayoutOptions has no stop signal (src/notation/include/elara/notation/page_layout.hpp:17-31), neither measureRange nor the systems loop checks anything, and relayout() launches one QtConcurrent::run per call and only discards by generation when finished (src/app/src/score_document.cpp:470-477). Keys.onPressed (src/app/qml/ScoreView.qml:134) does not filter autorepeat, so a held key does generate consecutive edits. What does not hold with the measured times is the overlap: every edit goes through edited() (score_document.cpp:875-893), which on the UI thread copies the song and the sequence (10.7 + 28 ms on the large song) before calling relayout(). The minimum interval between two launches is therefore ~39 ms, greater than the 21 ms of the most expensive layout (3.5 ms with one track), so in steady state the previous layout has already finished when the next one is launched and cancelling would save very little. Only quick changes of track or view (setTrack/setView, lines 400-418), which neither copy nor sequence, could overlap. Unmeasured inference: with the UI saturated, each result may arrive already stale and be discarded, but that is fixed by making edited() cheaper, not by cancellation. The proposal is feasible and safe (a check per job of 32 measures and per system, unrelated to the audio thread); if an exception of its own is thrown, the generation must be checked before fail(), as already happens at line 476.

#### notation/N7 · DisplayList items with avoidable padding and size_t for the measure index

- **Where:** `src/notation/include/elara/notation/display_list.hpp:57`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
struct GlyphItem {
    Glyph glyph{};
    Point origin;
    Role role{};
    std::size_t measure = 0;
    double scale = 1.0;
```

**Why it matters.** Sizes computed by hand for x64/MSVC, not measured: GlyphItem 48 bytes (Glyph and Role are uint8_t, each with 7 bytes of padding, plus size_t measure), LineItem 64, BeamItem 48, TextItem 72 (a 32-byte std::string for texts that are almost always 1-2 fret digits), CurveItem 96, PolygonItem 40 plus a heap block for every 3-point arrowhead (tab_staff.cpp:585). With all the tracks in notation and tablature (160k notes: head + fret number + stems and beams) it is estimated at 20-30 MB of live items, duplicated while the old and new pages coexist during a relayout (pages_ is a shared_ptr that painting also retains). The same bytes are walked in shift(), inkExtents() and when painting.

**Proposal.** Reorder and narrow: GlyphItem {Point origin; double scale; uint32_t measure; Glyph glyph; Role role;} = 32 bytes (-33%); LineItem with uint32_t measure and Role together = 56; BeamItem 48 -> 40 by lowering measure to uint32_t (after the doubles); TextItem with the 4 one-byte fields and uint32_t measure together = 64; CurveItem 96 -> 88. PolygonItem with inline storage for 3-4 points (std::array<Point, 4> plus a counter) to remove the per-arrow allocation. Keep double in coordinates so as not to change results. Add static_assert(sizeof(...)) as is already done with Note/Beat.

**Verification note.** Current sizes verified against the definitions (Glyph and Role are uint8_t, src/notation/include/elara/notation/smufl.hpp:20 and display_list.hpp:18; Point is two doubles): GlyphItem 48, LineItem 64, BeamItem 48, TextItem 72 (with a 32-byte std::string in MSVC release), CurveItem 96, PolygonItem 40. Errors in the finding: (1) BeamItem does not drop to 40: 16+16+8+4 = 44, which with 8-byte alignment is 48 again; there is no saving. (2) 'static_assert(sizeof) as is already done with Note/Beat' is false: there is no static_assert in src. (3) The only producer of polygons is the bend arrowhead (src/notation/src/tab_staff.cpp:585 through StaffWriter::polygon, layout_common.cpp:580); there are few items and inline storage does not justify changing the public API. Inflated impact: the 20-30 MB are an unmeasured estimate and the real saving is concentrated in GlyphItem (48 to 32), with LineItem 64 to 56, TextItem 72 to 64 and CurveItem 96 to 88; overall ~15-20% of the items, a few MB in the worst case for a desktop app. The surplus capacity of the per-page vectors (N5) weighs the same or more. Omitted cost: reordering fields breaks the designated initialisers, which in C++ must follow declaration order (e.g. system_layout.cpp:490, and at least 19 sites in src/notation and tests/app/test_score_painter.cpp), and changing measure to uint32_t introduces narrowing conversions in brace initialisation in all the producers. It does not affect determinism if the coordinates stay in double.

**Observations on this area (what is already well solved).**

Scope: read-only review of the notation area; nothing was edited, built or run.
Read in full: display_list.hpp/.cpp, page_layout.hpp/.cpp, layout_common.hpp/.cpp, system_layout.cpp, standard_staff.cpp, smufl.cpp, svg.cpp, caret.cpp, cursor.cpp, plus core/parallel.hpp and the call sites in src/app/src/score_document.cpp. Of tab_staff.cpp, lines 1-609 of 610 were read. Of smufl.hpp only the declarations were reviewed, by grep. caret.hpp, cursor.hpp, staff_layout.hpp, svg.hpp and drum_map.cpp/.hpp were not opened.
Well solved, leave alone:
- SMuFL glyphs: Glyph is a uint8_t enum and EngravingFont::metrics() indexes a std::array<GlyphMetrics, kGlyphCount>. There are no maps by name on the hot path; glyphNamed() is linear but is not used in layout, and the metadata is parsed once (static in the app).
- roleName() is a constexpr table. There is no std::function: everything is lambdas and templates (forEachIndex is a template).
- LineBreaker: dynamic programming with early cut-off and precomputed natural_/tightest_/ideal_ sums.
- measureRange and layoutLine reuse writers and working vectors (columns, all, merged, positions) between measures; clearItems() keeps the capacity.
- inkExtents() is only called once per staff when closing the line (close/finish), not per measure. markTempo already avoids the quadratic tempo search.
- append() moves the whole vector when the destination is empty.
- Fret numbers fit in the SSO of std::string: they do not allocate on the heap, they only take up space (see N7).
- Thread safety: the forEachIndex jobs write to disjoint indices (systems[s], subspans of widths) and only read const Song and EngravingFont. No data races were seen in notation, and the result is deterministic.
- There are no caches in notation, so there are no unbounded caches. Nothing in this module runs on the audio thread.
- cursorAt() and caretAt()/slotX() do linear searches over the MeasureBox/ColumnBox of the pages, but they are hundreds of elements per call (sub-microsecond); not worth a finding. If desired, cursorAt admits a binary search because the measures are in order.
- StaffWriter::finish() inserts the staff lines at the beginning of list_.lines (a memmove of a few KB per staff): negligible cost.
Dependencies between findings: N1 has the highest return for interactive editing; N3 and N4 reduce the cost of every full layout (open, change track or view) and are independent of N1. N2 and N6 touch core/parallel.hpp and the app in addition to notation.

### Core (model and editing)

#### core/A1 · The model offers no cheap snapshot or change extent: every edit copies the whole song and resequences it on the UI thread

- **Where:** `src/core/include/elara/core/document.hpp:47`
- **Axis:** concurrency · **Impact:** high · **Effort:** L · **Verdict:** confirmed

```cpp
[[nodiscard]] const Song& song() const noexcept { return song_; }
```

**Why it matters.** Document only exposes the Song by reference and Command (document.hpp:29-35) does not say what it touched (track/staff/measures). Staff stores `std::vector<Measure> measures_` by value (song.hpp:62), so any consumer that needs a stable copy for another thread has to copy everything. The real consumer does it on every key: src/app/src/score_document.cpp:877 `song_ = std::make_shared<const core::Song>(document_->song());` and :880 `std::make_shared<const audio::Sequence>(*song_)`, both synchronous on the UI thread. With the measured times that is 10.7 ms of copy + ~26-28 ms of sequencing = ~40 ms of blocking per keystroke on the large song (2-3 frames at 60 Hz), for a command that costs 0.60 us. In addition two full copies of the model always live (Document::song_ and the snapshot), and a third while a layout in progress retains the previous one: ~15 MB each.

**Proposal.** Give the core structural sharing and change extent: (1) Staff with the measures in a copy-on-write block (e.g. `std::shared_ptr<const std::vector<Measure>>` that is separated in the non-const `measure()`, or chunks of 32-64 measures), so that copying the Song is O(staves) and an edit only duplicates the staff/chunk touched (~0.5 ms per 500-measure track instead of 10.7 ms); (2) add to Command an `extent()` (track, staff, first measure, number, or 'structure') that Document accumulates together with a revision counter, so that the sequencer and the layout can redo only what is affected and so as to take the full sequencing off the UI thread.

**Verification note.** Checked: document.hpp:47 only exposes `const Song&`; Command (document.hpp:18-36) declares no extent; Staff stores `std::vector<Measure> measures_` by value (song.hpp:62). In src/app/src/score_document.cpp, `edited()` (875-894) copies the song (877) and builds the Sequence (880) synchronously; it is called from execute/undo/redo, all on the UI thread, and only afterwards launches `relayout()` with QtConcurrent (893). With the measured times that is ~10.7 + 26-28 ms per edit on the large song; in addition line 881 compares the new sequence with the previous one (`*sequence != *sequence_`), a cost that the finding does not count. The third live copy is also true (the relayout lambda captures `song = song_`). Qualifications: (a) it is a known cost accepted in writing, not a discovery: ADR-0031 (lines 50-54) and ADR-0032 (58-61) already say that every edit copies the song and that copy-on-write of measures will be needed; (b) the ~40 ms apply only to the stress song (20 tracks, 160k notes); on normal songs it stays within a frame; (c) the ~15 MB per copy are not measured, they are an estimate consistent with Note 48 B / Beat 64 B. On the proposal: COW at the Staff/chunk level is feasible and does not clash with ADR-0005 (which rejected persistent structures so as not to turn Measure/Beat/Note into handles) as long as those types remain values; note that the Staff `operator==` is `= default` and would start comparing pointers, and that any non-const `measure()` would split the block (with a single block per staff each edit recopies ~0.5 ms; chunks are better). COW only removes 10.7 of the ~40 ms: most of it is eliminated without touching the core, by building the Sequence in a worker with a generation counter as relayout already does (the snapshot is already `shared_ptr<const Song>`). `extent()` for partial resequencing is far more doubtful (ties between measures, repeats, tempo) and should not be sold as part of the same effort.

#### core/A4 · soundingNotes: each merge round allocates new buffers, initialises and copies them; the lists grow without reserve through a reference to the shared vector

- **Where:** `src/core/src/performance.cpp:142`
- **Axis:** memory · **Impact:** medium · **Effort:** M · **Verdict:** confirmed

```cpp
merged[i].resize(a.size() + b.size());
```

**Why it matters.** SoundingNote takes 80 bytes and its default values are not zero (key = 60, velocity = 80), so resize() writes every element before std::ranges::merge (line 143) overwrites it: a double write pass. With 160k notes that is 12.8 MB per round and about 5 rounds for 20 lists: ~64 MB allocated, touched for the first time (page faults on freshly requested large allocations) and freed per sequencing, with a 2x peak (25.6 MB) and the last rounds on 2 and 1 threads. In addition, at line 120 `std::vector<SoundingNote>& list = lists[i];` every thread does push_back through the vector header that lives in the shared array `lists` (24 bytes per header, several per cache line: false sharing when contiguous voices have notes) and without reserve (about 14 reallocations per list of 8k notes).

**Proposal.** When reading: build in a local vector with `reserve` (cheap estimate: beats of the voice x 2, or a prior count) and move it into `lists[i]` when finished. When merging: reserve once two buffers of the total size and alternate between them across rounds (each list is a run [start, end) of the buffer), merging with std::merge from run to run; this removes the per-round allocations and the prior initialisation, and the peak is bounded to a fixed 2x. Alternative: a k-way merge in a single pass over the final buffer.

**Verification note.** Checked: performance.cpp:142 does `resize(a.size()+b.size())` and 143 merges on top; SoundingNote has non-zero initialisers (key = 60, velocity = 80; performance.hpp:23-24) and measures 80 bytes, so every round writes twice. With 20 lists there are 5 rounds (10, 5, 3, 2, 1) and the last is single-threaded over the 160k notes (12.8 MB): it is the part that weighs the most. `lists[i]` is filled by reference and without reserve (line 120 and push_back in addVoice, line 57). Qualifications that reduce part of the text: false sharing only happens when two voices of the same staff have notes; the headers of voice 0 of consecutive staves are 96 bytes apart (4 voices x 24), on different cache lines, and empty voices do not write. The number of reallocations depends on the library (1.5x in MSVC is more than 14). The ~64 MB are not exact because odd lists are moved, not copied. The saving is not measured; reasonably it is 2-4 ms of the 10.7 ms of soundingNotes, which are paid on every edit and in MIDI export, which is why medium is kept (it would become low if the sequencing leaves the UI thread as A1 proposes). On the proposal: reserving SoundingNote buffers with `resize` or `new[]` still runs the initialisers; the minimal and safe fix is `reserve` + `std::ranges::merge(a, b, std::back_inserter(merged[i]), earlier)`, or two alternating buffers initialised only once. Determinism is preserved because the comparator includes track, staff and voice (there are no ties between lists). The same pattern is in sequence.cpp:696-711 with the 320k events and the finding does not mention it.

#### core/A2 · forEachIndex creates and destroys std::thread on every call, with no pool or global cap

- **Where:** `src/core/include/elara/core/parallel.hpp:44`
- **Axis:** concurrency · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** partial

```cpp
workers.emplace_back([&, t] {
```

**Why it matters.** Every call starts `threads` new threads and joins them; the calling thread does not work, it only waits. threadsFor (line 21) asks for `hardware_concurrency()` without knowing what else is running. soundingNotes calls forEachIndex once per read and once per merge round (performance.cpp:118 and :134), and sequence.cpp three more (469, 638, 698 in a loop): on the reference machine (24 logical threads) a single sequencing creates on the order of 60-100 threads. On Windows creating+joining a thread costs tens of us, that is, several ms of the 26-28 ms of sequencing, which also runs per edit. As there is no shared pool, a relayout (page_layout.cpp:298, system_layout.cpp:593) launched from QtConcurrent coincides with the sequencing and with page painting on worker threads: 2-3x oversubscription at the moment the edit latency is measured. The granularity is one index per `next++` (line 46), with the cache line of `next` bouncing between all the threads when the jobs are small (one measure).

**Proposal.** Keep the signature of forEachIndex and change the implementation to a lazy persistent pool (hardware_concurrency()-1 workers, created once) in which the calling thread also takes indices; distribution by blocks (e.g. `next.fetch_add(chunk)` with chunk = max(1, n/(8*threads))) and a global limit of active workers so that concurrent calls do not exceed the number of cores. With that the `parallelFrom` threshold can come down because starting no longer costs anything.

**Verification note.** True: parallel.hpp:41-56 creates `threads` std::thread per call and the caller only joins; threadsFor (line 21) uses hardware_concurrency() capped by the number of jobs and 64. The thread count per sequencing is correct and even short: performance.cpp:118 (80 parts -> 24 threads) + merge rounds 10+5+3+2 (134) + sequence.cpp:469 (20 blocks of 8192) + 638 (20 tracks) + 698 (~20) = ~100 threads per edit on the 24-thread machine. That this amounts to "several ms" is plausible on Windows but is not measured in the repo. What does not hold: (1) the oversubscription "at the moment the latency is measured": in score_document.cpp `edited()` sequences synchronously (880) and only afterwards calls `relayout()` (893), so the layout of an edit never coincides with its own sequencing; it can only overlap with the layout/painting of the previous edit if typing is faster than the 3.5-21 ms of layout. (2) The bouncing of the `next` cache line with "one measure" jobs: no caller uses jobs that fine; they are blocks of 8192 notes (sequence.cpp:35), 32 measures (system_layout.cpp:64), a system, a track or a whole voice, compared with which a fetch_add is negligible. That is why the impact is lowered to medium. Proposal feasible with conditions: forEachIndex is not called from the audio thread (offline_renderer is outside real time); the caller must take part to avoid deadlock if there are nested or concurrent calls (layout in a QtConcurrent worker + UI); the result is already per index, so determinism is preserved, but the contract "first exception in thread order" has to be redefined; a static pool in a header-only header requires care with destruction at exit. Lowering the `parallelFrom` thresholds requires measuring again; it cannot be deduced.

#### core/A7 · SoundingNote takes 80 bytes because it uses size_t and int in fields that fit in 8-16 bits

- **Where:** `src/core/include/elara/core/performance.hpp:18`
- **Axis:** memory · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
std::size_t track = 0;
```

**Why it matters.** track, staff and voice are size_t (24 bytes) when voice is 0..3 and the other two are small; key and velocity are int for values 0..127; measure is size_t. With the two Fraction (32 bytes) the rest would fit in 16 bytes: 48 bytes in total against 80. That is 160k x 32 = 5.1 MB less per vector of sounding notes, and since the pairwise merge moves all the notes in every round (about 5), 40% less memory traffic in soundingNotes (10.7 ms measured) and in whoever walks them (sequencer, MIDI export).

**Proposal.** Reorder and narrow: `Fraction start, length; std::uint32_t measure; std::uint32_t beat; std::uint16_t track; std::uint8_t staff, voice, note, key, velocity;` (48 bytes), validating the ranges on construction in addVoice. Add a static_assert on the size so that it does not grow again.

**Verification note.** Checked: performance.hpp:18-29 has track/staff/voice/measure as size_t, key/velocity as int and beat/note as uint32; 24 + 32 + 8 + 8 + 8 = 80 bytes (the comment at line 26 says it was kept at 80 on purpose). The proposed layout adds up to 47 and ends at 48 because of alignment: correct. The ranges fit: voice < 4 (Measure::kMaxVoices), key 0..127, velocity <= 127 (dynamicVelocity). Qualification: the vector of sounding notes is transient (local in the Sequence constructor, sequence.cpp:465, and in MIDI export), so it is not resident memory but bandwidth during sequencing; the real category is speed and the time saving is not measured. Low impact, agreed. To apply it: the designated initialisers of addVoice (performance.cpp:57-66) and of the tests follow declaration order, so reordering fields forces touching them (it fails at compile time, not silently) and the conversions from int to uint8 need a cast; there is no maximum of notes per beat or of tracks in the core, so `note` in uint8 and `track` in uint16 need an explicit check (once per part for track/staff/voice, per beat for note), not just a static_assert on the size.

#### core/A8 · Transposing, retuning and changing dynamics copy and compare measure by measure in series and create a heap-allocated command per measure

- **Where:** `src/core/src/transpose.cpp:138`
- **Axis:** speed · **Impact:** low · **Effort:** M · **Verdict:** confirmed

```cpp
Measure content = staff.measure(m);
```

**Why it matters.** makeTranspose copies each measure, transforms it and then deep-compares it with the original (line 140, `if (content != staff.measure(m))`) to decide whether there is a change; the same pattern is in retune.cpp:77/83 and entry.cpp:748/766. For the whole large song that is a full copy (10.7 ms) plus the full comparison plus up to 10,000 `std::make_unique<ReplaceMeasure>` (objects of ~160 bytes with a std::string name each), all on one thread and on the thread that invokes the command: on the order of 20-30 ms of blocking. The staves are independent (the `followed` state is per staff), so it can be parallelised without changing the result.

**Proposal.** Make transposeMeasure/soundFrets/placeOnStrings return whether they changed anything (this avoids the deep comparison, and allows copying only when the first note is going to change); distribute the staves with forEachIndex collecting the results by index to keep the order deterministic; and replace the N ReplaceMeasure with a single range command (track, staff, first measure, `std::vector<Measure>`) that swaps the whole run.

**Verification note.** The pattern copy + transform + compare + make_unique per measure is checked in transpose.cpp:138-143, retune.cpp:77-86 and entry.cpp:748-769, all on one thread and on the thread that invokes the command. ReplaceMeasure (commands.hpp:75-93) takes ~160 bytes; its name std::string ("Edit measure", "Change dynamic") fits in SSO, so there is no extra allocation for the name. The 20-30 ms are an unmeasured estimate (the cost of placeOnStrings is not in docs/performance.md), and these are infrequent menu commands whose latency is in any case dominated by the ~40 ms of A1; low impact, agreed. `followed` is declared inside the staff loop (transpose.cpp:136), so the staves are independent and parallelisable. Conditions for the proposal: transposeMeasure throws invalid_argument with the measure number (45-47) and forEachIndex rethrows the first exception in thread order, not in index order; for the message to be the same as in serial it is necessary to capture per staff and rethrow the one with the lowest index (as guitar_pro.cpp:1241-1281 does). In retune and setDynamic there is nothing to parallelise: it is a single staff with state chained between measures. In transposition with a non-zero interval every measure with notes changes, so the deep comparison can be replaced by "has notes"; in retune the real-change boolean is needed. The single range command is feasible and removes the 10,000 allocations, but does not reduce the memory retained in undo (see A6).

#### core/A9 · forEachIndex terminates the process if creating a thread fails and does not cancel the remaining work after the first exception

- **Where:** `src/core/include/elara/core/parallel.hpp:41`
- **Axis:** concurrency · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
std::vector<std::thread> workers;
```

**Why it matters.** If `workers.emplace_back` (line 44) throws std::system_error with threads already started (thread or memory limit, more likely precisely because every call creates up to 64), the vector is destroyed with joinable std::thread and std::terminate is called: the application shuts down instead of degrading to fewer threads. On the other hand, when a job throws, its thread stores the error and exits (lines 49-51), but the others keep consuming indices until the end: when opening a corrupt file all the remaining tracks are imported before the error is reported, work that is discarded.

**Proposal.** Use std::jthread (or a guard that joins in the destructor) and, if creation fails, continue with the threads already created plus the caller. Add a `std::atomic<bool> failed` that each worker checks before taking the next index; to keep the contract of 'first exception in order' it is enough to keep rethrowing the one from the thread with the lowest index. Both points are solved along the way if the pool from A2 is adopted.

**Verification note.** Checked: `workers` is a `std::vector<std::thread>` (parallel.hpp:41) and if `emplace_back` (44) throws system_error with threads already started, its destructor finds joinable threads and calls std::terminate. It is a very unlikely case on the desktop (thread creation fails), so it is a robustness defect of low impact, as proposed. It is also true that after an exception the other threads keep taking indices (46-51, there is no cancellation flag). Qualifications: the corrupt-file example only applies to callers that let the exception escape (musicxml.cpp:1906-1919, readPart); the Guitar Pro importer catches inside the job and stores the damage by index (guitar_pro.cpp:1241-1281) to always give the same error, so a flag in forEachIndex saves it nothing. The wasted work only occurs on the error path and at most equals a normal import (~100 ms). On the proposal: std::jthread fixes the terminate (the declaration order next, errors, workers makes the join happen before destroying what the threads reference); degrading to fewer threads requires catching the system_error and having the caller work. With early cancellation the error that is reported depends on the moment at which each thread fails: today it already depends on thread order and not on index, but it is advisable to fix the contract as "exception of the lowest job index" so as not to break the documented guarantee of the same errors with any number of threads (docs/performance.md:67).

#### core/A3 · Fraction computes 2-3 64-bit gcds per addition/subtraction and normalises already-normalised values again

- **Where:** `src/core/include/elara/core/fraction.hpp:50`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
friend constexpr Fraction operator-(const Fraction& a, const Fraction& b) { return a + -b; }
```

**Why it matters.** operator+ (lines 46-47) does a gcd of denominators and then builds with the public constructor, which calls normalise() (another gcd, line 90). The unary minus (line 43, `Fraction(-num_, den_)`) normalises again something that already is, so a subtraction is 3 gcds; Duration::length() adds another per beat. It is the pattern of all the hot loops of the model: `position += length` per beat in performance.cpp:69, Voice::length() in measure.cpp:12, validation. docs/performance.md already points it out: in validate (3.0 ms) 'half of it summing voice lengths' and in MIDI import on one thread 287 ms 'mostly fraction arithmetic in the grids'. Almost all real denominators are powers of two (or equal to each other), a case in which no gcd is needed.

**Proposal.** Add a tagged private constructor that does not normalise and use it in the unary minus and in already-reduced results; in operator+ and operator- a fast path for `a.den_ == b.den_` and another for power-of-two denominators (`(d & (d - 1)) == 0`): add with shifts and reduce with `std::countr_zero` instead of gcd; implement operator- directly. Optional: a constexpr table of 9 values x 4 dots for Duration::length() without an irregular group. Estimate: summing durations goes from ~3 gcds to 0 in the common case; validate would drop towards ~1.7 ms and the largest benefit is in the MIDI importer's grids.

**Verification note.** The code is as described: operator+ (45-48) does a gcd of denominators and the public constructor normalises again (gcd at line 90); the unary minus (43) goes through the constructor and normalises again; operator- (50) is `a + -b`, that is 3 gcds. Duration::length() (duration.hpp:83-90) adds one per beat. The quotations from docs/performance.md are literal (lines 44 and 63). The impact is indeed inflated: (a) `validate` is not called from src/app, so the 3.0 ms are not on the editing path; (b) the 287 ms of the MIDI importer is the single-thread case, by default it is 40.5 ms; (c) on the real hot path (soundingNotes and sequence.cpp:476-478, about 5 gcds per note) the work is already spread over ~20 threads, so the saving in wall time per edit is on the order of tenths of a ms to ~1 ms out of ~40. The gcds are on small integers (a few ns each). It is a cheap and correct micro-optimisation, not a medium-impact problem. Corrections to the proposal: the `a.den_ == b.den_` path by itself does not avoid reducing (1/4 + 1/4 = 2/4); only the power-of-two case allows zero gcds with countr_zero (taking care with numerator 0 and negatives); with triplets (denominators 12, 24) a gcd is still needed, although gcd(num, g) is enough instead of gcd(num, den). The private non-normalising constructor for the unary minus is correct. Everything remains constexpr. The estimated figures (validate at ~1.7 ms) are not measured.

#### core/A5 · Every note and every beat carry a 24-byte std::vector for a curve that is almost always empty, and every beat has its own allocation for its notes

- **Where:** `src/core/include/elara/core/effects.hpp:58`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
std::vector<BendPoint> points_{};
```

**Why it matters.** BendCurve is half of Note (24 of 48 bytes, via NoteEffects::bend, line 98) and more than a third of Beat (BeatEffects::whammyBar, line 122: 24 of 64). On the large song that is 160k x 24 + 80k x 24 = 5.8 MB of null pointers over a model of ~15 MB, multiplied by every live copy (Document, app snapshot, clipboard, measures retained in undo). It also prevents Note from being trivially copyable: docs/performance.md attributes what remains of the 10.7 ms of copy to 'copying each note's (usually empty) bend curve'. BendPoint uses two ints (8 bytes) for a position 0..100 and some cents of +-2400. Separately, `std::vector<Note> notes` per Beat (measure.hpp:32) means ~80k heap allocations when copying the song, which is what dominates the copy time.

**Proposal.** Step 1 (S/M): store the curve behind a single pointer (null if empty; block with counter + 4-byte points: int8 position, int16 cents) keeping the BendCurve API: Note 48 -> 32 bytes and Beat 64 -> 48, about 3.8 MB less per copy of the large song. Step 2 (L): move the curves to a side table per Voice (the note stores an 8-16 bit index) so that Note is trivially copyable, and flatten the notes per voice (`std::vector<Note>` in Voice; Beat with offset + count) to go from ~80k allocations to one per voice: the full copy should drop to a few ms.

**Verification note.** The facts are true: BendCurve is a `std::vector<BendPoint>` (effects.hpp:58), held by value in NoteEffects::bend (98) and BeatEffects::whammyBar (122); BendPoint is two ints (16-21); Beat carries `std::vector<Note> notes` (measure.hpp:32); docs/performance.md:42-43 gives Note 48 B, Beat 64 B and attributes the rest of the copy to the empty curve. The arithmetic (160k x 24 + 80k x 24 = 5.8 MB) adds up. What does not hold: (1) the finding contradicts itself about what dominates the 10.7 ms of copy: it cites the document (the curve) and at the same time claims that the ~80k allocations of `notes` dominate; neither of the two is measured separately. (2) The impact: 4-6 MB per copy in a desktop application and only on the stress song is little; the copy time only matters because the app copies on every edit, and A1 solves that, not this. (3) Step 1 does not make Note trivially copyable (a pointer with a counter still has a copy constructor, and the counter would have to be atomic because the snapshots are read from workers), so it hardly improves the copy time. (4) "Keeping the BendCurve API" is not true: `points()` returns `const std::vector<BendPoint>&` (line 46) and with compact points something else would have to be returned; there are 5 uses in 4 files (sequence.cpp, musicxml_export.cpp, native_format.cpp, tab_staff.cpp), manageable but it is an API change. Step 2 (flatten notes per voice) touches `beat.notes` in all the importers, notation and editing and breaks the value types of ADR-0004: it is effort L with a benefit that overlaps with A1.

#### core/A6 · The undo history is bounded by number of steps (1000), not by retained memory

- **Where:** `src/core/include/elara/core/document.hpp:42`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
static constexpr std::size_t kDefaultUndoLimit = 1000;
```

**Why it matters.** The trimming in document.cpp:29 (`while (done_.size() > undoLimit_)`) only counts commands. A ReplaceMeasure retains one measure (~1-2 KB) and 1000 of them are ~2 MB, but coarse commands retain full copies: makeTranspose over the whole song generates one ReplaceMeasure per changed measure of each track (transpose.cpp:141-142), that is, a complete copy of the content (~15 MB) per step; makeRetune (retune.cpp:84-85) and setDynamic (entry.cpp:767-768, which walks to the end of the staff when the whole run shares a dynamic) retain a whole track; RemoveTrack, another. Twelve consecutive one-semitone transpositions are ~180 MB retained and the theoretical limit is 1000 x 15 MB. The redo stack (`undone_`) has no cap of its own either.

**Proposal.** Add to Command a `retainedBytes()` (cheap estimate: measures x beats x sizeof + notes) and trim `done_` by a byte budget in addition to the count (e.g. 64-128 MB configurable), always keeping at least the last step. As a complement, merge consecutive equivalent steps (consecutive transpositions of the same scope) into a single command that retains only the original state.

**Verification note.** True: kDefaultUndoLimit = 1000 (document.hpp:42) and the trimming in document.cpp:29-34 only counts steps. makeTranspose generates a ReplaceMeasure with a new copy for every changed measure (transpose.cpp:138-142), so transposing the whole song retains a complete copy per step; makeRetune (retune.cpp:77-85) and setDynamic (entry.cpp:744-768, which reaches the end of the staff if the whole run shares a dynamic) retain a staff. Qualifications: (a) RemoveTrack adds no memory: it moves the track into the command (ADR-0005, "the history holds exactly what the song no longer holds"), it only prevents freeing it; (b) the redo stack is in fact bounded: `undone_` only receives steps that leave `done_` and `execute()` empties it (line 28), so done + undone never exceeds the limit; (c) the scenario is rare: 12 transpositions of the whole stress song are ~180 MB on a desktop machine, and on normal songs ten times less; the theoretical limit of 1000 x 15 MB requires a thousand complete transpositions. It is a robustness limit, not a cost paid in normal use; the ~15 MB figure is estimated. On the proposal: `retainedBytes()` is feasible (compute it once after apply; for a group of 10,000 ReplaceMeasure it means walking all the content, ~1 ms) and must also count `undone_` and respect `savedDepth_` as the trimming loop already does. Merging consecutive transpositions is not a neutral optimisation: it changes what Ctrl+Z does (it undoes several at once) and ADR-0005 leaves the merging of edits as a pending product decision.

**Observations on this area (what is already well solved).**

Scope: files read in full: song.hpp/cpp, measure.hpp/cpp, effects.hpp, fraction.hpp, duration.hpp, document.hpp/cpp, commands.hpp/cpp, entry.cpp, validation.cpp, performance.hpp/cpp, parallel.hpp, clipboard.cpp, retune.cpp; by sections transpose.cpp, fret_placement.cpp, pitch.hpp, tuning.hpp. For A1 and A2 the call sites in src/app/src/score_document.cpp (lines 472 and 875-893) and a grep of forEachIndex across all of src were also consulted; the inside of audio/sequence.cpp and of notation was not read, so the thread counts of A2 outside core are estimates from the call signatures.
Well solved, leave alone:
- Editing commands: ReplaceMeasure, SetMeasureHeader, SetSongInfo, RemoveMeasures, Insert/RemoveTrack swap or move, they do not copy; RemoveMeasures reserves before moving (commands.cpp:29-30) and Document::execute/redo reserve the slot before apply() so that nothing fails halfway.
- Key entry (entry.cpp): each command copies a single measure (`measureAt`, line 62) and returns it in a ReplaceMeasure; these are microseconds (0.60 us measured for edit + undo). The cost per keystroke is not here but in what is recomputed afterwards (A1). dynamicAt/previousNote walk backwards only to the first note, and clampCaret/lastIndex add one measure: negligible.
- Validation: `validate` is sequential and complete (3 ms), but in the app it is not called per edit; only the CLI uses it (edit_commands.cpp:514, file_commands.cpp:96/204, song_report.cpp:114). It needs neither a cache nor threads.
- Tuning is fixed-capacity with no allocation; Pitch, Duration, Tuplet and FretPosition take 2-4 bytes; MeasureHeader groups its flags; Song::playOrder does reserve and has a 256x guard.
- threadsFor already avoids threads below a work threshold and limits to min(cores, jobs, 64); the exceptions are rethrown in thread order, and the result is deterministic because each job writes to its own index.
- The core has no mutable global state, locks or code that runs on the audio thread; no data races were found within the module (the soundingNotes jobs only share reads of the Song and write to their own `lists[i]`/`merged[i]`; what is pointed out in A4 is cache false sharing, not a race).
Minor observations that do not rise to a finding: Measure always reserves 4 voices (`std::array<Voice, kMaxVoices>`, 96 bytes per measure and staff, ~1 MB on the large song); Song::secondsAt, tempoAt and measureStart are O(measures) per call with Fraction arithmetic (no callers in a loop inside core; notation/system_layout.cpp:463 calls tempoAt and it would be advisable for the notation area to check whether this is per measure); struckStarts (entry.cpp:99) builds a std::map per measure in setDynamic.

### Audio

#### audio/A1 · The sequencer merges the per-track lists into a global list that the player splits again per track

- **Where:** `src/audio/src/sequence.cpp:696`
- **Axis:** speed · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** confirmed

```cpp
while (tracks.size() > 1) {
        std::vector<std::vector<NoteEvent>> merged((tracks.size() + 1) / 2);
```

**Why it matters.** Sequence already has the events sorted by track (`tracks[t]`, line 637) and merges them in 5 rounds (20 -> 10 -> 5 -> 3 -> 2 -> 1). Each round does `merged[i].resize(a.size() + b.size())` (line 707: initialises 320k x 32 B) and then the merge rewrites them: about 100 MB of memory traffic per sequence, and the last round is a single merge of 320k on a single thread. In the app the only consumer of `events()` is `detail::eventsByTrack` (playback.hpp:38), which undoes the merge as soon as it is loaded. Only the CLI and the tests read the global list. It happens on every edit: score_document.cpp:880 builds a new Sequence and audio_controller.cpp:87 calls Player::load on the main thread (player.hpp:23 documents it as the control thread).

**Proposal.** Keep in Sequence the per-track lists as the primary data (`eventsOfTrack(t)`, plus the metronome's) and have Player::load and OfflineRenderer consume them directly. Leave `events()` as a lazy merge (or a separate function) for the CLI and the tests. `operator==` would compare track by track, with the same result. It eliminates the 5 rounds, their threads and the subsequent distribution.

**Verification note.** Checked in src/audio/src/sequence.cpp:637-714: `tracks[t]` already comes out sorted by track and the loop at 696-711 merges it pairwise with `resize(a.size()+b.size())` (707, zero-initialises) + `std::ranges::merge`. With 20 tracks there are 5 rounds (with metronome, 21 lists, also 5). NoteEvent measures 32 B (sequence.hpp:56-64). Consumers of `events()` outside tests: only playback.hpp:38 (`eventsByTrack`, used by player.cpp:73 and offline_renderer.cpp:50) and the CLI (audio_commands.cpp:220-278); `eventAt` has no callers in src. It happens on the UI thread on every edit: `ScoreDocument::edited()` (score_document.cpp:875-883) builds the Sequence synchronously, even when only the mix changes. Qualifications: (1) the impact is not measured; the ceiling is a fraction of the 28 ms of sequencing (the last two rounds run on 1-2 threads over ~10 MB each; estimated at 5-10 ms), which is why it is lowered to medium. (2) Player::load only runs if `player_` already exists (after the first play). (3) The proposal is correct: the merge preserves the relative order of each list, so splitting the global list per track returns exactly `tracks[t]` and comparing track by track is equivalent to the current `operator==`. (4) Beware of the 'lazy merge': a mutable cache would break the class contract (sequence.hpp:93-94, read-only after construction, readable without locks); it must be a function that returns by value for the CLI/tests, not a const `events()` with a cache.

#### audio/A7 · noteOn of a plucked string: three passes over the delay line, a division and sin() per sample on the audio thread

- **Where:** `src/audio/src/synth.cpp:734`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
for (std::uint32_t i = 0; i < voice.length; ++i) {
                line[i] /= peak;
            }
```

**Why it matters.** Every guitar, overdrive or bass noteOn walks `voice.length` samples three times (lines 716, 728 and 733): 585 samples for a low E at 48 kHz, 1170 for a bass. It includes a division per sample and, in harmonics, a double `std::sin` per sample (lines 718-719), plus three `std::pow` (lines 29, 598 and 707). That is on the order of 3-12 µs per note (estimated). Chords from several tracks coincide in the same frame: 20 tracks x 6 notes gives a peak of ~0.5 ms inside a single block, noticeable with buffers of 64-128 frames (1.3-2.7 ms).

**Proposal.** A single pass that generates the excitation and accumulates sum, minimum and maximum; the peak after subtracting the mean is max(max - mean, mean - min). A second pass applies `(x - mean) * (1 / peak)`: 3 passes become 2 and the division becomes a multiplication. For the harmonic, rotate a phasor (cos/sin computed once) instead of `std::sin` per sample. `keyFrequency` and the velocity curve can be tables of 128 entries computed in the constructor.

**Verification note.** Confirmed in synth.cpp:716-736: three passes over `voice.length` (generation + mean, subtraction + peak, division), a double `std::sin` per sample only for Touch::Harmonic (718-719) and `std::pow` at 29, 598 and 707; it runs inside `noteOn`, which `playTrack` calls from the audio thread (playback.hpp:146). The lengths add up (~582 samples for low E at 48 kHz, ~1165 for bass E). The cost per note (3-12 µs) and the 0.5 ms peak are unmeasured estimates and the case of 120 attacks in the same frame is infrequent; low impact is correct. On the proposal: the peak computed as max(max - mean, mean - min) is bit for bit identical to the current one, because float subtraction is monotonic and symmetric. On the other hand `* (1 / peak)` does not give the same bits as `/ peak`: it remains deterministic, but changes the audio output relative to the current one; to preserve it it is enough to keep the division in the second pass (it vectorises just the same). The phasor for the harmonic also changes the values and only affects a rare touch. The tables of 128 entries must be computed in the Synth constructor with the same expression (not as a local static, whose initialisation guard could run and block on the audio thread); their gain is marginal compared with the passes.

#### audio/A8 · The retired Loaded is not freed while playback is paused

- **Where:** `src/audio/src/player.cpp:161`
- **Axis:** memory · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
retired_.store(current_, std::memory_order_release);
```

**Why it matters.** The audio thread leaves the previous song in `retired_` on its next callback, and only `tidy()` frees it, which `load()` and `AudioController::pollTransport` call. After an edit while paused, loadSong() calls load() and pollTransport() before the audio has done the exchange, and the timer stops (audio_controller.cpp:164-166, `if (!playing) { transportTimer_.stop(); }`). The old Loaded stays alive until the next edit or the next play: 7.7 MB of events plus up to 20 synthesizers (about 307 KB of delay lines per plucked track: 2402 floats x 32 voices) and their buffers, around 14 MB dead on the large song. It is bounded to one, it is not a leak.

**Proposal.** After `player_->load(...)`, schedule a `QTimer::singleShot` (50-100 ms) that calls `player_->tidy()`, or keep the transport timer one more tick while something remains to be freed (expose `bool Player::hasRetired() const` reading `retired_`).

**Verification note.** The flow was followed: `loadSong()` (audio_controller.cpp:81-96) does pause, seek(0), `load()` (which calls `tidy()` before publishing `pending_`, player.cpp:98-102) and `pollTransport()`, which calls `tidy()` again and, as it is not playing, stops the timer (164-166). The audio thread does the exchange on its next callback (player.cpp:159-164) and leaves the previous Loaded in `retired_`; nobody calls `tidy()` until the next edit, seek, play or stop. It stays bounded to one dead Loaded, as the finding says. The size is plausible: `strings_` = (48000/20 + 2) x 32 voices x 4 B, about 307 KB per plucked track (synth.cpp:548-549), plus the events and buffers. It only happens if `player_` already exists (after the first play). Proposal feasible and safe: `tidy()` belongs to the control thread. Use `QTimer::singleShot` with `this` as context and check `player_` inside. If the option of keeping the timer is chosen, the condition must include that `pending_` has still not been taken and have a cap, because if the device is not calling render() the exchange does not arrive and the 16 ms timer would spin indefinitely.

#### audio/A9 · position() can return the pre-seek position during a block

- **Where:** `src/audio/src/player.cpp:166`
- **Axis:** concurrency · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
if (const std::int64_t asked = seekTo_.exchange(-1, std::memory_order_acq_rel); asked >= 0) {
```

**Why it matters.** The audio thread sets `seekTo_` to -1 at the start of render(), but does not publish `position_` until the end (line 189, after synthesising the whole block). In that window, `position()` (lines 147-148) sees `asked == -1` and reads the old `position_`: the playback cursor jumps for an instant to the pre-seek position, which can trigger a scroll or page change of the score and its return. The window lasts as long as the block takes to synthesise (0.2-2 ms per block), so it affects a small percentage of the seeks polled by timer. `play()` also uses `position()` to decide whether to rewind (line 127).

**Proposal.** Publish before clearing: `asked = seekTo_.load()`; if `asked >= 0`, `frame_ = asked; position_.store(frame_)` and then `seekTo_.compare_exchange_strong(asked, -1)`. If the CAS fails, a newer seek has arrived, which is taken in the next render.

**Verification note.** Confirmed: `render()` sets `seekTo_` to -1 at line 166 and does not publish `position_` until 189, after synthesising the whole buffer; meanwhile `position()` (146-150) sees -1 and returns the pre-seek `position_`. The effect on `play()` (127) is also real: after reaching the end, a seek followed by play inside that window would see position >= duration and rewind to 0, overriding the seek. The window lasts as long as the callback and can only be seen by a tick of the 16 ms timer during playback; while paused it is not observed, because the controller's `seek()` polls immediately (still with `seekTo_` set) and the timer stops. That it causes a visible page change was not verified in the QML; low impact is correct. The proposal is correct and wait-free: publishing `position_` before clearing closes the window (the reader can only see -1 after the CAS, which comes after the store), and a single strong CAS that fails leaves the newer seek for the next render, as happens today with two consecutive seeks. It neither allocates nor blocks.

#### audio/A2 · eventsByTrack without reserve and duplicated events (FrameEvent) relative to Sequence

- **Where:** `src/audio/src/playback.hpp:40`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
result[event.track].push_back({.frame = std::llround(event.time * rate),
```

**Why it matters.** 320k push_back spread over 21 vectors without reserve: each vector (~16k elements) is reallocated dozens of times and leaves surplus capacity. FrameEvent takes 24 B (int64 + 1 + 1 + 2 + 4 + int + int), so that is 7.7 MB plus the slack, duplicating the 10.2 MB of `Sequence::events_` (NoteEvent 32 B) that stay alive in `sequence_`/`loaded_`. In addition there can be up to three Loaded at once (current_, pending_, retired_). It runs on the main thread on every Player::load (player.cpp:73).

**Proposal.** Minimum: a prior pass that counts events per track and an exact `reserve`. Better, together with A1: convert each per-track list with its known size, and shrink FrameEvent to 16 B (frame uint32 or relative, key and velocity uint8, kind and touch in 1 byte). Alternative without copying: have Loaded share a shared_ptr<const Sequence> and compute `llround(time * rate)` when dispatching (one multiplication per event).

**Verification note.** The code exists as is (playback.hpp:35-50): `result(tracks)` without reserve and push_back per event. FrameEvent measures 24 B (8+1+1+2+4+4+4) and NoteEvent 32 B; the duplication relative to `Sequence::events_` is real and there can transiently be three Loaded (current_, pending_, retired_). It runs on the control thread in Player::load (player.cpp:73). But the impact is inflated: it is once per load, not per frame; the geometric reallocations copy ~2-3 times the final size (about 20 MB of memcpy, a few ms compared with the 28 ms of sequencing) and the average slack is ~25% of 7.7 MB. Proposal: count + reserve is trivial and correct. Shrinking FrameEvent to 16 B requires saturating before narrowing: `Synth::noteOn` clamps key (synth.cpp:561) and NoteEvent.key is int; truncating to uint8 before the clamp would change the result for out-of-range keys, and `LoggedEvent.frame` is still int64. The alternative with shared_ptr<const Sequence> is only feasible together with A1 (per-track lists) and changes the signature of `Player::load(const Sequence&)`; `llround` when dispatching does not allocate, so it does not violate the audio thread rule.

#### audio/A3 · Every forEachIndex of the sequencer and of the offline render creates and destroys its threads

- **Where:** `src/audio/src/sequence.cpp:698`
- **Axis:** concurrency · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
core::forEachIndex(0, merged.size(),
                           core::threadsFor(0, total, kParallelEvents, merged.size()),
```

**Why it matters.** `forEachIndex` (core/parallel.hpp:44, `workers.emplace_back([&, t] {`) launches new std::thread on every call and joins. A Sequence of the large song invokes it at lines 469, 638 and once per merge round: on 16 cores that is 16 + 16 + 10 + 5 + 3 + 2 = 52 threads created per sequence (34 on 8 cores). At 30-60 µs per thread on Windows (estimated) that is 1.5-3 ms of the 28 ms measured, on every edit. `OfflineRenderer::render` (offline_renderer.cpp:86) does the same per call, and the CLI calls it once per second of audio (audio_commands.cpp:318-321): about 16k threads for a 1000 s song.

**Proposal.** A persistent pool in core (hw-1 workers, with the calling thread taking part) on which `forEachIndex` is implemented with the same semantics and the same deterministic order. In OfflineRenderer, keep the workers alive for the lifetime of the renderer. With A1 the merge rounds disappear and 2 parallel regions per sequence remain.

**Verification note.** True that `forEachIndex` (core/parallel.hpp:41-56) creates std::thread and joins on every call. The count adds up: 469 and 638 with min(cores, jobs), and rounds of 10+5+3+2 threads (the 1-job round runs inline, parallel.hpp:33). It actually falls short: `core::soundingNotes` (performance.cpp:118 and 134), which the constructor calls at line 465, also uses forEachIndex. Even so the cost is inflated for 'medium': the 30-60 µs per thread are an unmeasured estimate and give 1.5-3 ms of 28 ms (5-10%), and the creation overlaps with the work of the threads already launched. In OfflineRenderer (offline_renderer.cpp:84-88) it is at most 16 threads per second of rendered audio, negligible compared with synthesising 1 s of 20 tracks; moreover only the CLI and the tests use it, not the app. Proposal: feasible but with more risk than indicated. A global pool must admit several callers at once (score_document.cpp:451 and 472 launch open and layout with QtConcurrent while the UI thread sequences in `edited()`), nested calls and the rethrowing of the first exception in order. Determinism is not affected because each job writes to its own index. If A1 is applied the 20 threads of the merges disappear and the remaining gain is minimal.

#### audio/A4 · Muted tracks (mute, or not solo) are fully synthesised and then discarded

- **Where:** `src/audio/src/player.cpp:209`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
detail::playTrack(track.synth, track.events, track.next, frame_, out,
                              [](const detail::FrameEvent&) noexcept {});
```

**Why it matters.** renderBlock synthesises all the tracks and only afterwards `mixInto` (playback.hpp:106) discards those with level 0. With one track in solo out of 20, around 95% of the audio thread's work (noteOn with string excitation, per-voice loops, overdrive tanh) is thrown away. OfflineRenderer does the same (offline_renderer.cpp:86-88), and there the levels are fixed throughout the render, so a track at level 0 will never be heard.

**Proposal.** In Player: if `track.applied` and `target` are both {0,0}, do not synthesise. On entering that state, `synth.reset()` once; in each block, advance `track.next` with `firstAt(events, frame_ + n)`. When the mute is removed the notes already started do not sound until the next attack, the same semantics as seek() (player.hpp:59). The block in which the level changes is still rendered for the ramp. In OfflineRenderer: skip the tracks with level 0 when `logEvents` is false.

**Verification note.** Code confirmed: `renderBlock` (player.cpp:204-217) synthesises all the tracks and `mixInto` (playback.hpp:105-108) afterwards discards those with level 0; OfflineRenderer does the same (offline_renderer.cpp:86-96). Inflated impact: it does not improve the worst case (all the audible tracks must fit in real time anyway), only the average consumption with mute/solo; with A5's own estimate (about 5 M voice-samples/s) the whole audio thread is around a small percentage of one core, and a track without active voices is already cheap. There is no measurement indicating dropouts. The proposal changes audible behaviour and that has to be decided as a product matter: today, when the mute or solo is removed, the notes already started are heard immediately with a ramp; with the proposal they stay silent until the next attack. In addition the level {0,0} does not arrive only through mute/solo: `levelOf` gives 0 with volume 0 (playback.hpp:81-82), so dragging a fader down to 0 and back (`updateMix(mixes)` in previewMix) would cut the sustained notes. `firstAt` and `reset()` neither allocate nor block, so it is safe on the audio thread. The offline part benefits nobody as it stands: the only production user is the CLI and it always passes `logEvents = true` (audio_commands.cpp:305). Check test_player.cpp:166-204, which removes the mute after 0.7 s and requires signal.

#### audio/A5 · Per-sample voice loops: state in memory, branches and early exit prevent vectorisation

- **Where:** `src/audio/src/synth.cpp:325`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
sample += value;
        if (ended(voice, value, context)) {
            return;
        }
```

**Why it matters.** It is the dominant cost of the audio thread. In renderPartials, renderSaw, renderString and renderDrum every sample writes to `out` (float) and reads or writes float fields of `Voice` by reference; the compiler cannot prove that they do not overlap, so it reloads and stores all the state on every sample. In addition there are per-sample branches: `envelope()` depending on the stage, `voice.age % kTidyEvery` (lines 312 and 428), the `return` of `ended()`, and the partials loop with a variable counter `p < voice.partials` (line 316) over arrays that are already float[4]. Estimate: 20 tracks x ~5 voices x 48 kHz are about 5 M voice-samples/s on a single thread; in the worst case (32 piano voices per track) a track costs around 2% of a core.

**Proposal.** Copy the hot state into local variables on entry and write it back on exit. Always iterate over kMaxPartials partials (the unused ones have amplitude and phasor at 0 after `voice = Voice{}`) so that the inner loop has 4 fixed lanes (SSE). Split the sub-block into branch-free runs: until the end of the attack (number of frames computable from `attackStep`), until the next multiple of kTidyEvery and until `minimumLife`. Keep the per-sample follower with branch-free min/max and cut at the exact index where the voice ends, to keep the output bit for bit independent of the chunking.

**Verification note.** The cited code exists (synth.cpp:310-330, and analogous loops in renderSaw 345, renderString 361 and renderDrum 426) and it is true that `sample` (float&) can overlap with the float fields of `Voice` and `Context`, which forces reloading state, and that there are per-sample branches (`envelope`, `age % kTidyEvery` at 312 and 428, `ended`). What is exaggerated: (1) 'prevent vectorisation' attributes to aliasing what is a serial recurrence between samples (phasor, envelope, follower, all-pass and Karplus-Strong feedback); the only vectorisable axis is the 4 partials, and it only applies to Piano/Mallet/Organ, not to plucked string, which is the dominant timbre in a tablature app. For string, saw and drums the gain is limited to moving state into locals. (2) 'Dominant cost' and the percentages are unmeasured estimates; benchmarks exist (tests/bench/bench_audio.cpp:56-65) that are not cited. With its own figures the audio thread uses a small percentage of one core, so the absolute improvement is low. Feasibility: always iterating over kMaxPartials is correct (after `voice = Voice{}` at 594 the unused partials have re, im, amplitude and decay at 0 and the sum starts from +0), but the sum of the 4 lanes must be done in the same order so as not to change bits. The contract 'the output does not depend on how render() is chunked' (synth.hpp:52-53) is covered by bit-for-bit tests (test_synth.cpp:249 and 399-416, test_offline_renderer.cpp:226-245): chunking by end of attack, multiples of kTidyEvery and minimumLife and cutting at the exact index where the voice ends is feasible but delicate; the effort M is optimistic.

#### audio/A6 · NoteEvent (32 B) and PlayedNote (64 B) use size_t and int for 8-16 bit values

- **Where:** `src/audio/include/elara/audio/sequence.hpp:32`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t voice = 0;
    std::size_t pass = 0;
```

**Why it matters.** PlayedNote: 4 size_t + 2 double + 2 int + 2 bytes = 64 B, x 160k notes = 10.2 MB. NoteEvent (line 56-64: double + 2 bytes + int16 + float + size_t track + int key + int velocity) = 32 B, x 320k = 10.2 MB. That is 20 MB per Sequence, and on every edit the old and the new one coexist (score_document.cpp:880-881) and `operator==` walks them entirely when only the mix changed. The size also weighs on the merges, the sorts and eventsByTrack.

**Proposal.** PlayedNote to 32 B: track uint16, staff and voice uint8, pass uint32, start and end double, key and velocity uint8, touch, struck. NoteEvent to 24 B: track uint16, key and velocity uint8. It saves about 7.7 MB per Sequence (5.1 + 2.6) and reduces in proportion the memory traffic of comparison, merge and load. The accessors can keep returning size_t/int so as not to touch the callers.

**Verification note.** The sizes are correct: PlayedNote = 4 size_t + 2 double + 2 int + 2 bytes + padding = 64 B (sequence.hpp:31-45) and NoteEvent = 32 B (56-66); the proposed layouts give 32 B and 24 B and the saving of 5.1 + 2.6 MB adds up with 160k notes and 320k events (figures from the benchmark bench_audio.cpp:27). It is also true that in `edited()` the new and the old Sequence coexist and that the default `operator==` walks them entirely when they are equal (score_document.cpp:880-881). Inflated impact: it is about 7.7 MB in a desktop app and 25-50% less traffic in steps that do not dominate the 28 ms. Two errors in the proposal: (1) there are no 'accessors' that could keep returning size_t/int: they are structs with public fields that are read directly by the CLI (audio_commands.cpp:223-240, 270-281), offline_renderer.cpp:31-38, the tests and the designated initialisers of sequence.cpp:522-531, where size_t to uint16/uint8 is a narrowing conversion and does not compile without casts. (2) `key` can be outside 0..127: the sequencer filters it explicitly (sequence.cpp:126, 150, 657 and harmonicKey at 387), so a uint8 needs saturation or a sentinel so as not to change which notes generate events. Feasible with those corrections and checking the limits (tracks < 65536, pass in uint32).

**Observations on this area (what is already well solved).**

Scope: read-only; nothing was built or measured. That the Sequence of each edit is built on the main thread (score_document.cpp:880) was not checked; that Player::load runs on it is documented in player.hpp:23.
Well solved, leave alone:
- Handing the song over to the audio thread (player.cpp): atomic pointers pending_/retired_, levels in std::atomic<float>, and render() without allocations or locks. No data races or dangling pointers were found in latest_.
- JUCE callbacks (system_audio.cpp, microphone_input.cpp): buffers sized in audioDeviceAboutToStart, no allocating or blocking; setSource only with the callback withdrawn.
- SampleRing: correct lock-free SPSC (acquire/release, power-of-two mask). written_, read_ and dropped_ share a cache line, but the producer writes about 100 times per second: the false sharing is irrelevant.
- Synth: no allocations after construction (strings_ and scratch_ preallocated), mixInto with a click-free ramp, limit() with tanh only above the knee.
- A single audio thread for all the tracks is enough: the estimated typical load is a few percentage points of one core. Parallelising the callback is not recommended.
Minor observations that are not among the 9 findings:
- Denormals: there is no ScopedNoDenormals or FTZ/DAZ in any callback. The synthesizer covers it by hand (kTiny, flushed(), end of voice at kSilence). The only remainder found is `voice.toneDelta *= voice.toneSweep` (synth.cpp:437), which is never cleaned up and can enter subnormals at the end of a Chinese cymbal (key 52), for a few tens of ms. Setting FTZ/DAZ at the start of the callback (system_audio.cpp:49) would make it robust and would allow removing the per-sample `flushed` in synth.cpp:882.
- Overdrive: `std::tanh` per sample (synth.cpp:881) even with the track silent and paused; estimated at 1-2 ms per second and track. It would be enough to skip it when there are no active voices and toneState_ is 0.
- Tuner: YIN accumulates in double over float samples (pitch_detector.cpp:29-36), which halves the SIMD width, and runs from the timer of TunerController::poll (tuner_controller.cpp:177). Worst case per detection: 1601 lags x 1600 samples, about 30 detections per second. Tuner::push shifts the whole window (3201 floats) on every push (tuner.cpp:35): negligible.
- OfflineRenderer::renderRest (offline_renderer.cpp:118) reserves tracks x frames x 4 B, plus mix_ and out: on the order of 4 GB for 1000 s and 21 tracks. Today only the tests use it; the CLI renders in blocks of 1 s. If it is used for exporting, it should chunk internally.
- Sequence constructor: sounding, timings, articulations, writtenAs, head and ends stay alive during the merge, with a transient peak of tens of MB; they can be released before the events phase.
Main files reviewed:
- src/audio/src/player.cpp
- src/audio/src/playback.hpp
- src/audio/src/synth.cpp
- src/audio/src/sequence.cpp
- src/audio/include/elara/audio/sequence.hpp
- src/audio/src/offline_renderer.cpp
- src/audio/src/pitch_detector.cpp
- src/audio/src/tuner.cpp
- src/audio/src/sample_ring.cpp
- src/audio/backend_juce/src/system_audio.cpp
- src/audio/backend_juce/src/microphone_input.cpp
- src/core/include/elara/core/parallel.hpp (forEachIndex)
- src/app/src/audio_controller.cpp and score_document.cpp (call sites only)

### IO: MusicXML and containers

#### io-xml/X1 · Importing MusicXML: pugixml parsing is single-threaded and is already ~85% of the time

- **Where:** `src/io/src/musicxml.cpp:1994`
- **Axis:** speed · **Impact:** medium (initially proposed: high) · **Effort:** L · **Verdict:** partial

```cpp
const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
```

**Why it matters.** docs/performance.md puts the import of the large song (33 MB) at 118 ms and notes that 'pugixml alone parses the text in about 100 ms' and that 'what is left is mostly the XML parse'. The whole document is parsed into a single xml_document before distributing the parts among threads (importScore, line 1917), so the part that is already parallelised is only ~15-20 ms and the rest is serial (Amdahl). In addition the full DOM stays alive until the end of importScore.

**Proposal.** Split the text by the top-level <part ...>...</part> elements (linear search for '<part ' / '</part>'; if CDATA or a comment containing '<part' appears, fall back to the current path) and parse each run into its own pugi::xml_document inside the same job that already reads the part (parse + layoutPart + PartReader::read). The header (part-list, identification, credit) is parsed separately with parse_fragment. Part 0 still goes first to write ScoreState, but its parsing can overlap with that of the others. With load_buffer_inplace on disjoint ranges there are no shared writes. Estimate: ~100 ms of parsing becomes ~100/N + the largest part (20 equal parts, 8 cores: ~15 ms), total import on the order of 35-45 ms; and each part's DOM can be freed when its reader finishes, lowering the peak.

**Verification note.** Checked: musicxml.cpp:1994 does a single document.load_buffer over the whole text before importScore; readPart(0) goes serially (1912) and only parts 1..N-1 go through forEachIndex (1917). The DOM and the text stay alive until importMusicXml returns. docs/performance.md (rows 52-54) gives 265 -> 238 -> 118 ms and says 'pugixml alone parses the text in about 100 ms' and 'what is left is mostly the XML parse', so the ~85% is coherent (238-100 = 138 ms of reading on one thread, spread out it gives ~18 ms). Impact lowered: in the app the import runs on a worker thread (score_document.cpp:451, QtConcurrent::run -> openAndLayOut -> io::openFile), it does not block the UI, it is once per open and the case is an extreme 33 MB file; going from ~118 to ~50 ms is load latency, not fluidity. The proposal is feasible but has more edges than it states: (1) the job of a part i>=1 cannot 'parse and read' in the same step because the reading needs the ScoreState that part 0 writes: a parallel parsing phase for all the runs is needed and then the current reading; (2) the split header is left with <score-partwise> unclosed and pugixml gives status_end_element_mismatch even if parse_fragment is used: it must be copied and a closing tag appended; (3) load_buffer auto-detects encoding (UTF-16, latin1 by declaration): splitting by ASCII bytes only works for UTF-8, the rest must fall back to the current path; (4) '<part ' does not cover '<part\n', '<part\t' or '<part>' and it must be validated that after the last </part> only the closing tag remains; (5) the message 'invalid XML at byte {}' needs the rebased offset and choosing the error with the lowest offset so as not to depend on the threads; (6) a document badly nested across runs could be accepted where today it is rejected; (7) load_buffer_inplace requires a mutable buffer and the public API receives a const string_view: without X4 each run would have to use load_buffer (a copy per run, in parallel). The estimate of 35-45 ms is optimistic: pugixml parsing is memory-bound and does not scale linearly; 45-60 ms is reasonable. Effort L on an untrusted input path.

#### io-xml/X2 · Exporting .mxl: single-threaded deflate over the whole text, plus two copies of the resulting archive

- **Where:** `src/io/src/archive.cpp:73`
- **Axis:** speed · **Impact:** medium (initially proposed: high) · **Effort:** L · **Verdict:** partial

```cpp
written = written && mz_zip_writer_add_mem(&zip, name.c_str(), entry.content.data(),
```

**Why it matters.** The XML is generated in 88 ms with all the cores, but the .mxl takes 391 ms: ~300 ms are a single mz_zip_writer_add_mem (level 6) on one thread over ~60 MB of compact text. To compress, all the parts must first be concatenated into a single std::string (musicxml_export.cpp:1731), and when finished the archive is copied twice: bytes.assign(begin, begin + size) (archive.cpp:82) and result.data.assign(bytes.begin(), bytes.end()) (musicxml_export.cpp:1747), with the uncompressed text still alive. miniz's initial heap is requested as total / 4 + 4096 (line 66): ~15 MB for a 566 KB result.

**Proposal.** Compress in chunks in the same threads that write each part (pigz technique): each job does a raw deflate of its text with tdefl and ends with TDEFL_SYNC_FLUSH (the last chunk with FINISH), computes its mz_crc32 and frees the text; afterwards the streams are concatenated in score order, the CRC is combined (crc32_combine, ~30 lines, miniz does not provide it) and the already-compressed entry is added with mz_zip_writer_add_mem_ex(..., MZ_ZIP_FLAG_COMPRESSED_DATA, uncomp_size, crc). The chunking is per part, not per thread, so the bytes remain the same with any number of threads. Estimate: ~300 ms of deflate become ~300/N (8 cores: ~40-50 ms), total .mxl ~130 ms; the concatenation of ~60 MB disappears and the peak drops to the sum of compressed chunks + the texts in progress. Along the way, have writeZip return miniz's buffer without copying it (or write directly to the file with the m_pWrite callback) and have ExportResult not require the second copy into a std::string. The same writeZip serves encodeNative (.elara).

**Verification note.** Checked: archive.cpp:73 calls mz_zip_writer_add_mem once per entry, serially, with level 6 by default (archive.hpp:57); the whole compact XML is concatenated beforehand (musicxml_export.cpp:1730-1732) and passed as a single entry (1745). performance.md row 58: 391 ms against 88 ms of the indented XML in parallel, so ~300 ms are deflate + CRC on one thread: the main claim is true. Inflated parts: the 'two copies of the archive' (archive.cpp:82 and musicxml_export.cpp:1747) copy the ALREADY compressed archive, 566 KB in the benchmark: negligible cost; and the initial total/4+4096 heap (~15 MB) is a reservation that is not touched, also reasonable because the doc itself warns that real songs compress much less. Scope: exportMusicXml/exportMusicXmlFile are only called from the CLI (src/cli/src/file_commands.cpp:242); the app does not export MusicXML (in src/app/src only io::openFile and io::saveNative appear), so today there is no interactive path that pays the 391 ms: medium impact, not high. Proposal technically correct (independent raw deflate chunks ended in SYNC_FLUSH and the last in FINISH, concatenated; crc32_combine; mz_zip_writer_add_mem_ex with MZ_ZIP_FLAG_COMPRESSED_DATA), with qualifications: miniz comes from vcpkg and is not in the tree, so the signature/flag could not be verified in the repo; for the bytes to be equal with any number of threads the threads<=1 path (1716-1720, which today writes directly into xml) must chunk in exactly the same way, and there is no test that compares .mxl bytes across threads (the one at test_musicxml_export.cpp:728 only covers uncompressed text); each job needs its tdefl_compressor (~300 KB) on the heap, not on the stack. It does not work as is for encodeNative: .elara is a single song.json entry without parts, there fixed-size chunking would be needed.

#### io-xml/X6 · forEachIndex creates and destroys threads on every call and the calling thread does not work

- **Where:** `src/core/include/elara/core/parallel.hpp:44`
- **Axis:** concurrency · **Impact:** medium · **Effort:** M · **Verdict:** partial

```cpp
workers.emplace_back([&, t] {
```

**Why it matters.** io/parallel.hpp does not duplicate anything: it only does 'using core::forEachIndex; using core::threadsFor;'. The cost is in core: every call launches 'threads' new std::thread (up to hardware_concurrency, cap 64) and the caller stays blocked in join, that is N+1 threads for N of work. Creating and joining a thread on Windows costs on the order of 50-100 us, 1-3 ms per call on a machine with 16-32 threads: irrelevant in a 118 ms import, but the same function is used at 26 points (io, audio/sequence, notation/page_layout and system_layout) with operations of 3-30 ms, and since there is no common pool two simultaneous operations (e.g. opening on the app's worker thread while laying out or painting) each add up all the cores.

**Proposal.** Cheap step: launch threads-1 threads and run the same 'next++' loop on the calling thread (same semantics and same order of errors). Full step: a lazy shared pool in core (threads = hardware_concurrency, created once) into which forEachIndex enqueues the loop, with the caller taking part; this eliminates the creation per call and bounds the oversubscription between modules. The distribution by atomic index (one part per job) is adequate and should not be touched.

**Verification note.** Checked: forEachIndex (core/parallel.hpp:39-56) creates 'threads' std::thread per call, the caller only joins (N+1 threads) and io/parallel.hpp is two usings. Correction: it is not 26 points; there are 13 calls to forEachIndex in src (io 5, audio/sequence 3, audio/offline_renderer 1, core/performance 2, notation 2); 28 is the textual count of forEachIndex|threadsFor including README, definition and using. Some are in a loop (sequence.cpp:696-711, one call per mix round), so building a Sequence launches on the order of 60-100 threads, and laying out a 500-measure staff launches ~16 (system_layout.cpp:593) + ~24 (page_layout.cpp:298) for a 3.5 ms operation. That makes a noticeable weight plausible there (the doc gives 9.4 ms on one thread against 3.4-3.6 ms on 12 cores, only 2.7x), but the cost per thread (50-100 us) is an unmeasured estimate in the repo and part of the startup runs on the new thread, not on the caller. The claim of oversubscription between simultaneous operations is reasonable (the app uses QtConcurrent to open, lay out and paint) but not demonstrated. No call is on the real-time audio thread (offline_renderer is offline rendering), so the rule of not allocating or blocking is not affected. Cheap step (threads-1 threads and the caller working) correct and without risk. Shared pool feasible with conditions: it must admit several concurrent callers, not block if it is ever nested, respect the explicit 'threads' argument that the determinism tests use ({1,2,3,8}), keep 'first exception in order' and handle static destruction; it must never be called from the audio callback. Measure before adopting the pool.

#### io-xml/X3 · Parallel MusicXML export: the final join copies the 124 MB and keeps two copies alive

- **Where:** `src/io/src/musicxml_export.cpp:1731`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** confirmed

```cpp
for (const std::string& text : texts) {
            xml += text;
        }
```

**Why it matters.** On the path with threads every part is written into texts[i] and then copied whole into xml; texts is not freed until leaving the block, so at the end of the loop ~124 MB (texts) + ~124 MB (xml) = ~248 MB coexist, touched for a 124 MB file. The copy is a 124 MB memcpy over freshly reserved pages (xml.reserve at line 1703): on the order of 15-30 ms between copy and page faults, against the 88 ms total of the export, and it is serial.

**Proposal.** Minimum (S): free each chunk as soon as it is copied (std::string().swap(texts[i]) inside the loop), which leaves the peak at ~124 MB + one part. Better (M): do not concatenate for the file path: exportMusicXmlFile writes into the .saving file the header, each texts[i] in order and the closing with successive out.write calls (writeFileAtomically accepting a span of string_view), eliminating the 124 MB memcpy and one of the two copies; exportMusicXml (in-memory API) can keep concatenating.

**Verification note.** Checked: on the path with threads every part is written into texts[i] (1724-1729) and then copied to xml with xml += text (1730-1732); texts lives until the brace at 1733 and xml is reserved whole at 1703, so the two copies coexist (~124 + ~124 MB in the benchmark). The copy is serial. The cost of 15-30 ms is not measured, but it is plausible and even conservative: the export only drops from 155 to 88 ms with 12 cores, a sign that what remains is serial and memory-bound (page faults over 124 MB freshly reserved). Impact lowered to low because of scope: the MusicXML export is only invoked from the CLI (file_commands.cpp:242), not from the app, and the 248 MB peak is transient and only on an extreme song (20 tracks x 500 measures with TAB). Proposal correct: freeing texts[i] after copying it is one line and leaves the resident peak at ~124 MB + one part; the variant M (writing header, chunks and closing with successive writes into the .saving file) is the only one that eliminates the memcpy, forces an internal path that returns the chunks because ExportResult.data is a std::string, and does not apply to the .mxl path, which needs the contiguous text for writeZip unless X2 is done.

#### io-xml/X4 · Import: load_buffer duplicates the text even though the buffer is already ours and disposable

- **Where:** `src/io/src/musicxml.cpp:2026`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** confirmed

```cpp
[&options](const std::string& xml) { return importMusicXml(xml, options); });
```

**Why it matters.** pugi::xml_document::load_buffer works on an immutable buffer and makes a private copy. On both file paths the buffer is a temporary of our own: the std::string extracted from the .mxl (line 2026) or the vector from readFile (line 2034, 'const std::vector<std::uint8_t>& bytes'). On the large song the following coexist: 33 MB of the file + 33 MB of pugixml's copy + the DOM (several times the text) + the model; the copy also costs a reservation and a 33 MB memcpy (a few ms). With the input limit of 512 MB (kMaxEntryBytes) a hostile .mxl duplicates up to 1 GB in text alone.

**Proposal.** Add an internal path importMusicXmlOwned(char* data, size_t n) that uses document.load_buffer_inplace(data, n) and call it from importMusicXmlFile (taking the vector by value/mutable in the and_then) and from the .mxl path (the std::string returned by mxlScore is already mutable). The public API importMusicXml(std::string_view) keeps load_buffer. It saves 33 MB of peak and the copy; it combines with X1 (in-place parsing by runs).

**Verification note.** Checked: importMusicXml (1994) uses load_buffer, which makes a private copy of the text; on the .mxl path the std::string returned by mxlScore is a temporary of our own (2025-2026) and on the file path the vector from readFile as well (2034), both received by const&. File + pugixml copy + DOM coexist. Impact lowered: it is 33 MB transient over a peak dominated by the DOM (several times the text) and a few ms (reservation + memcpy + page faults, perhaps 5-10 ms out of 118); in the hostile 512 MB case the DOM would weigh several GB, the copy is the least of it. Proposal feasible and correct: load_buffer_inplace with a buffer that outlives the document, which here is destroyed before returning; and_then on an rvalue expected allows receiving the value by value or &&. Qualifications: importMusicXmlFile has to be restructured, as today it goes through importMusicXmlBytes(const span); it only saves the copy if the text is UTF-8 (in another encoding pugixml converts into a new buffer anyway, which is still correct); and the current benchmark (bench_io.cpp:105) calls importMusicXml(string), which would keep load_buffer, so the improvement will not be seen in that row unless a per-file measurement is added.

#### io-xml/X7 · Read buffers are zero-filled before being entirely overwritten

- **Where:** `src/io/src/archive.cpp:40`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
```

**Why it matters.** ZipReader::entry zeroes the whole destination and immediately afterwards mz_zip_reader_extract_to_mem overwrites it; readFile does the same with 'std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));' (line 127) before in.read. It is an extra memory pass per file: for the 33 MB of MusicXML about 3-6 ms (write + page faults brought forward), and all the formats (.elara, .gp, .mxl, MIDI) pay it because both functions are common.

**Proposal.** In entry(): content.resize_and_overwrite(n, [&](char* p, std::size_t n) { ...extract_to_mem(p, n)...; return n; }) (C++23, already the repo's standard). In readFile(): return an uninitialised buffer (std::string with resize_and_overwrite or std::make_unique_for_overwrite<std::uint8_t[]> + size); along the way that mutable buffer is the one that X4 needs for load_buffer_inplace.

**Verification note.** Checked: archive.cpp:40 builds std::string(n, '\0') and line 41 overwrites it entirely with mz_zip_reader_extract_to_mem; readFile (127) creates vector<uint8_t>(size) zeroed before in.read (129). It is a real extra pass, but small: the memset of 33 MB is ~2-4 ms (the page faults would be paid anyway on reading), somewhat less than the 3-6 ms cited, and it does not enter the 118 ms row because the benchmark imports from an in-memory string. Scope correction: readFile is only used by MusicXML (musicxml.cpp:2034) and .elara (native_format.cpp:998); Guitar Pro (guitar_pro.cpp:1445-1450) and MIDI (midi_import.cpp:1283-1288) read with istreambuf_iterator, byte by byte, which is worse and is not in the finding. ZipReader::entry is indeed common to .elara, .mxl and .gp; MIDI uses neither. Proposal feasible (C++23 fixed in CMakeLists.txt:9) with one condition: the resize_and_overwrite operation cannot throw (a precondition of the standard), so the extraction failure must be stored in a variable, return 0 and throw FormatError outside the lambda. Changing the return type of readFile touches its two callers; returning std::string with resize_and_overwrite is the simplest and serves X4.

#### io-xml/X9 · std::format with an allocation per percussion note (and per bend/tempo) in the writer

- **Where:** `src/io/src/musicxml_export.cpp:1332`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
xml.attribute("id", std::format("{}-I{}", id_, midi + 1));
```

**Why it matters.** The rest of the XmlWriter writes with std::to_chars and a direct append to the buffer, without temporaries; this is the only point on the per-note path that builds a std::string with std::format (an id of 6-8 characters, fits in SSO but pays the formatting, on the order of 100-200 ns per note). The same, per less frequent element, with decimal() = std::format("{}", value) in bend-alter (lines 1519 and 1526) and per-minute (1031). On a drum track of 8k-16k notes that is 1-3 ms inside a job that is around 4-8 ms per part: it may be the slowest part and therefore the one that sets the wall time.

**Proposal.** Write the attribute in pieces: out_ += id_; out_ += "-I"; appendNumber(midi + 1) (a method attributeId(prefix, number) in XmlWriter), or precompute in PartWriter a table of ids per drum key (drumKeys_ already exists). For decimal(), add to XmlWriter an endDecimal/textDecimal with std::to_chars(double) on a stack buffer.

**Verification note.** Checked: musicxml_export.cpp:1332 calls std::format("{}-I{}", id_, midi + 1) for every note when percussion_ is true; the rest of the XmlWriter uses std::to_chars and append (136-140). decimal() = std::format("{}", value) (183-185) is used in bend-alter (1519, 1526), per-minute (1031, 1041) and volume/pan (512, 515), per infrequent element. Qualifications: the benchmark is of 20 guitars, without percussion, so this cost does not appear in any measured figure; the claim that the drum track 'may set the wall time' is speculative, because a drum part writes a single staff and a guitar two (staff + TAB, ~7.7 ms per part on one thread), so with 100-200 ns extra per note it would still be below a guitar with the same notes. In addition the export is only invoked from the CLI. Proposal correct and without a change of bytes: id_ is "P{n}" and needs no escaping, and std::to_chars(double) in shortest form produces exactly the same text as std::format("{}", double) by specification, so the round-trip and the determinism are preserved. Gain of 1-2 ms on a large drum track: low.

#### io-xml/X5 · Export: the reservations do not take compact mode or tracks without TAB into account (up to 2x too much)

- **Where:** `src/io/src/musicxml_export.cpp:1726`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
texts[i].reserve(songScan.work[i] * kBytesPerItem * 2);
```

**Why it matters.** kBytesPerItem = 260 is measured on the indented text with staff + TAB (480k items x 260 = 124 MB). But the per-thread reservation always multiplies by 2 even if the track has no TAB (the total at line 1699 does distinguish 'tab ? 2 : 1'), and neither this nor xml.reserve(work * kBytesPerItem + 4096) (line 1703) look at options.compressed, when the compact XML takes half ('halves what deflate reads'). In .mxl ~124 MB + ~124 MB are reserved to write ~60 MB + ~60 MB; on Windows large heap reservations count as committed memory even if they are not touched. In addition xml is reserved whole even though on the parallel path it only receives the header until the join.

**Proposal.** Compute bytesPerItem according to the mode (e.g. 260 indented, ~130 compact, measured the same way as the current one) and use the same 'tab ? 2 : 1' factor per track in texts[i].reserve. On the parallel path, reserve xml just before joining with the exact sum of texts[i].size() (or do not reserve it if X3/X2 is applied).

**Verification note.** Checked: 1726 always reserves work[i]*260*2, while the total at 1699 does use 'tab ? 2 : 1'; neither that reservation nor xml.reserve (1703) look at options.compressed; SongScan.work is beats + notes (392), and 240k items x 2 x 260 = 124.8 MB adds up with the indented text. xml is reserved whole before knowing whether it will go through the parallel path. But the real cost is almost nil: reserve does not touch the pages; on Windows a large reservation only adds commit charge, not resident memory or time. In the benchmark all the tracks carry TAB, so the x2 per track is exact there; the excess only appears in compact mode (~2x of virtual reservation) and in tracks without TAB. Moreover it is a CLI-only path. Risk of the proposal: falling short is worse than overshooting (reallocation and copying of strings of tens of MB with 1.5x growth in MSVC), so the compact factor must be measured and rounded up. Reserving xml just before joining with the exact sum of sizes is correct and without risk.

#### io-xml/X8 · layoutPart walks all the notes of every pitched part only to discard <unpitched>

- **Where:** `src/io/src/musicxml.cpp:482`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** partial

```cpp
unpitched = std::any_of(notes.begin(), notes.end(), [](pugi::xml_node note) {
                return static_cast<bool>(note.child("unpitched"));
```

**Why it matters.** The '!unpitched' cut-off only saves work on percussion parts. On a pitched part (almost all of them) the pre-pass visits all the <note> of all the measures and, in each one, child("unpitched") compares the name of all its children without finding it: it is a second full pass over the DOM, precisely what scanNote avoided in the reading. On the large song that is ~320k <note> (staff + TAB) x ~10-15 string comparisons; on the order of 10-30 ms of CPU spread across threads, and serial for part 0.

**Proposal.** Decide percussion without walking notes when there is already enough signal: the 'percussion' clef in the <attributes> (already read in the same loop) or midi-unpitched / channel 10 in the <score-part> (readPartList already reads it); only if there is no signal, look at the notes and stop at the first measure with pitched notes (<pitch>) or at the first <unpitched>. Alternative without changing semantics: have the pre-pass use the same iteration over children as scanNote and cut off as soon as it sees <pitch> or <unpitched> in each note instead of walking them all.

**Verification note.** Checked: layoutPart (479-485) walks, while unpitched is false, all the <note> of every measure and calls note.child("unpitched"), which is a linear sweep of children; on a pitched part it never cuts off, and scanNote (747) documents precisely that cost. Figures inflated about 4x: the import benchmark file (bench_io.cpp:26-72) has 160k <note> (20 x 500 x 16, a single staff with string/fret in <technical>), not 320k, and each note has 5-6 children (chord?, pitch, duration, voice, type, notations), not 10-15; moreover no name begins with 'u', so every comparison fails on the first character. What remains is on the order of 1M cheap comparisons: a few ms of CPU spread across threads, ~1 ms of wall time at most (part 0 serial, tenths). Low impact, almost negligible. On the proposal: skipping the sweep when a 'percussion' clef has already been seen is safe (unpitched only feeds 'percussion', 493-497); on the other hand stopping at the first measure with <pitch> or relying on midi-unpitched/channel 10 CHANGES the semantics (a part with pitched notes at the start and <unpitched> afterwards is percussion today) and could alter import results. The alternative without a semantic change (walk the children and cut at the first pitch, unpitched or rest of each note) is the correct one; <rest> must be included in the cut-off.

**Observations on this area (what is already well solved).**

Read: src/io/src/musicxml.cpp (part reader, build, importScore, mxlScore and public entry points), musicxml_export.cpp (XmlWriter, scan, exportMusicXml/File), musicxml_shared.hpp, archive.cpp/.hpp, open.cpp, parallel.hpp, warnings.hpp (by grep) and core/parallel.hpp.
Well solved, leave alone:
- open.cpp: detection by extension only (lowercase), a single read of the file, no repeated probing; the result is moved.
- io/parallel.hpp is NOT a duplicate: it is two 'using' declarations over core::forEachIndex/threadsFor. The parallelism threshold (1 MiB of text on import, 20,000 items on export) avoids threads on small files.
- No visible races in importScore: part 0 is read before launching threads and is the only one that writes ScoreState; warnings[i] and readers[i] are per index and are merged in score order.
- XmlWriter: to_chars + direct append, escaping by runs, no ostringstream or std::format on the hot path (except X9). Reader: scanNote in one pass, per-measure buffers reused, reserve of beats and chords.
- Deflate level 6 and compact XML in archives: already measured (level 1 saves 40 ms and gives 5.5 MB against 566 KB); do not change the level, parallelise instead (X2).
- Anti zip-bomb guard (kMaxEntryBytes and a 1 MiB limit for container.xml), atomic write with .saving + rename, std::format messages only on error paths.
- In the app, openFile is called from openAndLayOut (score_document.cpp:149), documented as a worker thread: the import does not block the UI thread.
How many copies coexist when exporting: uncompressed and with threads, 2 copies of the text (texts + xml, ~248 MB) until leaving the block and 1 when writing; in .mxl, 2 copies of the compact text (~60 MB each) during the join, 1 during deflate, plus 3 successive copies of the compressed archive (miniz heap, writeZip vector, ExportResult std::string; two at once at most). When importing: file (33 MB) + pugixml's internal copy (33 MB) + DOM + model; for .mxl the original zip is added.

### IO: Guitar Pro

#### io-gp/GP3 · DOM with default flags: a separate PCDATA node for every text leaf of the GPIF

- **Where:** `src/io/src/guitar_pro.cpp:1354`
- **Axis:** memory · **Impact:** medium · **Effort:** S · **Verdict:** confirmed

```cpp
pugi::xml_document document;
```

**Why it matters.** GPIF is almost all leaves of the type `<Fret>0</Fret>`, `<String>0</String>`, `<Number>40</Number>`, `<Step>E</Step>`: with parse_default each one costs two nodes (element + PCDATA child). With 160k notes and several leaves per note that is on the order of a million extra nodes (tens of MB of DOM and its reservations in the serial parse). The DOM lives until the end of importScore, at the same time as the already built Song.

**Proposal.** Parse with `pugi::parse_default | pugi::parse_embed_pcdata`: the text is stored in the element itself and the child node is saved. The reader always accesses the text with `node.text().get()` / `text().as_double()` (textOf, line 74), which is compatible with that flag; validate with the existing import tests. Optionally remove parse_eol/parse_wconv_attribute if they are not needed.

**Verification note.** Checked: `pugi::xml_document document;` (1354) + load_buffer without options = parse_default, which creates a PCDATA node for every leaf with text. In the reference GPIF (bench_io.cpp:182-240) there are ~590k leaves (3 per note x 160k, Notes per beat, Beats per voice, Clef/Voices per measure) x 64 B of xml_node_struct = ~38 MB less DOM, not 'a million' but the same order; in real files there are more leaves per note. It also attacks what performance.md:67 identifies as the remaining dominant cost (the XML parse). Compatibility verified: all the text is read with textOf() = node.text().get() (line 74) or text().as_double (434, 936); there are no uses of child_value(), first_child() or node.value() in the file (only attribute().value()), and BeatFields/NoteFields iterate children by element name. CDATA (names, title) is not affected. Qualifications: (1) the optional part of removing parse_eol/parse_wconv_attribute was already measured and 'gained nothing' (performance.md:67), and it would also change multiline text: discard it; (2) it is transient memory of an import on a worker thread, not a cost per edit; (3) the installed pugixml version could not be checked (it comes from vcpkg; the flag has existed since 1.8). No effect on determinism or threads.

#### io-gp/GP1 · pugixml copies the whole GPIF: load_buffer instead of load_buffer_inplace on the std::string that is already ours

- **Where:** `src/io/src/guitar_pro.cpp:1355`
- **Axis:** memory · **Impact:** medium (initially proposed: high) · **Effort:** S · **Verdict:** partial

```cpp
const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
```

**Why it matters.** load_buffer reserves an internal buffer and does a memcpy of the whole XML before parsing. The caller already has the text in a std::string of its own and mutable (`score`, lines 1399 and 1418) that is not used again, so during the whole import two copies of the GPIF coexist (N MB + N MB) plus the DOM and the Song. For a GPIF of tens of MB that is tens of MB of peak and an avoidable memcpy + large reservation in the serial phase (the one that does not scale with threads).

**Proposal.** Have importScore receive the std::string by value/rvalue (`importScore(std::move(*score), parts, options)`) and parse with `document.load_buffer_inplace(score.data(), score.size())` keeping the string alive alongside the document. importGpif(std::string_view) can keep load_buffer because it does not own the buffer.

**Verification note.** Checked: guitar_pro.cpp:1355 uses `document.load_buffer(xml.data(), xml.size())` and the callers of the zip (1399-1412) and gpx (1418-1431) branches own a mutable `std::optional<std::string> score` that is not reused; load_buffer copies the whole buffer. True fact, inflated impact: (a) the import runs on a worker thread (score_document.cpp:147-154, 'Runs on a worker thread'), once per open, not per edit or per frame; (b) docs/performance.md:67 measures 125 ms for the 39 MB GPIF and says that what remains is the XML parse: the memcpy + reservation of 39 MB are on the order of 10 ms (<10%), plus 39 MB of transient peak; (c) in .gpx the score does not exceed ~4 MB (a single sector entry, see GP2), so it only matters in .gp (zip). The proposal is feasible: load_buffer_inplace modifies the buffer and requires the string to outlive the document, which holds if importScore receives the string by value; the parsing has to be separated from importGpif(string_view), which must keep load_buffer. Note: the current benchmark ('import the large song from GPIF', bench_io.cpp:318) enters through importGpif, so it will not reflect the improvement; a benchmark through importGuitarPro with zip is needed. No effect on determinism or threads.

#### io-gp/GP7 · intOf()/trimmed() create a temporary std::string for every integer and every text property

- **Where:** `src/io/src/guitar_pro.cpp:87`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
const std::string clean = trimmed(text);
```

**Why it matters.** intOf is used for every id of the lists (via intsOf, line 134), for every id when building ById (line 584) and per note in String, Fret, Midi/Number and Octave (lines 826, 849, 850, 409); `trimmed` also per beat in Dynamic (line 748) and twice per note in pitchOf (Step and Accidental, lines 406 and 413). That is on the order of 1.5-2 million temporary std::string on the reference song. They fit in SSO, so there is no malloc, but there is construction + copy + destruction per token: on the order of 10-20 ms of CPU spread out, and it is trivial code to eliminate.

**Proposal.** Add `std::string_view trimmedView(std::string_view)` (same find_first_not_of/find_last_not_of, returning a substr of the view) and use it in intOf, pitchOf, dynamicFor(...), noteValueFor(...), the clef of readBar (line 640), HType (935) and Tremolo (806). Leave `trimmed` with std::string only where the result is stored (names, song info).

**Verification note.** Checked: trimmed (77-84) returns std::string and intOf (86-94) uses it at line 87; the references are exact: 134 (every id of intsOf), 584 (every id of ById), 826/849/850 (Number, String, Fret per note), 406/409/413 (pitchOf), 748 (Dynamic per beat), 640, 806, 935. The count of ~1.6 M temporaries on the reference adds up (6 per note + ids). They fit in SSO, so there is no malloc; estimated cost 8-16 ms of CPU, almost all in the parallel phase (plus ~2-3 ms serial in ById), that is a few ms of wall clock: 'low' is correct. The proposal is feasible and safe: clefFor, noteValueFor and dynamicFor already receive std::string_view (155, 364, 384), and pitchOf only uses size(), [0], iteration and == on the text, all valid with a view (pugixml's text lives as long as the document lives). from_chars behaves the same on the view. Keep std::string where the value is stored (names, info, 1162-1171).

#### io-gp/GP8 · Duration cache in an unordered_map per staff when the Rhythm ids are dense

- **Where:** `src/io/src/guitar_pro.cpp:972`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
std::unordered_map<int, Duration> durations_;
```

**Why it matters.** durationOf does a hash lookup per beat (80k on the reference) and every StaffReader (one per staff, 20+) parses the same <Rhythm> again and populates its own map with heap nodes. The Rhythm ids are numbered from 0 (ById already stores them in `table_`), so an indexed array is enough. Small gain (a few ms), but it eliminates hashing and allocations in the parallel loop.

**Proposal.** Replace with `std::vector<std::optional<Duration>> durations_` sized with `rhythms_.size()` and indexed by id (with the map kept as a fallback only if ById fell back to sparse mode). The warning behaviour is preserved, which today are emitted the first time each staff resolves the rhythm.

**Verification note.** Checked: `std::unordered_map<int, Duration> durations_` (972) is a member of StaffReader, one per staff (1244), and durationOf (700-730) does a find per beat (745). True, but the gain is minimal even for 'low': ~80k hash lookups of an int are ~1-2 ms of CPU spread across threads (tenths of a ms of wall clock), and the number of distinct rhythms is small (1 on the reference), so the map's allocations are negligible. Last priority. The proposal is feasible if: the bounds are checked (the id comes from attribute("ref").as_int(-1), may be negative or nonexistent and must keep throwing the same FormatError of line 706); a map is kept when ById falls back to sparse mode (size() then returns the number of entries, not the maximum id); and the cache remains per StaffReader, because sharing it across threads would break thread safety and the deterministic order of the warnings.

#### io-gp/GP2 · In .gpx the decompressed container stays alive during the whole parse and the file is extracted without reserve

- **Where:** `src/io/src/guitar_pro.cpp:1417`
- **Axis:** memory · **Impact:** low (initially proposed: high) · **Effort:** S · **Verdict:** partial

```cpp
std::optional<detail::GpxContainer> container;  ...  return importScore(*score, parts, options);
```

**Why it matters.** `container` (with `unpacked_`, up to kMaxUnpacked = 64 MB) is not destroyed until importScore returns. At that moment the following are alive at once: the file bytes, `unpacked_` (N), `score` (N, copied sector by sector), pugixml's internal copy (N, see GP1), the DOM and the Song: three copies of the same XML. In addition findFile builds `content` with 4 KiB appends without reserving (gpx_container.cpp:156 `std::string content;`), which adds ~log2(N/4K) reallocations with copying and a transient peak of up to ~2N.

**Proposal.** After reading `score` and `parts`, do `container.reset()` before calling importScore. In findFile, `content.reserve(std::min(size, fs.size()))` when `wanted`. Combined with GP1 the text peak goes from ~3N to ~1N.

**Verification note.** Checked: `container` (1417) stays alive until importScore returns (1431) and findFile declares `std::string content;` without reserve (gpx_container.cpp:156) and appends sector by sector (170). But the impact is greatly inflated: findFile only reads the sectors listed in ONE entry (slots from offset+0x94 to offset+0x1000 = 987 sectors), so an extracted file does not exceed ~4.04 MB (confirmed by the comment in bench_io.cpp:248 'at most about 4 MB'). Therefore 'three copies of N with N up to 64 MB' is not possible for the XML: N <= 4 MB, and what container.reset() frees is `unpacked_` (typically a few MB; 64 MB only in a hostile file). In the uncompressed BCFS case the container only keeps a span and there is nothing to free. The lack of reserve is at most ~15-20 reallocations over <=4 MB (<1-2 ms; the 28-31 ms measurement of the 4.4 MB .gpx is dominated by the bit-by-bit unpacking). The two proposals are correct and safe (`parts` and `score` are copies of our own; the reserve is bounded by min(size, fs.size()), better still by 987*4096), but the gain is marginal.

#### io-gp/GP4 · intsOf() returns a new std::vector<int> per measure, voice and beat

- **Where:** `src/io/src/guitar_pro.cpp:125`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
std::vector<int> values;  //  for (const int noteId : intsOf(textOf(fields.notes)))
```

**Why it matters.** It is called on the hot path: line 649 (Voices per measure), 664 (Beats per voice) and 749 (Notes per beat). On the reference song that is ~10k + ~10k + 80k calls, each with at least one heap reservation and, since there is no reserve, 2-3 reallocations for lists of 2-4 ids: on the order of 150-250k malloc/free, done from all the track threads at once (allocator contention, which limits parallel scaling).

**Proposal.** Replace with a heap-free iterator/visitor: `template<class F> void forEachInt(std::string_view, F&&)` that does from_chars directly on the view, and use it in readBar/readBeat. Minimal alternative: a member `std::vector<int> scratch_` of StaffReader reused with clear() (one for voices, another for beats/notes, since the loops nest).

**Verification note.** Checked: intsOf (124-140) returns a new std::vector<int> without reserve and is called at 649 (per staff measure), 664 (per voice) and 749 (per beat), inside readStaff, which runs in parallel (1274). The count is correct for the reference: 10k + 10k + 80k calls; with MSVC's growth (1,2,3,4,6,9) it comes to ~260k malloc/free. The impact is inflated: it is ~10-20 ms of CPU spread across the track threads, that is ~5-8% with one thread (211 ms) and 1-3 ms of wall clock out of the 125 ms measured with several threads, where the serial XML parse dominates. The 'allocator contention' is not demonstrated, and the model's own construction (vector<Note> per beat, vector<Beat> per voice) makes a similar number of reservations that will remain. The main proposal (forEachInt with from_chars on the view) is correct and preserves behaviour (invalid tokens are skipped the same way). The alternative of member buffers is poorly framed: there are THREE nested levels alive at once (voices at 649 -> beats at 664 -> notes at 749 inside readBeat), so sharing one buffer for 'beats/notes' would corrupt the iteration of beats; three would be needed. No thread problem (one StaffReader per thread).

#### io-gp/GP5 · Serial phase before the threads: the five ById tables are built one after another, with an intermediate vector without reserve

- **Where:** `src/io/src/guitar_pro.cpp:978`
- **Axis:** concurrency · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
: root_(root), bars_(root.child("Bars"), "Bar"), voices_(root.child("Voices"), "Voice"), beats_(root.child("Beats"), "Beat"), notes_(root.child("Notes"), "Note"),
```

**Why it matters.** The GpifReader constructor walks on the calling thread the ~10k Bar, ~10k Voice, 80k Beat and 160k Note (comparing the name of each child and looking for the id attribute), and each ById first accumulates into `std::vector<std::pair<int, pugi::xml_node>> found;` (line 580, 16 B per element, without reserve: ~2.5 MB and ~18 reallocations for Notes alone) to copy afterwards into `table_`. All that is serial time before readStaves, which is the only thing parallelised; by Amdahl it is what bounds the gain with 20 tracks.

**Proposal.** (1) Build the table in a single pass: while the ids arrive as 0,1,2... do a direct push_back into `table_` and fall back to the current path (found + map) only on seeing the first out-of-sequence id. (2) Build the five collections in parallel with `detail::forEachIndex(0, 5, ...)` when the score exceeds the threshold: the pugixml DOM is read-only here and the collections are independent.

**Verification note.** Checked: the GpifReader constructor (977-981) builds the five ById serially and ById (579-603) accumulates into `found` without reserve before copying to `table_`. But the Amdahl conclusion is incorrect: the serial part that bounds the gain is the pugixml parse (~110 ms of the 125 ms; performance.md:67 'most of what is left is the XML parse'), not these tables. Walking 270k siblings with name comparison + id attribute + intOf is on the order of 5-15 ms; the `found` vector (2.5 MB, ~30 reallocations with factor 1.5) costs ~1 ms. Realistic gain: (1) single pass, 1-2 ms; (2) parallelising the five collections, at most ~40% of that phase because Notes is 60% of the elements and cannot be split (linked list), minus the cost of creating 5 std::thread (forEachIndex does not use a pool, parallel.hpp:41-56): 3-5 ms. Feasibility: concurrent reading of the DOM is safe and the result does not depend on order, but (a) the ById are built in the initialiser list and `threads` only arrives in read(threads), so the option has to be restructured and propagated to respect {.threads = 1}; (b) the fast path must preserve 'the first element with an id wins' and the dense/sparse criterion of line 592.

#### io-gp/GP6 · The file is read byte by byte with istreambuf_iterator and without a prior size

- **Where:** `src/io/src/guitar_pro.cpp:1449`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
```

**Why it matters.** With input iterators the vector cannot reserve: it grows geometrically (reallocations with copying and a transient peak of ~1.5-2x the file) and every byte goes through the streambuf (a few ns per byte; on MSVC it is especially slow). For a .gp/.gpx of several MB that is tens of ms that can be avoided on open. In addition `bytes` stays alive during the whole import even though in .gp/.gpx it is only needed until score.gpif is extracted.

**Proposal.** Read in bulk: `const auto size = std::filesystem::file_size(path, ec); std::vector<std::uint8_t> bytes(size); in.read(reinterpret_cast<char*>(bytes.data()), size);` (checking gcount). Optional: release `bytes` after extracting the GPIF in the zip/BCFZ branches, moving the reading of entries into a block of its own.

**Verification note.** Checked: importGuitarProFile (1449-1450) builds the vector with istreambuf_iterator, without a prior size, and is the real open path (open.cpp:50 <- score_document.cpp:154 and CLI). The fact is true but the impact is inflated: what is read this way is the file on disk (compressed), not the GPIF: a real .gp/.gpx usually ranges from tens of KB to ~1 MB; at a few ns per byte in release that is ~1-5 ms, once per open and on the worker thread, against the 28-125 ms of the rest of the import. 'Tens of ms' would only hold for files of several MB or debug builds. That `bytes` stays alive is irrelevant (it is the size of the compressed file, and the binary gp3-5 branch needs it until the end). The proposal (file_size + bulk read checking gcount) is correct and trivial; a fallback path should be kept if file_size fails and the size should not be trusted for files that change while being read.

#### io-gp/GP9 · gp3-5 binary reader: discarded texts that are decoded to UTF-8 and measures per track without reserve

- **Where:** `src/io/src/guitar_pro_binary.cpp:670`
- **Axis:** speed · **Impact:** low · **Effort:** S · **Verdict:** partial

```cpp
(void)in_.intByteString();
```

**Why it matters.** The texts that are going to be thrown away (beat text at line 670, markers at 534, header/footer templates at 312-314, RSE effects at 576-577, names at 752) go through utf8FromWindows1252: it reserves a std::string, fills it character by character and destroys it. In files with text per beat that is a heap reservation per beat for nothing. In addition `track.measures.push_back(std::move(measure));` (line 655) grows without reserve even though `measureCount` is known at line 333: ~10 reallocations per track, moving every time all the Measure (4 voices each), with 20 tracks x 500 measures.

**Proposal.** Add to ByteReader `skipIntByteString()` / `skipByteString(size)` that validate the size and advance `at_` without building the string, and use them in all the `(void)in_....String()`. Pass `measureCount` to readTrack (or reserve right after the loop at line 336) with `track.measures.reserve(measureCount)`.

**Verification note.** Checked: guitar_pro_binary.cpp:670 `(void)in_.intByteString();`, and also 312-314, 534, 576-577, 740, 748/752 (these two are chord names with byteString(22)); intByteString (166-179) builds the std::string with utf8FromWindows1252 (83-98, which does reserve). `track.measures.push_back` (655) grows without reserve and measureCount is known at 333. It exists, but the cost is negligible: the discarded texts are 11 per file, 2 per track and only the beats with flag 0x04 (text), which are rare; empty ones do not reserve and short ones fit in SSO. The ~10-15 reallocations per track move 96 B Measure (4 vectors): 20 tracks x 500 measures are tens of microseconds. There is no gp3-5 benchmark to back it. Warnings about the proposal: (1) the skips must preserve the validations exactly (size < 1 throws with the same message and byte; need(field)) so that the errors for damaged files do not change; (2) `track.measures.reserve(measureCount)` as it stands is dangerous: measureCount is dictated by the file up to kMaxMeasures = 100,000 and there are up to 128 tracks, so a hostile file of ~115 KB would force reserving 128 x 100,000 x 96 B = ~1.2 GB up front, when today memory grows only with the data actually read. If done, bound the reservation by the bytes remaining in the file.

**Observations on this area (what is already well solved).**

Scope: read-only review of src/io/src/guitar_pro.cpp, guitar_pro_binary.cpp/.hpp, gpx_container.cpp/.hpp, gp_directions.hpp and general_midi.hpp. Nothing was built or measured: the magnitudes are estimates from the reference song (80k beats, 160k notes) and the code read; the real size of that song's GPIF was not checked, which is why the memory figures are given as a function of N (size of the XML).
Well solved, leave alone:
- ById (guitar_pro.cpp:577) already uses an indexed table with a map only as a fallback; BeatFields/NoteFields read children and properties in one pass.
- readStaves (1227) distributes by staff with an atomic index (dynamic distribution, there is no static imbalance between threads), each thread writes only into its Staff, its Warnings and its Damage, and the result is deterministic. threadsFor limits to min(cores, jobs, 64) and there is a kParallelNotes threshold so as not to launch threads on small scores. The threads are created per call (core/parallel.hpp:41), but in a one-off import that cost (on the order of 1 ms) is irrelevant.
- gpx_container.cpp: 64-bit BitReader, `out.reserve(std::min(expected, packed.size() * 16))` and copying of references with copy_n; the container is unpacked only once for score.gpif and PartConfiguration. A 64 MB limit against decompression bombs.
- gp3-5 binary reader: ByteReader works over a span without copying the file, with cheap bounds checking; headers_ already has reserve; the TrackData measures are MOVED into the Song in build() (line 1162), not duplicated. The format is a stream without offsets (measure by measure interleaving tracks), so it cannot be parallelised per track; not worth trying.
- general_midi.hpp and gp_directions.hpp are constexpr tables of string_view with a linear search over 19 elements only per MasterBar: no relevant cost.
Outside the scope of this section; worth checking separately: Warnings::add (src/io/src/warnings.hpp:24) does a linear search comparing two strings per warning; with per-note warnings (e.g. "tapping dropped") it runs up to 160k times, although the list is short. song.cpp was not read, so nothing can be stated about whether the non-const accessors (song.measureHeader(m), song.track(t).staff(s)) called from several threads in readStaves are purely read-only; it is worth confirming when core is reviewed. The import in the app seems to be launched with QtConcurrent::run (src/app/src/score_document.cpp:451), not on the UI thread, but this was not verified in detail.

### IO: MIDI and native format

#### io-midi-nativo/N1 · Saving .elara runs entirely on the UI thread (JSON + deflate level 6 + disk write)

- **Where:** `src/app/src/score_document.cpp:843`
- **Axis:** concurrency · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** confirmed

```cpp
io::saveNative(document_->song(), std::filesystem::path(local.toStdU16String()));
```

**Why it matters.** ScoreDocument::save() is synchronous and QML calls it: songToJson (42.8 ms) + deflate + rename run on the UI thread. On the reference song that is 70.4 ms (4-5 frames lost) and that with a JSON that compresses to 41 KB because it repeats a pattern; docs/performance.md warns that real songs compress much worse, and deflate level 6 (ZipEntry::level = 6 by default, archive.hpp:57) over 7.9 MB of real JSON can go to 100-250 ms, plus the disk I/O (antivirus, slow disk, network drive) without a bound. open() and relayout() already go through QtConcurrent; save is the only heavy I/O operation that remains blocking.

**Proposal.** Launch the save with QtConcurrent::run capturing song_ (it is already an immutable std::shared_ptr<const core::Song> that edited() keeps up to date at line 877, so no extra copy is needed) and finish with .then(this, ...) like open(): markSaved() referring to the value of edits_ captured at launch, saved/saveFailed signals instead of the bool return, and a single save in progress at a time (the temporary is always called path + ".saving", archive.cpp:94, so two simultaneous saves to the same file would overwrite each other). Optional: lower song.json to deflate level 1-3, which in miniz is several times faster.

**Verification note.** Checked: ScoreDocument::save() (score_document.cpp:834-857) is Q_INVOKABLE, synchronous, and QML calls it directly (Main.qml:107 and 174, `if (score.save(...)) runPendingAction()`). saveNative -> encodeNative -> songToJson + writeZip (level 6 by default, archive.hpp:57) + writeFileAtomically, all on the UI thread. In src/app the only other use of io:: with I/O is openFile, which already goes through QtConcurrent (line 451), so it is true that it is the only heavy I/O that blocks. Qualifications: (1) the 70.4 ms are from the encodeNative benchmark (bench_io.cpp:89), without disk: the real save is that plus the write. (2) Inflated impact: it is an explicit and infrequent action, and edited() (lines 877-880) already does on the UI thread a copy of the song (10.7 ms) and a Sequence (28 ms) on EVERY edit; a save is equivalent to about two edits. What does justify 'medium' is the unbounded I/O (network, antivirus) and that real data compresses worse. The 100-250 ms of deflate is an unmeasured extrapolation. Proposal feasible with conditions: song_ is synchronised with document_->song() at every return to the event loop (lines 502/509, 668, 877; typeDigit always ends in edited() or redo()), and capturing the shared_ptr<const Song> is safe between threads. But Document::markSaved() (document.hpp:70) only marks the CURRENT depth: if an edit is made during the save, it must be marked only if edits_ has not changed, or an API must be added in core that respects the discarding of the redo branch and the undo limit (document.cpp:25-33). In addition the QML pendingAction/onClosing flow (which today depends on the bool return) must be rewritten and the window must not be closed until the save in progress finishes. Lowering the deflate level is not worth it: docs/performance.md itself (.mxl row) measures that level 1 gains little and multiplies the size by 10.

#### io-midi-nativo/N2 · The native writer builds a full mutable yyjson DOM and then duplicates the text

- **Where:** `src/io/src/native_format.cpp:108`
- **Axis:** memory · **Impact:** medium (initially proposed: high) · **Effort:** M · **Verdict:** partial

```cpp
std::string json(text, length);
```

**Why it matters.** Writer creates a yyjson_mut_val (24 bytes: a 16 value + next pointer) for every object, key and value: about 7 per note (object + pitch/string/fret) and 5 per beat, that is ~1.5 M nodes = 35-40 MB of DOM for a model that takes ~13 MB (160k notes x 48 B + 80k beats x 64 B), plus the pool of strings copied with strncpy. With the DOM still alive, yyjson_mut_write (line 104) reserves the 7.9 MB of text and line 108 copies it into a std::string: approximate peak model + 37 MB + 7.9 MB + 7.9 MB. In addition walking the model, building the DOM and walking it again to serialise is double work: that is the 42.8 ms measured.

**Proposal.** Emit the JSON directly into a std::string (the keys are fixed literals and the values are integers, booleans and short strings; string escaping is only needed for info/names): one buffer per track with an estimated reserve (~50 B per note), written in parallel with forEachIndex and joined in order, as the MusicXML exporter does (ADR-0008). It eliminates the whole DOM (peak ~8 MB instead of ~53 MB) and leaves the writing on the order of 5-10 ms. If a cheap intermediate step is wanted: free the doc before copying and return yyjson's buffer without copying it (unique_ptr<char, free> + string_view towards writeZip).

**Verification note.** The code exists as is: yyjson_mut_write at line 104 and the copy into std::string at 108 with the document still alive (it is freed in the Writer destructor). The node count is correct: every object add_* creates key + value, so a note with pitch/string/fret is 7 yyjson_mut_val and a beat 5; on the reference song about 1.5 M nodes x 24 B = ~36 MB, plus 7.9 MB + 7.9 MB of text. What lowers it: (1) as a MEMORY finding the impact is low: a transient peak of ~50 MB for ~43 ms in a desktop application and only for a song of 160k notes. (2) The real cost is time (42.8 ms of the 70.4 of the save) and it only matters while the save stays on the UI thread; with N1 done it becomes low. (3) It is a known and deferred decision: docs/performance.md line 51 says 'fast enough for open/save. If it ever shows, write tracks in parallel or emit JSON directly'. (4) The target of 5-10 ms is an estimate; it is coherent with the MusicXML exporter (124 MB in 155 ms on one thread), but it is not measured. Gaps in the proposal: not all the values are integers, booleans and strings: addNumber writes reals (non-integer bpm, volume, pan, harmonic fret), and the direct emitter would have to produce valid JSON numbers that round-trip and reject non-finite values; yyjson also validates UTF-8 when writing and the direct emitter must keep that validation or it will write files that the reader rejects. The intermediate step (not copying yyjson's buffer) is correct and cheap, but songToJson is a public API that returns std::string (the CLI uses it), so it has to be an internal overload.

#### io-midi-nativo/N3 · MIDI import: Fraction arithmetic with repeated gcd in the hot loops of grids and rhythms

- **Where:** `src/io/src/midi_import.cpp:750`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** confirmed

```cpp
static_cast<std::size_t>((from / cell).numerator() / (from / cell).denominator());
```

**Why it matters.** Every operator/ of Fraction is 4 std::gcd (normalised reciprocal + 2 cross-reduction + normalise) and here the same division is done twice per iteration of spellRhythm. The same pattern repeats: longestPlain (line 691) calls isMultiple (one division) for each of the ~24 candidate durations in rests; the loop at line 776 multiplies cell * Fraction(next) for each probed cell and again at 779; Grids::snap (549-555) builds measure.cell() three times and does 2 products + 1 sum per note (320k calls); Grids::locate (line 591) evaluates measures_.back().end().toDouble(), which is a sum of fractions, on each of the ~640k calls, and its upper_bound converts start.toDouble() (a floating-point division) on every probe; MeasureInfo::end()/cell()/beat()/cellCount() are recomputed on every use inside writeVoice (lines 988-1035). It is exactly what the document points out as the remainder of the 287 ms on one thread (40.5 ms only thanks to 12 cores; on a 4-core laptop it would be ~80-100 ms).

**Proposal.** 1) Store in MeasureInfo, computed once in layMeasures, end, cell, beat and cellCount as fields. 2) In Grids, precompute in the constructor startTicks/cellTicks per measure and the song end in double, and advance with a cursor instead of upper_bound (the notes arrive sorted by start). 3) In voiceSubdivisions/spellRhythm/writeVoice work in integers: units of cell/10080 (10080 = lcm of all the candidate subdivisions 1..32, 24, 14, 10, 9), so that isMultiple is a %, floorTo an integer division and the comparisons a <; convert to Fraction/Duration only when emitting. Reasonable target: 287 ms on one thread to under 80 ms.

**Verification note.** Verified line by line: operator/ (fraction.hpp:64-69) is 4 std::gcd (reciprocal constructor + 2 cross-reduction + normalise); line 750 writes `from / cell` twice; longestPlain calls isMultiple (one division) per candidate only when aligned, that is on rests, and there are 24 plain durations; lines 776 and 779 multiply cell * Fraction(next); snap (549-555) builds measure.cell() three times; locate (591) evaluates measures_.back().end(), which is a sum of fractions, on each of the ~640k calls (addPoint + snap, two per note each) and its upper_bound does start.toDouble() per probe; MeasureInfo::end()/cell()/beat()/cellCount() (279-293) are not stored and are recomputed in writeVoice. It matches docs/performance.md line 63 ('One thread still takes 287 ms, mostly fraction arithmetic in the grids'). Impact lowered: the import runs on a worker thread (openAndLayOut via QtConcurrent, score_document.cpp:451), once per open of a MIDI file, and is already spread per track: 40.5 ms on the reference machine and about 80-100 ms on 4 cores for a song of 160k notes. It blocks neither the UI nor the audio. Corrections to the proposal: point 2 (cursor instead of upper_bound) only holds for the starts; the notes are sorted by start (line 248) but addPoint(note.end) and snap(raw.end) are not monotonic, so the ends still need a search. Point 3 is arithmetically correct (lcm of 1..10, 12, 14, 16, 24, 32 = 10080, and beat is always a multiple of cell), but it is a rewrite of the core of the rhythm writing and the result must remain bit-for-bit identical; the target of under 80 ms is not measured. Points 1 and the precomputation part of 2 are safe and cheap: start there.

#### io-midi-nativo/N5 · When writing, every pitch and duration is formatted with std::format/std::to_string into a temporary std::string and copied again

- **Where:** `src/io/src/native_format.cpp:310`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** confirmed

```cpp
addText(object, "pitch", value.pitch.toString());
```

**Why it matters.** Pitch::toString() ends in std::format("{}{}{}", ...) (src/core/src/pitch.cpp:84) and Duration::toString() in std::to_string plus std::format for tuplets (src/core/src/duration.cpp:45-51); line 294 does the same per beat. That is 240k calls (160k notes + 80k beats), each of which builds a std::string that addText copies again into the pool with yyjson_mut_obj_add_strncpy (line 119). std::format on MSVC costs on the order of 100-200 ns per call, that is a noticeable part (estimated 10-20 ms) of the 42.8 ms of writing, for a tiny domain of values: 7 notes x 5 accidentals x ~11 octaves and 9 note values x 3 dots.

**Proposal.** A static table of already formatted strings indexed by (step, alter, octave) for the pitches and by (value, dots) for durations without an irregular group, built once; pass them by pointer with yyjson_mut_obj_add_strn (no copy: static storage outlives the document) or write them as they are in N2's direct emitter. std::format remains only for durations with a tuplet, or is replaced by std::to_chars on a stack buffer.

**Verification note.** Verified: line 310 (pitch) and 294 (duration) go through toString(); Pitch::toString ends in std::format("{}{}{}") (pitch.cpp:84) and Duration::toString in std::to_string, with std::format only for irregular groups (duration.cpp:46-52). That is 160k + 80k calls, each creating a temporary std::string (no heap allocation, thanks to SSO) that addText copies again with yyjson_mut_obj_add_strncpy (line 119). Impact lowered: the 10-20 ms are an unprofiled estimate (plausible for MSVC's std::format, but not measured), only affect saving and stop being noticeable if N1 takes the save off the UI thread. Even so it is the cheapest fix on the save path. The proposal is correct: yyjson_mut_obj_add_strn does not copy and static storage outlives the document; the immutable table is safe between threads and the output is identical. A detail to correct: the valid octave range is not 11 but 12 (from -2 to 9: B#-2 is MIDI 0 and passes the validation in pitch.hpp:40), so the table must cover it or fall back to the general path. A simpler alternative: have Pitch::toString write with std::to_chars instead of std::format, which benefits all callers without a table.

#### io-midi-nativo/N9 · Opening .elara keeps two copies of the 7.9 MB JSON in addition to the DOM and the model

- **Where:** `src/io/src/native_format.cpp:948`
- **Axis:** memory · **Impact:** low · **Effort:** S · **Verdict:** confirmed

```cpp
yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &error);
```

**Why it matters.** With flags = 0 (without YYJSON_READ_INSITU) yyjson reserves and internally copies the whole input so as to be able to terminate the strings in place, while the std::string returned by zip.entry("song.json") (line 981) stays alive until the end of decodeNative. That string, moreover, is created zero-filled before decompressing on top of it ('std::string content(size, '\0')', archive.cpp:40: a useless 7.9 MB memset). Peak when opening the reference song: 7.9 MB (text) + 7.9 MB (yyjson copy) + ~25 MB of DOM (~1.5 M values x 16 B) + the model that keeps growing up to ~13 MB; the copy and the memset are also 1-2 ms of the 51 ms.

**Proposal.** Decompress into a buffer of our own with YYJSON_PADDING_SIZE bytes of padding at the end (std::string::resize_and_overwrite or a unique_ptr<char[]> to avoid the zero fill) and parse with YYJSON_READ_INSITU through an internal overload of songFromJson that receives the mutable buffer; the public API with string_view can keep copying. It saves 7.9 MB of peak and one memcpy pass plus one memset pass.

**Verification note.** Verified: yyjson_read_opts with flags = 0 (lines 947-948) reserves and copies the whole input (length + padding) and that copy lives with the document; the std::string from zip.entry("song.json") (line 981) stays alive until the end of decodeNative; and ZipReader::entry creates the buffer with `std::string content(size, '\0')` (archive.cpp:40), a zero fill that inflate overwrites afterwards. The 'low' impact is already the right one: 7.9 MB of a ~55 MB peak and on the order of 1 ms of 51 (the 1-2 ms is a somewhat generous estimate: memset and memcpy of 7.9 MB are around 0.5 ms each), all on the open worker thread. The proposal is feasible with care: YYJSON_READ_INSITU requires a mutable buffer with YYJSON_PADDING_SIZE (4) zero bytes after the data, and a std::string only guarantees a terminator, so size + 4 must be reserved and size passed as the length; yyjson modifies the buffer (unescapes strings in place) and the string_views that the Reader returns point into it, so the buffer must outlive the document. It must be done with an internal overload: songFromJson(std::string_view) is a public API and cannot mutate its input, and ZipReader::entry is shared by other readers (.mxl, .gpx), whose behaviour must not change.

#### io-midi-nativo/N4 · The native reader processes the tracks serially over an immutable yyjson document

- **Where:** `src/io/src/native_format.cpp:451`
- **Axis:** concurrency · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
forEach(tracks, [&](std::size_t, yyjson_val* track) { readTrack(track, song); });
```

**Why it matters.** Reading song.json is 35.3 ms of the 51.3 ms of opening an .elara and, according to the measurement itself, the yyjson parse is the small part: the bulk is building the model track by track. It is the only importer that still runs on one thread (MusicXML, GPIF and MIDI already distribute per track). The immutable yyjson_doc is safe for concurrent reading and each track writes only into its Track. In addition every object is queried key by key with yyjson_obj_get (linear search with strlen of the key): 4 lookups per note (pitch, string, fret, effects), 3 per beat and 14 in readNoteEffects (lines 890-913) even if the object has 1-2 keys: on the order of 1 M lookups on the reference song.

**Proposal.** Split readTrack in two phases: (1) serially and in order, create each Track with its staves (cheap) and keep each staff's yyjson pointer; (2) readMeasures of each track with forEachIndex/threadsFor, one Reader per thread (path_ is its own state) initialised with the prefix tracks[i], and an error slot per track to always return the one from the first track in order (forEachIndex rethrows the first per thread, not per track). Expected ~35 ms to 6-10 ms. Along the way, walk each note/beat/effects object only once with yyjson_obj_iter and dispatch by key, as was already done with the children of <note> in MusicXML.

**Verification note.** Line 451 exists and readTrack/readMeasures/readBeat/readNote run serially; the immutable yyjson_doc is safe for concurrent reading and each track would only write into its Track if the tracks are created beforehand (song.addTrack reallocates the vector). The lookups by key are true: 4 yyjson_obj_get per note and 3 per beat, about 880k on the reference song; the 14 of readNoteEffects are only paid on notes with effects (none in the benchmark). What does not hold: (1) 'it is the only importer that still runs on one thread' is false: guitar_pro_binary.cpp (.gp3/.gp4/.gp5) does not use forEachIndex either. (2) The impact is inflated: the open runs on a worker thread (score_document.cpp:451), it is 35 ms of an open that also sequences (28 ms) and lays out (up to 21 ms), and docs/performance.md itself (line 51) considers it sufficient. (3) The gain from 35 to 6-10 ms is optimistic: the yyjson parse and its 7.9 MB copy remain serial, plus the thread startup; 10-15 ms is reasonable. (4) The lookups are cheap on objects of 1-3 keys (yyjson compares lengths before memcmp); walking with yyjson_obj_iter contributes little except in the effects objects. On correctness: splitting into two phases changes which error is reported when there are several (a header error in track 1 would come out before a measure error in track 0); to keep the current order the phase 1 must stop at the first error and the one with the lowest track index must be returned between the two phases. ReadError does not derive from std::exception, but forEachIndex catches with catch(...), so it propagates fine.

#### io-midi-nativo/N6 · gridSpelling queries a thread_local std::map with a tuple key on every candidate, cell and iteration

- **Where:** `src/io/src/midi_import.cpp:453`
- **Axis:** speed · **Impact:** low (initially proposed: medium) · **Effort:** S · **Verdict:** partial

```cpp
thread_local std::map<std::tuple<std::int64_t, std::int64_t, int>, std::optional<GridSpelling>>
```

**Why it matters.** The memoisation avoids recomputing, but the lookup is still on the hot path: usable() (line 605) calls it for every candidate of exactFor/bestFor (up to 15 per cell with dots, ~40k cells per track), voiceSubdivisions (line 723) per candidate and cell, and spellRhythm on every iteration and on every following cell it probes (lines 746, 778). Each call pays the TLS access with an initialisation guard, building the tuple and descending the tree comparing three-field tuples; with on the order of 1-2 M calls on the reference song that is tens of ms of the 287 ms on one thread. In addition usable() builds Fraction(1, 128) (a gcd) on every call (line 607) and, as forEachIndex creates new threads per import, the cache is rebuilt and destroyed on each thread.

**Proposal.** A song has 1-3 distinct cell lengths: build in layMeasures a table per cell length, std::array<std::optional<GridSpelling>, 33> indexed by subdivisions (with the boolean 'usable by notation', written >= 1/128, already resolved), and store in MeasureInfo a pointer to its table. The lookup becomes an array index, with no TLS or map, and the immutable table is shared between the threads.

**Verification note.** The code exists (thread_local std::map with a tuple key, lines 452-461) and is queried from usable() (605), voiceSubdivisions (723) and spellRhythm (746, 778). But the volume is inflated: '~40k cells per track' is false, it is 2000 per track (500 measures x 4 cells) and 40k in the whole song. On the reference song exactFor stops at the second candidate, voiceSubdivisions too, and spellRhythm makes one lookup per chord (the loop at line 776 does not get to probe with eighth notes): about 240k lookups in total, not 1-2 M, over a map of 2 entries. At 10-20 ns per lookup that is 3-5 ms of the 287 ms on one thread (less than 1 ms of wall clock in parallel), not 'tens of ms'. Fraction(1, 128) with constant arguments is constexpr and its gcd is trivial; rebuilding the map per thread is a few entries. Moreover this memoisation IS the mitigation already applied (docs/performance.md line 62: 357 to 287 ms). In played (non-quantised) music up to 15 + 6 candidates per cell are tried and the cost rises, but it remains proportional to the cells. The proposal (a table per cell length indexed by subdivisions, immutable and shared) is correct, deterministic and safe between threads, but the gain is marginal; it only makes sense as part of N3.

#### io-midi-nativo/N7 · MIDI import: temporary vectors per measure, per chord and per cell (hundreds of thousands of concurrent allocations)

- **Where:** `src/io/src/midi_import.cpp:705`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
std::vector<std::vector<Fraction>> inside(count);
```

**Why it matters.** For every (track, measure, voice) writeVoice creates std::vector<Fraction> points (line 991) and voiceSubdivisions creates a vector of vectors (1 + one allocation per cell with points) plus std::vector<int> result (line 716); spellRhythm returns a new std::vector<Duration> per chord and per rest (line 742); every Chord carries its own std::vector of notes (line 811, one allocation per chord in assignVoices); beat.notes.push_back (line 1027) grows without reserve even though chord.notes.size() is known, and beats is not reserved either (line 997); Grids::points_ is a vector<vector<Point>> with one allocation per occupied cell (line 669). On the reference song (10k measure-voices, 80k chords) that is on the order of 400-500k very short-lived allocations, made at once from up to 24 threads against the same heap: it is a candidate to explain why the scaling is 7x (287 to 40.5 ms) with 12 cores and 20 jobs.

**Proposal.** A working struct per writeNotes reused between measures (points, inside flattened into a single vector with offsets per cell, subdivisions, a buffer of durations with clear() instead of constructing); have spellRhythm append to a caller vector instead of returning one; beat.notes.reserve(chord.notes.size()) and beats.reserve; the chord's notes as a range (first, count) over a single vector per voice instead of a vector per Chord; points_ in CSR format (count, prefix, fill).

**Verification note.** The allocations all exist: points (991), inside as a vector of vectors (705), result (716), the vector that spellRhythm returns per chord and per rest (742), Chord::notes (811), beat.notes without reserve (1027), beats without reserve (997) and points_ as vector<vector<Point>> (669). The count even falls short: counting the growth of the vectors it comes to about 60 allocations per measure-voice, plus ~160k of points_, on the order of 800k on the reference song. What lowers it: (1) That this explains the 7x scaling is an unmeasured conjecture; Amdahl's law explains it just as well with what remains serial: readSmf (320k MidiEvent), gather with its stable_sorts, layMeasures, the thread startup and the destruction of the result in the benchmark. (2) The import runs on a worker thread and takes 40.5 ms; it affects neither the UI nor the audio. On the proposal: the reserves (beat.notes.reserve(chord.notes.size()), beats.reserve) and a working struct reused by writeNotes are safe (each track writes in its thread and in its Track) and do not change the result. On the other hand 'the chord's notes as a range (first, count) over a vector per voice' does not fit assignVoices: a note can join a chord that is not the last one (reverse loop at lines 827-833, by equal start and end) and the full-voices branch adds notes to an already placed chord (860-862), so the ranges are not contiguous without first reordering by (start, end, key) while preserving 'the highest first'.

#### io-midi-nativo/N8 · MIDI export goes through a 48-byte MidiEvent with a std::string for every note-on/off, without reserve, and computes the ticks with fractions

- **Where:** `src/io/src/midi_export.cpp:277`
- **Axis:** memory · **Impact:** low (initially proposed: medium) · **Effort:** M · **Verdict:** partial

```cpp
track.events.push_back(channel(edge.tick, detail::kNoteOn, channelNumber, edge.key,
```

**Why it matters.** MidiEvent (smf.hpp:45-50) carries a std::string payload even though channel messages do not use it: 48 bytes on MSVC x64 per event that ends up as 3-4 bytes in the file. With 320k events that is ~15 MB of intermediates to produce 1.2 MB, in addition to the edges (24 B x 320k) and the SoundingNote. track.events grows without reserve (edges.size() is known), and encodeEvents starts with an empty 'std::string out;' (smf.cpp:40) and fills it byte by byte with push_back. Timing::ticks (line 45) builds Fraction(4 * division) and multiplies: 4 gcds per call, two calls per note plus the sum note->start + note->length at line 262 (about 1.7 M gcds), when in the exact case the denominator already divides 4*division. reportDropped (line 213) calls warnings.add, which walks the entries comparing strings, once per note with a fret position: in a tablature editor, almost all of them. The importer has the mirror image: readSmf materialises the same 48 B MidiEvent and 'const auto file' (midi_import.cpp:1274) stays alive during the whole import even though only gather() uses it.

**Proposal.** In addNotes, encode the edges directly into bytes (varlen delta + running status + 2 data) in a std::string with reserve(edges.size() * 4) and leave MidiEvent only for the few header meta-events; exact ticks as num * (4 * division / den) when (4 * division) % den == 0, with the Fraction path only for the non-exact case; in reportDropped accumulate per type a counter and the first measure in a local array and dump it into Warnings once per track. On import, release the MidiFile when gather() finishes (or have readTrack deliver the channel messages in a compact 16-byte struct and the meta ones separately).

**Verification note.** The facts are true: MidiEvent (smf.hpp:45-50) carries a std::string and takes 48 B on MSVC x64; out.events grows without reserve; encodeEvents starts with an empty `std::string out;` (smf.cpp:40) and writes byte by byte; Timing::ticks (line 45) is 4 gcds per call, two calls per note plus the sum at line 262 (2 gcds): 10 gcds per note, ~1.6 M; reportDropped calls warnings.add for every note with a position (line 213) and add walks the entries comparing strings (warnings.hpp:26-31). The impact is inflated: (1) exportMidi is only called from the CLI (src/cli/src/file_commands.cpp:242); the application does not export MIDI, so it does not touch the UI. (2) A MidiEvent with an empty payload does not allocate on the heap (SSO): the ~15 MB is transient memory spread across tracks, not 320k allocations. (3) The whole export is 45 ms (74 on one thread) and the per-track part already runs in parallel; what remains, according to the document itself, is mostly the sounding notes. (4) On a track with only 'fret positions dropped' warnings.add hits on the first entry: two string comparisons per note, a few ms in total. The proposal is correct: in exact mode 4*division is a multiple of the denominator by construction (chooseTiming, lines 56-62), so num * (4*division/den) gives the same tick as the Fraction path, and ticks(start) + ticks(length) is equivalent to ticks(start + length); the path with rounding must be kept when exact is false. Encoding the edges directly must give the same bytes (running status, which is reset after the meta events) to respect ADR-0009. On the import mirror, `file` (line 1274) cannot simply be released after gather(): importParsed uses file.division afterwards (line 1257); the division must be copied first. It saves 15 MB for 40 ms: irrelevant.

**Observations on this area (what is already well solved).**

Files read in full: src/io/src/native_format.cpp, midi_import.cpp, midi_export.cpp, smf.cpp, smf.hpp, midi_shared.hpp, archive.cpp, warnings.hpp, src/core/include/elara/core/parallel.hpp and fraction.hpp, and the parts of src/app/src/score_document.cpp that open, save and react to an edit.
Well solved, leave alone:
- Writer: the keys are literals stored by pointer (not copied); default values and trailing empty measures/voices are omitted, which already reduces the JSON.
- Reader: JsonPath stores segments and only builds text on failure; readMeasures and readBeat do reserve with yyjson_arr_size; limits and errors are localised at no cost on the happy path.
- ScoreDocument::open already runs in QtConcurrent with a generation counter that discards stale results; song_ is a shared immutable snapshot, which makes it cheap to move the save to a worker (N1).
- writeSmf(format, division, bodies) reserves the exact size; addNotes reserves edges; plainDurations() is computed once; chooseTiming avoids building fractions and cuts early with 'exact && needed % den != 0'.
- MIDI import and export already distribute per track with warnings merged in order (deterministic result) and a kParallelNotes threshold so as not to launch threads on small songs.
- No visible data races in the area: each job writes only into its Track / its body / its Warnings, and the gridSpelling cache is thread_local.
Observations outside the strict scope (not counted as a finding): core::forEachIndex creates and destroys std::thread on every call (core/parallel.hpp:44) instead of using a pool; for importing/exporting a file it is negligible (1-2 ms), but it is worth evaluating together with core/notation/audio, where it is called per edit. ScoreDocument::edited() copies the whole song and the sequence on the UI thread on every edit (score_document.cpp:877-880); it belongs to the app area.
