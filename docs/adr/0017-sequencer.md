# 0017. The sequencer

- Status: Accepted
- Date: 2026-11-23

## Context

Playback (milestone 4) needs the song as it is played: measures in the order
repeats give (ADR-0016), each note's start and end in seconds under the tempo
map, and the note-on and note-off messages a synthesizer takes. The model
already gives the notes as written (`core::soundingNotes()`, tied notes
joined, velocity from the dynamic) and the play order (`Song::playOrder()`).
The audio thread must not build any of this: it may not allocate or lock
(ADR-0002), and a long song has hundreds of thousands of notes.

## Decision

**Where.** `elara_audio` holds the sequencer, as `docs/architecture.md`
planned: it is plain C++ over `elara_core`, testable without a sound card.
`audio::Sequence` is built from a `core::Song` in one call and never changes
afterwards.

**What it holds.**

- `measures()`: the written measures in play order, each with its start and
  end in seconds.
- `notes()`: every note as played, with the index of the played measure of
  its attack (`pass`), so the second time through a repeat is told apart from
  the first.
- `events()`: note-on and note-off events per track, ordered by time with
  releases before attacks. A key struck again while it still sounds on its
  track is released and struck again, and released only when the last of its
  notes ends, as the MIDI export does (ADR-0009).
- `eventAt(seconds)`: where playback starting at a time begins.

**Time.** Positions stay exact whole notes along the play order until they
become seconds, so notes that meet in the score meet in the event list.

- Every measure plays at the tempo written in it: the tempo in force at its
  written start, then its own changes. Going back to the start of a repeat
  returns to the tempo written there, not the one reached at its end.
- A note lasts its length in whole notes over the measures played after its
  attack, so a note tied across an end repeat sounds over the start of the
  section it goes back to.
- Seconds are doubles; the audio thread turns them into sample frames at its
  own rate.

Articulations (bends, slides, palm mute, let ring...) are not applied yet: they
are the roadmap's next playback item and will add events, not change these.

**Threads.** A sequence is built off the audio thread (the main thread or a
worker) whenever the song changes, and handed to the audio thread whole; the
audio thread only reads it, walking `events()` from `eventAt()`. How the
handover works (an atomic pointer swap) is decided with the synthesizer
integration.

**Headless.** `elara-cli sequence FILE [--notes] [--events]` prints the play
order with times, and the notes and events as played, in text or `--json`
(ADR-0003).

## Consequences

- Tests check playback order and timing without listening: unit tests for the
  sequence and CLI cases on corpus files with repeats and endings.
- The MIDI export still writes the measures as written (ADR-0016); writing them
  as played needs the unrolling in `elara_io`, which may not use
  `elara_audio`, so the play order would move to the core first.
- Building a sequence for the 500-measure benchmark song takes milliseconds
  (`docs/performance.md`), so it is rebuilt whole after an edit rather than
  patched.
