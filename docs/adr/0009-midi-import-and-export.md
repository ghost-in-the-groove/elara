# 0009. MIDI import and export

- Status: Accepted
- Date: 2026-11-23

## Context

Standard MIDI Files are how sequencers, DAWs and most notation programs
exchange music, and how players share songs. MIDI holds a performance, not a
score: notes are keys pressed and released at ticks, with no spelling, voices,
rests, tuplets, ties, staves or tablature. Writing MIDI from the model is
therefore easy and lossy. Reading MIDI means *inferring* notation, and two kinds
of files must both work: files from notation programs, which are exact (every
note on a rhythmic grid), and played music, which is not.

## Decision

**Sounding notes in the core.** `core::soundingNotes(song)` lists every note as
it sounds: start and length in whole notes from the start of the song, ties
joined (same key, and same string on a fretted staff, in the previous beat of
the voice), velocity from the dynamic. The MIDI writer uses it, tests compare
songs by it, and playback (milestone 4) will build on it. Articulations are not
applied there.

**Export.** A format 1 file, with the measures in play order: repeats,
alternate endings and jumps written as played (ADR-0083):

- Ticks per quarter note: 960, or the least multiple of 960 that makes every
  time in the song a whole number of ticks, while it fits in 15 bits; otherwise
  960 with a warning.
- A conductor track with the title (track name), copyright, every meter (MIDI
  clocks per click: the dotted beat in compound meters), every key (`sf`, `mi`)
  and every tempo in quarter notes per minute (other beat units are converted,
  with a warning; a gradual change goes in steps, ADR-0082), and its end at the end of the song so empty measures survive.
- One track per track: track name, instrument name, program change, notes.
  Drums play on channel 10; other tracks take channels 1-9 and 11-16 in order
  and share them beyond fifteen (warning). A key struck again while it sounds on
  the channel is released and struck again; it ends with the last of its notes.
- Fret positions, capo, guitar techniques, articulations, song info other than
  the title and copyright, and instrument kinds the importer would not give
  back are listed in the warnings.

**Import.**

1. *Events.* Format 0 and 1 (2 read as 1); running status, system exclusive and
   unknown chunks; damaged headers and events fail with the byte offset, damaged
   lengths and missing end-of-track events are tolerated with a warning. Each
   channel of each track becomes a track, in order of first use. A note-off ends
   the oldest sounding note of its key; notes never released end with their
   track. Lyrics, sustain pedal, pitch bends and later program changes are
   reported.
2. *Measures.* Meters, keys and tempos come from their events (any track). A
   meter change inside a measure cuts it with a meter of its actual length when
   one exists, else moves to the next measure. Keys apply at measure starts.
   Tempos are read in quarter notes per minute, rounded to hundredths, so what
   the writer wrote comes back. The song lasts until the last note or
   end-of-track event, except that a final measure holding only releases or a
   track end less than a sixteenth note past the barline (played music) is not
   kept.
3. *Instruments.* The program decides the kind (`kindForProgram`, shared with the
   writer): channel 10 drums, programs 1-8 keyboards (two staves split at middle
   C), 25-40 fretted (a guitar or bass preset with its tuning; no fret positions),
   others pitched (the matching preset, or a single staff whose clef suits the
   notes). Names come from the track and instrument name events, else from the
   General MIDI program name.
4. *Grids.* Each staff picks a rhythm grid per *cell* (the beat, at most a
   quarter note; the dotted beat in compound meters). A cell's grid is the
   simplest subdivision (1, 2, 4, 3, 8, 6, 16, 12, 5, 10, 7, 14, 32, 24, 9; or
   1, 3, 6, 2, 12, 4, 24... in compound meters) that holds every note start and
   end in it within one tick. When at least 90 % of the cells with notes have
   one, the staff is *quantized* and keeps those grids. Otherwise it is *played*,
   and each cell takes the grid that best fits its note starts (releases are too
   loose to count), among sixteenths and triplets, with finer grids only when
   they fit much better. Starts and ends snap to their cell's grid; a note that
   snaps to nothing lasts one grid step. Drum hits last until the next hit of
   their voice (hands, or feet: bass drums and pedal hi-hat), within their beat.
5. *Voices.* Notes that start and end together form chords. Each chord goes to
   the first of four voices that is free when it starts, higher chords first; if
   none is free, the voice that frees first is cut short (warning).
6. *Rhythms.* Each voice writes each measure on its own grids (the simplest ones
   that hold its boundaries), so a triplet in one voice does not turn the other
   voices into tuplets. Tuplet cells are written by themselves, as notes of the
   next longer power-of-two value scaled by the tuplet. A note starting off the
   beat is cut at the next beat; otherwise each step takes the longest value that
   fits (up to two dots), rests also aligned to their own length. Cut notes are
   tied; pitches are spelled in the key in force (`KeySignature::spell`);
   velocities become the nearest dynamic (played velocities: a dynamic per
   measure with accents and ghost notes, ADR-0045).

**Threads.** Both directions work track by track once the shared part is done
(the sounding notes and the division when writing; the events, measures and
empty tracks when reading). Each track only reads the shared part and writes
its own chunk or track, so large files use every core; warnings are kept per
track and merged in track order, and `MidiOptions::threads` exists so tests can
check that the result is the same on one thread and on several.

## Consequences

- A song written the way the importer writes comes back equal, and any song
  plays the same after a round trip whenever its times fit the file's division;
  tests check both (hand-written songs, played music with jitter, random songs,
  and that a second round trip changes nothing).
- Notation inferred from MIDI is a starting point: spelling follows the key,
  voices follow the greedy rule, and fretted tracks have no tablature until fret
  positions are assigned automatically (a later task).
- Playback will reuse `soundingNotes`, adding articulations and pitch bends.
- `elara-cli import <file.mid>` and `elara-cli export <song.elara> <out.mid>`.
