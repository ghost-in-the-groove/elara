# 0111. Edits say what they change

- Status: Accepted
- Date: 2026-11-26

## Context

After every edit, undo or redo, `ScoreDocument` laid the score out again and
built a new sequence (ADR-0031). `sequenceFor()` then compared the new
sequence with the one playing and kept the old one when they were equal
(ADR-0039), so playback went on. By then the work had already been done. That
cost a whole sequence, and for a mix change a whole layout too, on edits that
cannot change either. A track's volume, pan, mute and solo are not drawn, and
the player applies them itself. Song information, chord symbols, beat texts,
lyrics and section names are drawn but do not sound (`measure.hpp`). Such
edits should run no sequence at all.

## Decision

- `core::Changes` says what an edit changes: `notation`, what the pages show,
  and `sound`, what playback plays. `Command::changes()` returns it once
  `apply()` ran, and it holds for `revert()` too. Both are true unless the
  command knows better:
  - `SetTrackMix`: neither;
  - `SetSongInfo`: notation only;
  - `ReplaceMeasure`: notation only, when the two measures differ only in
    chord symbols, beat texts and lyrics; it compares them as it applies;
  - `SetMeasureHeader`: notation only, when only the section name or the double
    barline changed;
  - `CommandGroup`: what any of its commands changes.
- `core::Document::lastChanges()` gives what the last execute, undo or redo
  changed.
- `ScoreDocument::edited()` lays the score out only when the notation changed,
  and builds the sequence only when the sound did. Otherwise the sequence
  playing goes on as the sequence of the edited song. The sequence that arrives
  last is paired with the song as it is then: the sequencing lane delivers only
  the newest request (ADR-0110), and edits made after it changed no sound.

## Consequences

- Committing a mix runs no layout and no sequence. Credits, chords, texts,
  lyrics and sections run no sequence. The smoke test checks both through the
  lanes' counts.
- A command that wrongly says it changes no sound leaves playback on an old
  sequence. Commands therefore say so only when it is plain from what they
  change, and a new command changes everything until it says otherwise.
- The descriptor can grow, for example which tracks or measures changed, when
  layout and sequencing become incremental.
