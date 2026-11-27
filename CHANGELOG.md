# Changelog

What changed in each version of Elara, the newest first. Versions are
major.minor.patch; while the major version is 0 they are previews, and a file
saved by one may not open in an older one. `python scripts/elara.py release
prepare X.Y.Z` turns Unreleased into a version's section
([docs/releasing.md](docs/releasing.md)).

## Unreleased

The first preview: a tablature and score editor that opens, plays, edits and
writes songs.

### Files

- Opens Guitar Pro 3, 4, 5, 6 (`.gpx`) and 7-8 (`.gp`) files, MusicXML
  (`.musicxml`, `.xml`, `.mxl`), MIDI and its own `.elara` files, and says
  what an import approximated or left out.
- Saves `.elara` files and exports MusicXML, MIDI and PDF, saying what a format
  leaves out.
- Keeps a copy of unsaved changes and offers it back after a crash.

### Notation

- Standard notation, tablature or both, on pages or as one continuous view,
  for fretted instruments in any tuning and capo, drums on a percussion staff,
  and single-staff instruments, up to four voices per staff.
- Time and key signatures, clefs, tempo changes and gradual tempos, triplet
  feel, repeats, alternate endings, signs and jumps, sections, measure repeats.
- Effects of strings (bends, slides, hammer-ons and pull-offs, palm mutes,
  harmonics, vibrato, whammy bar, tapping, slap and pop), articulations,
  dynamics and hairpins, slurs, ties, grace notes, fermatas, trills, mordents
  and turns, octave lines, the sustain pedal, breath marks, fingerings, chord
  symbols with diagrams, texts and lyrics.

### Playing

- Plays through MuseScore General or any SoundFont, and guitars and basses
  through SFZ instruments, with bends, slides, legato, palm mutes, harmonics
  and strums.
- A mixer with volume, pan, mute and solo per track.
- A metronome with count-in, an A-B loop, speeds from 25 to 200 %, and a tuner
  that listens to the microphone.

### Editing

- Keyboard entry of frets, note names, drum hits, durations, tuplets and
  voices; every effect from the note effects palette or its key.
- Tracks, measures, signatures, tempos, tunings, repeats and sections edited
  from the song panel, the measure popup or where they are drawn; selection,
  copy, paste and transposition of measures; undo and redo.

### The app

- One window with menus naming every command and its keys, a song panel, a
  docked tuner, a home with the recent scores, light and dark themes with a
  dark page, English and Spanish.
- The window opens as it was closed: its place and size, its panels, its zoom,
  its page and its view.
- `elara-cli` does from a terminal what the app does, with JSON output.
- Installers for Windows (MSI) and Linux (AppImage), a disk image for macOS,
  and portable archives, with MuseScore General and a guitar SFZ instrument.
