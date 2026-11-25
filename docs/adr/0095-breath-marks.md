# 0095. Breath marks and caesuras

- Status: Accepted
- Date: 2026-11-25

## Context

Wind and vocal parts mark where to breathe with a comma over the staff after
a note; a caesura (two slanted strokes through the top line) marks a short
break in the music. Elara dropped both:

- MusicXML writes `<breath-mark/>` and `<caesura/>` in the `<articulations>`
  of the note they follow. The importer warned "articulations other than
  staccato, tenuto and accents dropped".
- SMuFL has `breathMarkComma` and `caesura`. Engraving manuals put the comma
  over the staff (over the notes when they are higher) between the note and
  the next one, and the caesura through the top line.
- Guitar Pro has neither.

## Decision

**A beat can have a break after it**: `BeatEffects::breath`, a `Breath`
(`None`, `Comma`, `Caesura`). It belongs to the beat, not to a note: all the
voice's notes there breathe together. It takes a padding byte: `BeatEffects`
stays 40 bytes.

**Played** by taking time off the end of the beat's notes, not by adding
time: a breath mark takes a sixteenth, a caesura an eighth, never more than
half of the note (after staccato and the other articulations). The next beat
starts on time.

**Drawn** with `Role::Articulation` right of the beat's ink: the comma over
the staff, clear of the beat's notes and of the marks above them; the
caesura through the top line, from the space under it. The beat's ink
reaches over the mark, so beats close together move apart to make room;
wide enough spacing already holds it.

**Kept** by the native format as the beat effect `"breath"` (`"comma"`,
`"caesura"`). MusicXML reads either mark on any note of the beat (a caesura
wins over a breath mark) and writes it on the beat's first note.

**Edited** with `core::setBeatEffects()`, the CLI's `breath
<comma|caesura|none>` and a button in the app's entry bar that steps the
caret's beat through a breath mark, a caesura and none.

## Consequences

- A tied note sounds its whole length: the break is looked at on the beat
  where the note is struck.
- Not covered: the pause a caesura could add to the timeline, other breath
  mark shapes (tick, salzedo, upbow-like), and breath marks on tablature.
