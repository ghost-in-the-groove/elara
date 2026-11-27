# 0150. Layout issues

- Status: Accepted
- Date: 2026-11-27

## Context

Layout is automatic (ADR-0012, ADR-0014): every measure is placed by rules,
never by hand. The layout adjustments that follow (breaks kept where the
writer puts them, room before a beat, a measure looser or tighter, settings of
the page) all need one thing first: a way to say, without eyes, whether a page
came out right. Tests so far check positions they know in advance. Nothing
checked a whole page of any song for the faults a reader notices first:

- noteheads, accidentals, fret numbers, syllables, chord names or dynamics
  drawn into each other;
- a measure wider than the page, which gets a system of its own that runs past
  the right margin.

## Decision

- `elara/notation/layout_issues.hpp` reads finished pages and returns their
  `LayoutIssue`s: an `Overlap` of two roles that must never touch, or a
  `MeasureTooWide`. Each has its page, its measure and a box: where the ink
  overlaps, or the part of the system past the margin. It does not change
  layout, `PageLayoutCache` or the display list.
- **What must never touch** is a table in the source: notehead with notehead
  and with accidental, accidental with accidental, fret number with fret
  number, syllable with syllable, chord name with chord name, dynamic with
  dynamic. Lines, beams and curves are left out: their boxes are too coarse
  to judge by.
- **Ink.** Glyphs are where the font's bounding boxes put them; texts where
  layout assumes they are (`textWidth()`, its ascent and descent); digits, the
  X of a dead note and the angle brackets of a natural harmonic from their
  baseline up to their cap height. Items are compared only within the
  system whose ink holds them; ink outside every system (credits) is not
  looked at.
- **Overlap** means going into each other by more than 0.15 staff space in both
  directions. Neighbours drawn to touch reach into each other by a stem's
  thickness (the two noteheads of a second either side of their stem), so a
  smaller tolerance would report the engraving's own rules as faults. The same
  glyph at the same place is one notehead drawn twice (a unison two voices
  share), not an overlap.
- **The sweep**: per page, the boxes of the table's roles in horizontal bands
  two staff spaces high (a box in every band it reaches), sorted by system,
  band and left edge; each is compared only with the boxes of its band that
  still reach past its left edge, and an overlap is reported in the band that
  holds its top. Without bands every staff of a system is open at once, and a
  box of a 20-staff system meets some forty others. O(n log n) per page; its
  buffers are reused from page to page.
- `elara-cli render ... --issues` reports them, as `"issues":[...]` in JSON
  (`kind`, `page`, `measure`, `roles`, `box`) and one line each in text. With
  the one-line layout there is no page, so only overlaps are reported.

## Consequences

- Every later layout change has an oracle: a property test can lay out the
  corpus, or songs made at random, and require no issue. The corpus, laid out
  on A4 as notation, tablature and both, has none today, and a test keeps it
  so; another lays each file out again after each of 50 random edits (the
  incremental layout test's, with lyrics, chord symbols and a second voice
  added) and checks the pages each edit changed. What it found the engine now
  does by itself: the accidentals of two voices' chords stand together
  (ADR-0151); a fret number in parentheses (a ghost or tied note) is written
  at 0.8 of the size of the others, so that its parentheses, which reach
  below the line, clear the numbers on the strings beside it; a note read
  without a fret position takes the lowest fret on a string no other note of
  its column is on, in any voice.
- Two voices of one staff a second apart, or in unison with different
  noteheads, do collide (ADR-0012 lists it as not covered): the oracle finds
  it on a song written for it. No corpus file has the case.
- A fault the table does not name (a slur through a fret number, a tie through
  an accidental) is not found. The table grows with the faults found, each row
  with a test.
- Finding the issues of every A4 page of the 20-staff benchmark score is a row
  of `docs/performance.md`.
