# 0031. Editing the song in the app

- Status: Accepted
- Date: 2026-11-23

## Context

Until now the app only showed songs. The core has had undoable edits since
milestone 1 (`core::Document` and its commands, ADR-0005), and `elara-cli
edit` applies them in scripts. The app needs to edit a song too: start one,
name it, set its tempo, signatures and length, add and remove tracks for any
instrument (guitars, basses, drums, keys), undo, redo and save. Pages are laid
out on worker threads from an immutable song (ADR-0015), so the song being
edited and the song being laid out cannot be the same object.

## Decision

**The document owns the song; the pages get copies.** `ScoreDocument` holds a
`core::Document`. Every change goes through a command, so every change can be
undone. After each edit, undo or redo it copies the song into a new
`shared_ptr<const Song>` and lays it out again on a worker thread, as it does
after a change of track or view. The copy is what workers and painters share;
the document is touched only on the main thread.

**Whole relayouts for now.** An edit lays out the whole score again. A
500-measure track takes 3.5 ms on A4 pages and the 20 tracks of the large
benchmark song 21 ms (docs/performance.md), off the main thread, so this is
fast enough for song-level edits. Incremental relayout (milestone 3) will make
note entry cheaper.

**Signatures change until the next change.** Changing the time or key
signature of a measure changes the run of measures that had the same
signature, up to the next change, as editors do. `core::changeTimeSignature()`
and `core::changeKeySignature()` build that as one undo step, shared by the
app and `elara-cli edit` (`time` and `key`). The song panel changes them from
the first measure; changes inside the song wait for the selection of
measures.

**Saving writes the native format.** Save writes the `.elara` file the song
came from; a song opened from another format, or a new one, asks for a
`.elara` file. Opening another song, starting a new one or closing the window
with unsaved changes asks whether to save them.

**What the smoke test checks.** `--smoke-test` edits the song (a new one when
there is no file): it adds a track, changes the time signature, undoes, redoes
and checks that the song ends as it began, laying out and painting each step.

## Consequences

- Each edit costs a copy of the song on the main thread: about 11 ms for the
  benchmark song of 20 tracks and 500 measures (docs/performance.md), more
  than laying out one of its tracks (3.5 ms). That is acceptable for
  song-level edits; note entry on large songs will want measures shared
  between copies (copy-on-write) so that a copy costs only what changed.
- Playback hears an edit through a new sequence (ADR-0017), which is built
  on a worker thread beside the edit's layout; a sequence that plays as before
  is kept, so playback carries on (ADR-0039). Building it on the main thread
  took longer than the copy (about 30 ms for that song). The pages do not wait
  for it: on that song the layout of a track takes a few milliseconds and the
  sequence about 70, so they show as soon as they are laid out. Until the
  sequence arrives, playback plays the previous one with the new mix, and the
  audio side always loads a sequence with the song it was built from. Layouts
  and sequences each count their own generation, so only the latest of each
  is used, and a song opened or made meanwhile drops the sequence of an edit
  to the song before.
- An edit's new sequence is loaded where playback is, playing or paused as it
  was: the player takes it at its current frame. Only another song (a file
  opened, a new song: `ScoreDocument::songReplaced`) sends playback back to
  the start. Before, every edit that changed the music paused playback and
  rewound it, so typing a note lost the place in the song.
- Names of edits come from the core in English and are translated in the app
  by a table; an edit missing from it is shown in English.
- Track names given by the app are the instrument's name in the interface
  language. Songs made with `elara-cli new` keep the English names.
