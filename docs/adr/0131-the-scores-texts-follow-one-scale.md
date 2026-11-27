# 0131. The score's texts follow one scale

- Status: Accepted
- Date: 2026-11-27

## Context

The notes of a score were drawn at the size engraving gives them, and the
words round them at less. At a staff space of 1.75 mm, one staff space is
4.96 pt: a tempo mark at 1.4 was type of 6.9 pt, a chord symbol and a lyric at
1.5 were 7.4 pt, "P.M." at 1.25 was 6.2 pt, the credits 7.4 pt. Engraving sets
a tempo at 12 pt, chord symbols, lyrics and the composer at 10 pt, fret
numbers at 9 pt and measure numbers at 8 pt (the defaults of MuseScore, which
printed editions agree with). On screen at paper size the tempo's text was 9
pixels high and its note smaller than a notehead; ADR-0113 had raised the
tablature's texts once, and they were still the smallest of their kind.

Each size was a constant chosen when its feature was written, so nothing
kept them in proportion to each other.

The larger marks also showed that two tempo marks close together were drawn
one over the other: a change a beat after another, or a triplet feel mark
before the next measure's change, ran into the mark that followed.

## Decision

- The texts of the score follow one scale, in staff spaces, about a quarter
  to a third larger than before and close to what engraving uses:

  | Text | Was | Is | On paper |
  | --- | --- | --- | --- |
  | Title | 4.0 | 4.4 | 21.8 pt |
  | Subtitle | 2.4 | 2.8 | 13.9 pt |
  | Artist | 2.0 | 2.4 | 11.9 pt |
  | Words, music, copyright, page numbers | 1.5 | 1.8 | 8.9 pt |
  | Tempo, and its note at 0.9 of the staff's size (was 0.65) | 1.4 | 2.0 | 9.9 pt |
  | Section name | 1.5 | 2.0 | 9.9 pt |
  | Chord symbol | 1.5 | 2.0 | 9.9 pt |
  | Lyrics | 1.5 | 1.9 | 9.4 pt |
  | Jumps ("D.S. al Coda"), track names | 1.4, 1.6 | 1.9 | 9.4 pt |
  | Beat text | 1.4 | 1.8 | 8.9 pt |
  | Fret numbers | 1.6 | 1.8 | 8.9 pt |
  | Alternate endings and repeat counts | 1.4 | 1.7 | 8.4 pt |
  | "P.M.", "let ring", harmonic labels | 1.25 | 1.55 | 7.7 pt |
  | Measure numbers | 1.2 | 1.5 | 7.4 pt |
  | The amount over a bend | 1.2 | 1.45 | 7.2 pt |
  | The whammy bar's label, a diagram's first fret | 1.1, 1.2 | 1.35, 1.4 | 6.7, 6.9 pt |
  | "H" and "P" over an arc | 1.05 | 1.3 | 6.4 pt |

- What holds a text grows with it: the lanes of the techniques, the box of a
  section, the gaps of a tempo mark and of a triplet feel mark, the line
  between two credits. A fret number's capitals are 1.3 staff spaces high
  between lines 1.5 apart, so it still clears the lines above and below.
- Chord diagrams grow with their names: strings 1.0 staff space apart and
  frets 1.2 (they were 0.85 and 1.0), the dots and the open and muted marks
  at 1.1 of the font's size.
- A tempo mark that would run into the mark before it on its line goes up a
  line, as high as the tallest mark of the system, and the system makes room
  for the lines it needs. A triplet feel mark stays with its tempo.
- This replaces the sizes of ADR-0113; its decision on the tablature's time
  signature stands.

## Consequences

- A score reads at paper size on screen: a tempo's type of 9 pixels is 13,
  and a fret number's of 10.5 is 12.
- Systems with words over or under them are a little higher, and measures
  with chord symbols, lyrics or two-digit frets a little wider. A long song
  takes more pages: a 7-string guitar part of 14 pages takes 15.
- A last system that was just short of being spread over the line may now be
  spread.
- Every render snapshot of the corpus changed; they were regenerated and
  looked at.
- The sizes are still constants of the layout (`page_layout.cpp`,
  `system_layout.cpp`, `layout_common.cpp`, `tab_staff.cpp`). A style the
  user can change would replace them, and this table is where it would start.
- The interface has one regular and one italic face for the score's texts, so
  size alone tells a tempo from a fingering; a bold face would let the tempo
  and the section names be told apart at a smaller size.
