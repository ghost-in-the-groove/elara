# 0046. Retuning a fretted staff

- Status: Accepted
- Date: 2026-11-23

## Context

A fretted staff has a tuning and a capo, set when its track is created from an
instrument preset or read from a file. Players change both: a song
in drop D, a capo at the second fret, a seven-string part played on six
strings. Every fretted note has a pitch and a position (string and fret) that
must agree (validation checks it), so a new tuning or capo has to move one of
them. Which one depends on what the player wants:

- **The music stays, the fingering changes.** A part written for standard
  tuning is played in drop D, or with a capo to reach easier shapes. The notes
  must sound the same; their frets move.
- **The tablature stays, the music changes.** A riff is written in standard
  tuning but the band tunes down, or the capo moves to suit a singer. The
  shapes stay; every note sounds what its fret now plays.

Guitar Pro and other tablature editors offer both, as a choice in their tuning
dialogs.

## Decision

**`makeRetune(song, Retuning)` builds one undo step, "Change tuning".** It gives
one staff a tuning and a capo (0 to `Tuning::kMaxFret`) and moves its notes as
`Retuning::keep` says. It is `nullptr` when the staff already has that tuning
and capo, so a retuning that changes nothing adds no undo step. A staff without
strings cannot be retuned.

**Keeping the pitches (`Retune::Pitches`, the default)** places the notes as
transposition does (ADR-0026): a note keeps its string when its fret there is
on the neck, else it takes the nearest free string that plays it, a tied or
hammered note first trying the string its note went to, and a natural harmonic
its node. A note on a string the new tuning lacks (seven strings to six) finds
a string the same way. When no string can play a note, the retuning is refused.

**Keeping the frets (`Retune::Frets`)** leaves every position as it is and gives
each note the pitch its fret plays on its string's new open pitch above the
capo. A note that changes is spelled in the key of its measure
(`KeySignature::spell`), as a fret typed at the caret is; notes on unchanged
strings keep their spelling. A note on a missing string, a fret past the neck
above the new capo or a pitch above MIDI 127 refuses the retuning.

**The staff's tuning changes through its own command.** `SetStaffTuning`
swaps the staff's tuning and capo (`Staff::setTuning`); the group puts it
before the `ReplaceMeasure` of every measure that changed.

**Headless first (ADR-0003).** The CLI's `edit` step
`tuning <track> [<preset>|<notes>] [capo <n>] [staff <s>] [frets]` drives it:
a preset name (`guitar-drop-d`) or open strings from the lowest (`D2 A2 D3 G3
B3 E4`), the current tuning when only the capo changes, the current capo when
only the tuning does. The `edit --json` output gives each track's tuning and
capo.

## Consequences

- The song panel can offer tuning presets and a capo field over this command,
  with a choice between keeping the sound and keeping the tablature.
- Keeping the pitches can move notes to awkward positions: it keeps strings
  where it can, but does not look for the best fingering of a whole passage.
- Changing the number of strings of a staff in the app's views (the tablature's
  line count) follows from its tuning; nothing else stores it.
