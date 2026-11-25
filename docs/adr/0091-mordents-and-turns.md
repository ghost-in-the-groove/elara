# 0091. Mordents and turns

- Status: Accepted
- Date: 2026-11-25

## Context

Mordents and turns are short ornaments: a few quick notes on the steps of
the key next to a note, played at its start, then the note itself.

- A mordent (a zigzag crossed by a stroke) dips to the step below and back;
  an upper mordent (the zigzag alone, also called inverted mordent or short
  trill) goes to the step above and back. A turn (a lying S) goes round the
  note from above: the step above, the note, the step below, the note; an
  inverted turn from below.
- MusicXML writes them in a note's `<ornaments>` as `<mordent/>`,
  `<inverted-mordent/>` (the upper one), `<turn/>` and `<inverted-turn/>`,
  besides delayed and vertical turns, shakes and others. The importer warned
  "ornaments dropped".
- Guitar Pro 6-8 put `<Ornament>` on a note: `LowerMordent`, `UpperMordent`,
  `Turn` or `InvertedTurn`. The importer warned "ornaments dropped". Guitar
  Pro 3-5 have none.
- SMuFL has `ornamentMordent`, `ornamentShortTrill`, `ornamentTurn` and
  `ornamentTurnInverted`. Like a trill, the mark is centred over the
  notehead, clear of stems and articulations.

## Decision

**A note can have an ornament**: `NoteEffects::ornament`, an `Ornament`
(`None`, `Mordent`, `UpperMordent`, `Turn`, `InvertedTurn`). It takes a byte
`NoteEffects` had as padding: `Note` stays 48 bytes. `ornamentSteps()` lists
an ornament's quick notes as steps of the key from the note, and
`semitonesToPreviousStep()` joins `semitonesToNextStep()` (ADR-0090) to turn
them into semitones.

**Played** by the sequencer as quick notes at the start of the note, each a
thirty-second, or an equal share of a note too short for them, then the note
for the rest of its length. A trill on the same note wins: it already plays
the note through.

**Kept** by the native format as the note effect `"ornament"`: `"mordent"`,
`"upper-mordent"`, `"turn"`, `"inverted-turn"`. MusicXML reads and writes the
four marks; delayed and vertical turns still warn. Guitar Pro 6-8 read
`<Ornament>`. MIDI export warns "mordents and turns dropped": it writes no
ornaments.

**Drawn** on standard notation as the trill's "tr" is: centred over the
notehead, `kTrillGap` clear of the ink, under the staff for the lower voice
of two, with `Role::Ornament`; a trill on the same chord takes the place.
Tablature shows the glyph in the ornament lane, where a trill's "tr" goes.

**Edited** with the CLI's `ornament <mordent|upper-mordent|turn|inverted-turn|none>`
and four buttons in the app's entry bar, next to the trill's.

## Consequences

- An ornament changes how a note plays and how its measure is drawn: the
  incremental layout lays the measure out again as for any other note effect.
- Bravura's mordents are almost three staff spaces wide: on consecutive
  quarter notes they come close to touching, since the spacing does not
  widen for ornaments.
- Not covered: the accidentals drawn over or under an ornament for a step
  outside the key, delayed turns (after the note), long mordents, shakes and
  the other MusicXML ornaments.
