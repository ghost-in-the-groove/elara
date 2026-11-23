# 0027. Copying and pasting measures

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 5 asks for copy and paste. Tablature is full of repeated measures:
a riff played four times, or a verse that comes back. The editor needs a way to
copy measures from some tracks and put them somewhere else, in the same song,
as one undoable edit (ADR-0005). The target can differ from where the
measures came from. It can be another track with another tuning, a track with
fewer staves, a measure past the end of the song, or a place where new measures
should be made.

## Decision

**A clip is a value.** `copyMeasures(song, first, count, track, trackCount)`
returns a `MeasureClip`: the copied measure headers and, for each track in a
run of consecutive tracks, its kind and, for each staff, its tuning, capo and
measures. A clip does not refer back to the song, so editing the song never
changes a clip, and a clip can be pasted any number of times.

**Pasting is one command.** `makePaste(song, clip, at, track, mode)` builds a
`CommandGroup` named "Paste". Copied track *i* goes to track `track + i`, and
staff *s* to staff *s*. Staves the target lacks are dropped.

- `PasteMode::Replace` writes over the measures from `at`. The target's time
  and key signatures stay, because the measures around them already rely on
  those. When the clip runs past the end, measures are appended.
- `PasteMode::Insert` makes new measures before `at`, in every track, with the
  copied headers. Tracks that receive nothing get empty measures.

**Frets follow the target staff.** Notes are pasted as they were when the
target has the same tuning and capo. On another tuning or capo, the notes find
strings again by the same rules as transposition (ADR-0026). They keep their
string when the fret is on the neck, otherwise take the nearest free one, and
ties and hammer-ons follow their note. On a staff without a tuning, notes lose
their fret positions. A note no string of the target can play refuses the
paste.

**Drums stay drums.** Drum keys are not pitches. A clip from a drum track
pastes only into drum tracks, and a clip from a pitched track only into
pitched tracks.

## Consequences

- The fret placement written for transposition now lives in one private place
  (`fret_placement.cpp`), shared by both edits. A natural harmonic keeps its
  node in both: the same fret on some string, or an artificial harmonic.
- Pasting over measures with another time signature can leave voices that do
  not fill their measure. Validation reports them, as it does for any edit.
- `elara-cli edit` keeps the last `copy` in the session for later `paste`
  steps. The app's clipboard will hold a `MeasureClip` the same way. Copying a
  selection of beats inside a measure needs the selection model and is not
  part of this decision.
