# 0132. The caret says the beat and the string

- Status: Accepted
- Date: 2026-11-27

## Context

The caret was one small box: on a tablature, as wide as two digits and as
high as the room between two strings, on the string typing goes to (ADR-0021,
ADR-0032). On a page of ten systems it was hard to find, and found, it said
the string and little else: on a string without a note it floated over a
line, and nothing tied it to the note of the same beat in the notation above.

Two questions are asked of a caret: where in the music am I, and what will
the next key change. One box answered the second.

## Decision

- The caret is drawn as two things. A **band** says the beat: a soft wash of
  the accent as wide as the caret, across every view of the caret's staff in
  its system, from above the notation to below the tablature when both are
  shown. A **cell** on the band says the string: the box there was, with a
  frame of the accent and a stronger wash, on the string of a tablature or
  the lane of a drum staff.
- On notation alone the cell is the whole staff, as before: the band is its
  wash and the cell its frame.
- `notation::caretColumn()` gives the band, as `caretBox()` gives the cell;
  the document has it as `caretColumn` beside `caretRect`.
- The caret is 2.4 staff spaces wide (it was 1.8): two digits of a fret
  number at their size since ADR-0131, with room either side.
- While the keys go elsewhere both are faint and the frame thin. On a dark
  page the washes are stronger, where the accent hardly shows.
- The band glides between beats as the cell does; the cell also glides
  between strings.

## Consequences

- The caret is found at a glance, and the beat it is on is read in the
  notation and in the tablature at once.
- The band lies over the notes of its beat. It is a wash of about a tenth,
  so that they are read through it.
- The free slot after a measure's last beat keeps the caret's width from the
  barline, so it sits a little further left than it did.
- A click picks the beat whose box is nearest, as before; the band takes no
  click of its own.
