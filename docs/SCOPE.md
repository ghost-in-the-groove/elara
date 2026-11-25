# Scope of Elara 1.0

Agreed with the project owner on 2026-11-22. Changes to this file are product
decisions: discuss them first, then record why in the commit message.

## Goal

A free, modern, open-source alternative for writing, reading and practising
guitar tablature and scores, on Windows, macOS and Linux.

## In 1.0

**Notation**
- Tablature and standard staff in sync, for guitar, bass and other fretted
  instruments, any tuning and capo.
- General notation for single-staff instruments and piano (grand staff), up to
  4 voices per staff.
- Measures, key and time signatures, tuplets, ties, dynamics, repeats, voltas,
  Coda/Segno.
- Page view and continuous view; SMuFL symbols (Bravura font).

**Files**
- Import Guitar Pro `.gp3`, `.gp4`, `.gp5`, `.gpx`, `.gp` (7-8).
- Import and export MusicXML and MIDI.
- Export the score as PDF (planned for 1.1, written early: ADR-0074).
- Versioned native project format.

**Sound**
- FluidSynth with MuseScore General SF3 for every instrument.
- Guitar articulations on SFZ instruments, through Elara's own sampler
  (ADR-0066): bends and vibrato (each note bent on its own), slides, legato
  (hammer-on/pull-off), palm mute, dead notes, harmonics. Requires a free SFZ
  guitar library with a compatible licence (milestone 4 task); fallback:
  articulations simulated on SF2.
- Basic mixer: volume, pan, mute, solo per track.

**Practice tools**
- Metronome with count-in, A-B loop, speed 25-200 %, tuner (microphone input).

**Platforms**
- Windows, macOS and Linux with CI and installers for all three.

## Not in 1.0

- Audio recording, clips, DAW timeline.
- VST3 plugins, piano roll.
- Advanced orchestral notation: parts/cues, cue notes, cross-staff beaming, lyrics.

## Constraints

- Licence AGPL-3.0-or-later; every dependency and asset must be compatible.
- No code, assets or reverse-engineered internals from commercial products.
  File formats are implemented from public documentation.
- macOS signing/notarisation needs an Apple Developer account (paid); without it
  users must approve the app manually.
