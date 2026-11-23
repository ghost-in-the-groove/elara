# 0041. Rhythm under tablature

- Status: Accepted
- Date: 2026-11-23

## Context

Tablature alone shows which fret to play but not when: with the standard
notation hidden (the "tab" view, the one most guitarists read), a quarter, a
triplet and a dotted eighth looked the same, and only the spacing hinted at the
rhythm. Guitar tablature books and every tablature editor solve this by writing
the rhythm under the staff: stems, beams, flags and dots without noteheads.

## Decision

**When.** The tablature of a staff writes its rhythm when the system shows no
standard notation of that staff, the same condition under which it already
writes rests (ADR-0013). Under standard notation it stays as it was: the
notation above already shows the rhythm.

**Which voice.** The first voice of each measure that has notes. Other voices
of the measure show their fret numbers but not their rhythm; two rhythms under
one staff would need stems up and down through the fret numbers, which no
reader of tablature expects.

**How.** Each note of that voice gets what standard notation with stems down
would give it, without the notehead:

- a stem under the centre of its fret numbers, from 0.8 spaces below the last
  string, 2.5 spaces long for quarters and shorter, half that (its lower half)
  for halves, none for whole notes and rests (rests stay on the staff);
- beams grouped by beat exactly as in standard notation, horizontal at the end
  of the stems with the secondary beams above the primary one, and a short beam
  for a lone shorter note in its group; flags for notes outside a group;
- dots right of the stem, 0.9 spaces above its end, with room for them in the
  column's ink;
- tuplet numbers under the beam, or a bracket under the stems, by the rules of
  ADR-0040.

Beam groups and tuplet groups are computed by one function each
(`beamGroups()`, `tupletSpans()` in `layout_common`), shared with the standard
staff, so the two views always group the same notes. The items carry the
existing roles (`stem`, `beam`, `flag`, `dot`, `tuplet`), so `elara-cli render
--json` counts them and renderers need nothing new. They count in the staff's
ink, so systems make room under the tablature.

## Consequences

- The tab view reads like a tablature book; the page holds a little fewer
  systems because each one is taller.
- Stems do not shorten or lengthen with the notes: every stem of a line ends at
  the same depth, which keeps beams horizontal and easy to read. Notes with four
  or more beams (sixty-fourths) crowd their stems.
- Ties, rhythm of other voices and rhythm slashes are not covered.
- Tests: `notation.Tablature layout: rhythm under the tablature without
  notation` (stems under the fret numbers, beams, a shorter half-note stem,
  flags, dots, rests, whole notes, secondary beams and beamlets, a triplet,
  none under standard notation), and `cli.render_tab` counts the stems, flag,
  beam and dot of the corpus file.
