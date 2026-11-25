# 0090. Trills

- Status: Accepted
- Date: 2026-11-25

## Context

A trill alternates a note quickly with the note above it for as long as the
note lasts. Elara dropped trills everywhere:

- MusicXML writes `<trill-mark/>` in a note's `<ornaments>`, often followed by
  a `<wavy-line>` running to where the trill ends. The other note is the next
  step up in the key unless an `<accidental-mark>` in the same ornaments
  changes it. MusicXML gives no speed. The importer warned "ornaments
  dropped", and read the wavy line as a vibrato.
- Guitar Pro 3-5 store a trill as the other fret on the same string and a
  speed: 1 sixteenths, 2 thirty-seconds, 3 sixty-fourths. The importer warned
  "trills dropped".
- Guitar Pro 6-8 put `<Property name="Trill"><Trill>n</Trill>` on the note,
  `n` the MIDI key of the other note, at no stated speed. The importer warned
  "trills dropped" too.
- Engraving manuals write "tr" in bold italics centred over the notehead, clear
  of the stems and articulations, with a wavy line after it as far as the next
  note when the note is long. SMuFL has `ornamentTrill` and `wiggleTrill`.
  Tablature writes "tr" over the staff with the other fret in parentheses.

## Decision

**A note can trill**: `NoteEffects::trill`, a `Trill` of `semitones` to the
other note (positive above, negative below, 0 no trill, at most an octave:
`Trill::kMaxSemitones`) and the `speed` the notes alternate at, thirty-seconds
by default. The two fields take two bytes that `NoteEffects` had as padding:
`Note` stays 48 bytes and `NoteEffects` 40.
`semitonesToNextStep()` gives the semitones from a pitch to the next step of a
key, which is where a trill goes unless written otherwise.

**Played** as tremolo picking is (ADR-0025): the note is struck again every
`speed`, every other attack on the other note.

**Kept** by the native format as the note effect `"trill"`:
`{"semitones": 2, "speed": "32"}`. MusicXML reads a trill mark as a trill to
the next step of the key, changed by an accidental mark, and the wavy line
after it as the trill's; it writes trill marks back with an accidental mark
when the other note is not the next step, warning for trills to a lower note
and for speeds other than thirty-seconds, which it cannot write. Guitar Pro 3-5
and 6-8 read trills with their other note, at their speed in Guitar Pro 3-5 and
in sixteenths in 6-8. MIDI export warns "trills dropped": it writes no
ornaments.

**Drawn** on standard notation as `ornamentTrill` centred over the notehead,
`kTrillGap` clear of the ink under it (under the staff for the lower voice of
two), with `Role::Ornament` ("ornament"). When there is room before the next
column a wavy line of `wiggleTrill` follows it through the middle of the
letters and stops `kTrillEndGap` before the next column. Tablature has a lane
with "tr" and the other note's fret in parentheses.

**Edited** with the CLI's `trill <on|semitones|none> [16|32|64]`, where `on`
trills each of the caret's notes to the next step of the key
(`setTrillToNextStep()`), and a "tr" button in the app's entry bar that does
the same or takes the trill off.

## Consequences

- A trill changes how a note plays and how its measure is drawn: the
  incremental layout lays the measure out again as for any other note effect.
- Not covered: wavy lines that run over several notes, trills that start on
  the upper note or end with a turn, the accidental drawn over "tr" for an
  unusual other note, and trills of three-octave or wider intervals.
