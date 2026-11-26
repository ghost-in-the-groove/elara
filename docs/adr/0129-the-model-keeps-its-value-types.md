# 0129. The model keeps its value types

- Status: Accepted
- Date: 2026-11-26

## Context

A song's music is held as values: a `Note` is 48 bytes, a `Beat` 144, a
`Measure` 104 on Windows x64, and the large benchmark song (20 tracks, 500
measures, 80 000 beats, 160 000 notes) holds 20.6 MB of tracks, 129 bytes a
note (`docs/performance.md`). Much of a note and of a beat is room for what
most of them lack: the vector of a bend curve in every note and of a whammy
curve in every beat, 24 bytes each though empty, and three shared references
in every beat for a chord, a text and lyrics.

A study looked at seven ways to make the model smaller, from the least to the
most invasive: curves kept outside their notes and beats; a beat's optional
data behind one reference; tighter enumerations and flags; a pool of memory
for each measure; identical effects shared by value; 32-bit indices into flat
buffers; and a structure of arrays.

Each can make the model smaller. What it buys is small. Brought to 80 bytes a
note, the benchmark song would hold 7.8 MB less: about 3 % of a process that
measured 247 MB of working set showing a song of 8 tracks (`docs/performance.md`).
A song whose model is 1 or 2 MB would gain less than a megabyte. What else is
held for the same song is larger by far: an SFZ library's samples of 100 MB
and more, the geometry of the pages (2.9 MB for one track, 34.7 MB for 20
staves), an undo history of up to 256 MiB (ADR-0121).

What it costs is not small. `Note` and `Beat` are read and written as they
are by the importers and exporters, the commands, the edits at a caret, the
notation and the sequencer. They copy and compare as values, which is what
makes an edit's undo step, a round trip's test and a snapshot read on a worker
simple to reason about; measures are shared between copies of a song until
one changes (ADR-0051). Every one of the seven ways can keep that, with
owners per measure, immutable data or adapters: that work is the cost.

## Decision

- The model is stored as it is. `Note`, `Beat` and how a staff holds its
  measures do not change, and no size of a note is a goal in itself.
- The seven ways are a list of what could be done, not work that is planned.
- A change that makes a type smaller without changing what it means or how it
  is used (members in another order, say) may still go in, when the size
  measured says it is smaller.
- The question is opened again by a need that is measured, not by the room
  there is to save: memory pressure that can be reproduced, a history whose
  growth gets in the way, a profile where allocating the model or missing the
  cache on it dominates, or real scores for which the model is a large part
  of what the process holds. Then the least invasive way that answers that
  need is tried first, alone, and measured on real scores as well as on the
  benchmark song, with the time of an edit, an import, a layout and a
  sequencing, the sharing of measures, undo and redo, and round trips; the
  audio callback gains no lock, allocation or destruction by it.
- `approximateBytes()` counts what it had left out: the points of the bend of
  each note, grace notes included, and of the whammy bar of each beat. The
  curve's own 24 bytes were counted with its note or beat; its points, on the
  heap, were not.

## Consequences

- The model costs what it costs: about 129 bytes a note where a compact one
  might cost 80. Nobody has to learn a second way of reaching a note.
- The undo history's byte budget (ADR-0121) measures a measure with bends as
  what it holds. A history of such measures reaches the budget a little
  sooner than it did, which is the budget being kept.
- A future review that finds the same room in `Note` and `Beat` finds here
  why it was left, and what would change the answer.
