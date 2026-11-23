# 0016. Repeats and alternate endings

- Status: Accepted
- Date: 2026-11-23

## Context

Most songs repeat sections: a verse played twice, a chorus with a first and a
second ending. Guitar Pro, MusicXML and every notation program write them as
repeat barlines and alternate endings (voltas), not as copied measures. Until
now the importers dropped them with a warning, so a song played and printed
straight through, and the sequencer of milestone 4 needs to know the order in
which measures are played.

## Decision

**In the model.** Repeats are song-wide, like time and key signatures, so they
live in `MeasureHeader` and move with their measure:

- `repeatStart`: a start repeat barline opens the measure.
- `repeatTimes`: 2 or more when an end repeat barline closes the measure, how
  many times its section is played; 0 for none. Formats store a count of
  plays (Guitar Pro 5-8, MusicXML) or of plays after the first (Guitar Pro 3
  and 4); the model stores plays, which is what a reader sees ("x3").
- `alternateEndings`: the times through the repeat that play the measure, one
  bit each, the first time in the lowest bit; 0 for a measure played every
  time. Eight times are enough for every format we read (Guitar Pro has eight
  ending boxes) and keep the header small.

Jumps (segno, coda, da capo, fine) and section markers come later, with their
own fields.

**The play order.** `Song::playOrder()` lists the measures in the order they
are played; positions and times elsewhere still count measures as written.
Reading from the first measure, with the current section starting at the first
measure and the first time through:

1. A start repeat reached by moving forward starts a new section, first time.
2. A measure with alternate endings that do not include the current time is
   skipped.
3. A played measure that closes a repeat goes back to the start of the section
   while the time through is less than its `repeatTimes`; each jump counts one
   more time.
4. A new section starts, first time, after the last measure of a run of
   alternate endings, and after a finished repeat that no alternate ending
   follows. So an end repeat without a start repeat goes back to the start of
   the song, or to the measure after the previous repeat, as musicians read
   it.

Every jump raises the time through, and a new section starts after the
current measure, so the order always ends; it is also capped at 256 times the
measures.

**In the notation.** A start repeat (thick, thin, dots) follows the
clef and signatures at the start of a system and signature changes elsewhere;
right after a barline it takes that barline's place. An end repeat (dots,
thin, thick) closes its measure, and back-to-back repeats share their thick
line. The dots sit in the two spaces around the middle of the staff, or either
side of the middle space of a staff with an odd number of spaces (six-string
tablature). A repeat played more than twice shows its count ("x3") above its
end. Alternate endings are brackets above the first staff of the system, all
at one height, from the barline before them: a run of measures with the same
endings is one bracket with the numbers ("1., 2.") after its start hook, an
end hook where an end repeat closes it, and no start hook or numbers where it
goes on from the system before.

**In files.**

- `.elara`: `repeatStart`, `repeatTimes` and `alternateEndings` as a list of
  times (`[1,2]`), omitted when unset (ADR-0006).
- Guitar Pro 6-8: `<Repeat start end count>` on the master bar (`count` is the
  number of plays) and `<AlternateEndings>` with the times, separated by
  spaces.
- Guitar Pro 5: header flags 0x04 (start) and 0x08 (end, followed by the
  number of plays) and 0x10 (alternate endings, followed by a byte of bits).
- Guitar Pro 3 and 4: the same flags, but the end repeat counts the plays after
  the first, and an alternate ending is one number. It takes every time up to
  that number that no earlier ending since the start repeat took, so endings
  1, 2 and 3 in a row give times 1, 2 and 3, and an ending 3 after an ending 1
  gives times 2 and 3.
- MusicXML: `<repeat direction="forward">` in a left barline,
  `<repeat direction="backward" times>` (2 when absent) in a right barline,
  and `<ending number="1, 2" type>` from its `start` to its `stop` or
  `discontinue`. The export writes `stop` (a closed bracket) where a repeat
  ends the ending and `discontinue` otherwise. Only the first part is read:
  every part repeats the same barlines.

## Consequences

- Imported songs keep their form: the score shows it, and the sequencer will
  play them in order.
- MIDI files have no repeats: a MIDI export of a song with repeats writes the
  measures as written until the sequencer writes them as played.
- The Guitar Pro 3 and 4 counting is taken from how those versions describe
  repeats; the corpus checks it, and a real file that shows otherwise becomes
  a corpus case and a fix.
- `elara-cli import --json` and `dump --json` show the repeats in the song;
  the play order is checked by tests and will be printed by the sequencer's
  command.
