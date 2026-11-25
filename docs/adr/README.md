# Architecture Decision Records

One file per decision that constrains future work: `NNNN-short-title.md`,
numbered in order, never renumbered. Superseded ADRs stay, with a link to the
one that replaces them.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-technology-stack.md) | Technology stack | Accepted |
| [0002](0002-juce-as-headless-audio-library.md) | JUCE as a headless audio library | Accepted |
| [0003](0003-headless-verification.md) | Headless verification | Accepted |
| [0004](0004-song-model.md) | Song model | Accepted |
| [0005](0005-undo-with-reversible-commands.md) | Undo with reversible commands | Accepted |
| [0006](0006-native-file-format.md) | Native file format | Accepted |
| [0007](0007-musicxml-import.md) | MusicXML import | Accepted |
| [0008](0008-musicxml-export.md) | MusicXML export | Accepted |
| [0009](0009-midi-import-and-export.md) | MIDI import and export | Accepted |
| [0010](0010-guitar-pro-import.md) | Guitar Pro import | Accepted |
| [0011](0011-notation-engine.md) | Notation engine | Accepted |
| [0012](0012-staff-layout.md) | Staff layout and the display list | Accepted |
| [0013](0013-tablature-and-systems.md) | Tablature layout and staves aligned in systems | Accepted |
| [0014](0014-line-breaking-and-pages.md) | Line breaking, justification and pages | Accepted |
| [0015](0015-score-view.md) | The score view: painting display lists in the app | Accepted |
| [0016](0016-repeats.md) | Repeats and alternate endings | Accepted |
| [0017](0017-sequencer.md) | The sequencer | Accepted |
| [0018](0018-built-in-synthesizer.md) | The built-in synthesizer and offline rendering | Accepted |
| [0019](0019-real-time-playback.md) | Real-time playback | Accepted |
| [0020](0020-track-mix.md) | The track mix | Accepted |
| [0021](0021-keyboard-entry.md) | Keyboard entry at a caret | Accepted |
| [0022](0022-articulations-in-playback.md) | Articulations in playback | Accepted |
| [0023](0023-pitch-articulations.md) | Bends, slides and legato in playback | Accepted |
| [0024](0024-practice-playback.md) | Practice playback: speed, metronome, count-in, loop | Accepted |
| [0025](0025-strums-and-tremolo-picking.md) | Strums and tremolo picking in playback | Accepted |
| [0026](0026-transposition.md) | Transposition | Accepted |
| [0027](0027-copy-and-paste.md) | Copying and pasting measures | Accepted |
| [0028](0028-pitch-detection.md) | Pitch detection for the tuner | Accepted |
| [0029](0029-microphone-input.md) | Microphone input for the tuner | Accepted |
| [0030](0030-translations.md) | Translations of the user interface | Accepted |
| [0031](0031-editing-in-the-app.md) | Editing the song in the app | Accepted |
| [0032](0032-keyboard-entry-in-the-app.md) | Keyboard entry in the app | Accepted |
| [0033](0033-editing-measures-at-the-caret.md) | Editing measures at the caret | Accepted |
| [0034](0034-note-names-at-the-caret.md) | Note names at the caret | Accepted |
| [0035](0035-drum-entry.md) | Drum entry | Accepted |
| [0036](0036-selecting-measures-in-the-app.md) | Selecting measures in the app | Accepted |
| [0037](0037-note-effects-at-the-caret.md) | Note effects at the caret | Accepted |
| [0038](0038-note-effects-in-the-app.md) | Note effects in the app | Accepted |
| [0039](0039-mixer-in-the-song-panel.md) | The mixer in the song panel | Accepted |
| [0040](0040-tuplet-numbers-and-brackets.md) | Tuplet numbers and brackets | Accepted |
| [0041](0041-rhythm-under-tablature.md) | Rhythm under tablature | Accepted |
| [0042](0042-tempo-marks.md) | Tempo marks | Accepted |
| [0043](0043-soundfont-playback.md) | SoundFont playback | Accepted |
| [0044](0044-dynamics-marks.md) | Dynamics marks | Accepted |
| [0045](0045-played-midi-velocities.md) | Played MIDI velocities | Accepted |
| [0046](0046-retuning.md) | Retuning a fretted staff | Accepted |
| [0047](0047-dynamics-at-the-caret.md) | Dynamics at the caret | Accepted |
| [0048](0048-packages.md) | Packages | Accepted |
| [0049](0049-signs-and-jumps.md) | Signs and jumps | Accepted |
| [0050](0050-tuplets-at-the-caret.md) | Tuplets at the caret | Accepted |
| [0051](0051-shared-measures.md) | Measures shared between copies of a song | Accepted |
| [0052](0052-incremental-relayout.md) | Incremental relayout | Accepted |
| [0053](0053-worker-pool.md) | A worker pool for parallel jobs | Accepted |
| [0054](0054-worker-results-from-the-event-loop.md) | Worker results arrive from the event loop | Accepted |
| [0055](0055-bend-forms.md) | Bends of any form at the caret | Accepted |
| [0056](0056-beat-effects-at-the-caret.md) | Beat effects at the caret | Accepted |
| [0057](0057-continuous-view.md) | Continuous view | Accepted |
| [0058](0058-kinds-of-note-effects.md) | Kinds of note effects at the caret | Accepted |
| [0059](0059-song-credits.md) | Song credits | Accepted |
| [0060](0060-whammy-bar-and-bass-techniques.md) | Whammy bar and bass techniques at the caret | Accepted |
| [0061](0061-whammy-bar-and-bass-marks.md) | Whammy bar and bass techniques in the score | Accepted |
| [0062](0062-sections.md) | Sections of a song | Accepted |
| [0063](0063-section-marks.md) | Section marks in the score | Accepted |
| [0064](0064-app-icon.md) | The app icon | Accepted |
| [0065](0065-installers.md) | Installers | Accepted |
| [0066](0066-sfz-sampler.md) | SFZ instruments through Elara's own sampler | Accepted |
| [0067](0067-flac-decoder.md) | A FLAC decoder of Elara's own | Accepted |
| [0068](0068-sfz-techniques.md) | Guitar techniques on a library's keyswitches and controllers | Accepted |
| [0069](0069-included-soundfont.md) | MuseScore General comes with the packages | Accepted |
| [0070](0070-sfz-legato.md) | Hammer-ons and pull-offs play a library's legato samples | Accepted |
| [0071](0071-recent-scores.md) | Recent scores | Accepted |
| [0072](0072-included-guitar-sfz.md) | A clean electric guitar SFZ comes with the packages | Accepted |
| [0073](0073-grand-staff.md) | A brace and the barlines join a grand staff | Accepted |
| [0074](0074-pdf-export.md) | PDF export written by Elara | Accepted |
| [0075](0075-guitar-pro-keyboards-grand-staff.md) | Guitar Pro 3-5 keyboards on a grand staff | Accepted |
| [0076](0076-arpeggio-lines.md) | Strums of keyboards are arpeggio lines | Accepted |
| [0077](0077-techniques-of-strings.md) | Techniques of strings go on fretted staves only | Accepted |
| [0078](0078-hairpins.md) | Hairpins | Accepted |
| [0079](0079-grace-notes.md) | Grace notes | Accepted |
| [0080](0080-chord-symbols.md) | Chord symbols | Accepted |
| [0081](0081-beat-text.md) | Beat text | Accepted |
| [0082](0082-gradual-tempo-changes.md) | Gradual tempo changes | Accepted |
| [0083](0083-midi-export-play-order.md) | MIDI export writes the play order | Accepted |
| [0084](0084-lyrics.md) | Lyrics | Accepted |
| [0085](0085-fermatas.md) | Fermatas | Accepted |
| [0086](0086-slurs.md) | Slurs | Accepted |
| [0087](0087-clef-changes.md) | Clef changes | Accepted |
| [0088](0088-tenuto.md) | Tenuto and portato | Accepted |
| [0089](0089-octave-lines.md) | Octave lines | Accepted |
| [0090](0090-trills.md) | Trills | Accepted |
| [0091](0091-mordents-and-turns.md) | Mordents and turns | Accepted |
| [0092](0092-pedal-marks.md) | Pedal marks | Accepted |

## Template

```markdown
# NNNN. Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
What forces are at play.

## Decision
What we do.

## Consequences
What becomes easier, harder, or must be watched.
```
