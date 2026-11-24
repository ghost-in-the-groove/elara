# 0050. Tuplets at the caret

- Status: Accepted
- Date: 2026-11-23

## Context

A `Duration` carries a tuplet ("3 in the time of 2"), the importers read them,
playback times them and the score draws their numbers and brackets (ADR-0040).
The CLI types them already (`duration 8*3:2`), since keyboard entry (ADR-0021)
gives a beat whatever duration it is asked for and fills the rest of a short
measure. The app's input duration, though, held only a note value and a dot:
a triplet could not be typed from the score.

## Decision

**The input duration has a tuplet.** `ScoreDocument::setInputTuplet(actual)`
makes it a tuplet of `actual` notes, or plain again when it already is that
one, and gives it to the caret's beat as a new note value or dot does (one
"Change duration" step). Changing the note value or the dot keeps the tuplet.

**The tuplets offered are the usual ones**, each in the time of the nearest
smaller power of two, as notation programs and Guitar Pro write them: 3:2,
5:4, 6:4, 7:4 and 9:8, with the duplet 2:3 and the quadruplet 4:3 for
compound meters. Other ratios still come from files and the CLI.

**The input follows the caret's beat** when that beat has one of those
tuplets, so typing on after a triplet goes on in triplets; a beat with another
ratio leaves the input as it was.

**Keys and the entry bar.** Ctrl+2 to Ctrl+9 (but Ctrl+8) pick a tuplet, or
none when the input has it. The entry bar has a tuplet button that toggles a
triplet and shows the input's tuplet in the music font's tuplet digits; a
right click or a long press opens a menu of the others.

## Consequences

- A tuplet group is not a unit: the beats of a triplet are three beats with
  the same tuplet, as in the model. Typing two triplet eighths and a plain
  quarter is allowed; the score's tuplet brackets show what was typed.
- Nothing stops a group being cut by a barline or left incomplete; measure
  validation reports the measure as for any short or long voice.
