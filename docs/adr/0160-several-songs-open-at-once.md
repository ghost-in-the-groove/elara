# 0160. Several songs open at once

- Status: Accepted
- Date: 2026-11-27

## Context

The window held one song: `Main.qml` made one `ScoreDocument`, and opening a
file or starting a new song replaced it, after asking to save. Measures copied
stayed in that song's editing session (ADR-0127), so nothing could go from one
song to another except by opening the second one over the first, which the
session happened to survive. Musicians work across songs: a riff taken from
one transcription into another, two arrangements compared, parts put together
from several files. The owner asked for several songs open at once, with
things copied between them, as fast as the machine allows.

## Decision

- **One window, the songs in tabs.** The window keeps one transport, one set
  of panels and one output device; a row of tabs under the tool row shows once
  two or more songs are open, so one song looks as before. Separate windows
  were turned down: each would need its own transport on the one output
  device, its own panels and its own remembered state.
- **Each song is a `ScoreDocument` of its own**, with its history, recovery
  copy (ADR-0106), layout and sequencing lanes (ADR-0110), caret and
  selection. A `DocumentSet` in the app holds them and says which one is
  shown; `Main.qml`'s `score` is that one, so every part of the window
  follows it. Opening a file that is already open shows its tab.
- **Songs open in parallel.** Files opened together (several chosen in the
  dialog, dropped, or named on the command line) each start loading at once.
  The loads run on the shared worker pool, and the layout inside each spreads
  over `core::WorkerPool` (ADR-0091), so opening four songs takes about as long
  as the largest. A song not shown does no work until it is shown or edited;
  its pages stay laid out, so going back to it costs no layout.
- **The transport plays the song shown.** Showing another song stops playback
  and puts the transport on that song, as opening a song always did.
- **One clipboard for every song open.** `core::Clipboard` holds what was
  copied last; the editing sessions of all the songs share it, so measures
  copied in one song are pasted in any other, by the same rules as within a
  song (ADR-0027). A copy holds its measures as `SharedValue<Measure>`
  (ADR-0051): copying costs a reference count per measure, and pasting where
  the frets stay as they are shares them again. Pasting onto other strings, or
  onto a staff without strings, makes measures of their own. Copy on write
  keeps the two songs apart from then on.
- **The CLI** opens songs beside the one it edits (`edit --from <file>`,
  repeatable, opened in parallel on the worker pool) and copies from them
  (`copy <m> <n> ... from <song>`), so a paste from another song is checked
  without the app.
- **Closing**: a song with unsaved changes asks before its tab closes, and
  quitting asks for each such song in turn.

## Consequences

- Each open song keeps its pages and its undo history in memory. Dropping the
  pages of songs not shown, and laying them out again when shown, stays
  possible if large scores need it.
- Two copies of the app still cannot copy between each other: the clipboard
  lives in one process. A second start could hand its files to the running
  app instead; that is a task of its own.
- The smoke test drives several songs: opening two at once, switching, a paste
  from one into the other, closing with unsaved changes.
