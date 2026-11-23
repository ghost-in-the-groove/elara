# 0012. Staff layout and the display list

- Status: Accepted
- Date: 2026-11-23

## Context

The notation engine (ADR-0011) must turn a staff of the model into something
the app draws with the Qt scene graph, `elara-cli` writes as SVG and tests can
check without a screen (ADR-0003). Engraving has many rules (where stems point,
which notes share a beam, when an accidental is needed, how much room a note
gets). They need to be written down once, so that the renderers agree and the
tests state what is expected.

## Decision

**A display list.** Layout produces glyphs (a SMuFL glyph and its origin),
lines (two points and a thickness: staff lines, ledger lines, barlines, stems)
and beams (parallelograms with vertical ends). Lengths are in staff spaces, y
grows downwards and the top staff line is y = 0. Every item has a role
(notehead, stem, accidental...) and the index of its measure, so renderers can
style items and editors can pick them. Renderers choose the size of a staff
space and the colours; they never re-run engraving rules.

**One staff on one line first.** `layoutStaff()` lays out a range of measures
side by side, each at its natural width. Systems and pages (milestone 3) break
that line and stretch measures to fill each system; tablature is another layout
of the same staff.

**Written pitch.** Notes are drawn at the sounding pitch moved up by the
instrument's written offset, spelled with the interval those semitones usually
make (`round(semitones × 7 / 12)` steps: a major second for 2, a major sixth for
9); the key signature moves by the matching number of fifths.

**Spacing.** Notes of all voices that start together form a column. After a
column comes its ideal distance, 3 staff spaces for a quarter note multiplied
by √2 for each doubling of the duration (half the time, 1/√2 the room), unless
the ink on either side (accidentals, seconds on the other side of the stem,
dots, flags) needs more, with 0.4 staff spaces between inks.

**Stems.** A lone voice points its stems away from the middle line, judged by
the note farthest from it (a note on the middle line points down). With several
voices the first and third point up, the second and fourth down. On a drum staff
the drum map decides when hands and feet have separate voices: hands up, feet
down. Stems are 3.5 staff spaces long (longer for 32nd notes and shorter), and
always reach the middle line.

**Beams.** Eighth notes and shorter of one voice are beamed within one beat of
the meter (a dotted quarter in compound meters such as 6/8 and 12/8); rests and
notes that cross the beat break the group. The beam slants by half the interval
between its outer notes, at most one staff space, and lies flat when an inner
note reaches beyond both. It sits so that every stem is at least 3.5 staff
spaces long (0.5 more for each extra beam) and reaches the middle line.
Secondary beams join runs of shorter notes; a lone one gets a short beam that
points right at the start of the group and left elsewhere.

**Accidentals.** A note shows an accidental when neither the key signature nor
an earlier note on the same line or space in the measure (in any voice, in time
order) already gives its alteration. Tied notes carry theirs over without one.
In a chord, accidentals stack in columns that keep three staff spaces apart.

**Drums.** Percussion staves place General MIDI keys with the default drum map
of `docs/instruments.md` (position, notehead, hand or foot).

**SVG.** The SVG writer draws glyphs as text in the music font (one em is four
staff spaces), loads the font with `@font-face` and uses staff spaces as user
units, so snapshots do not depend on a renderer's font rasteriser.

## Consequences

- Tests assert positions in staff spaces against the font's metrics; `elara-cli
  render --json` and `--svg` show the same list for a whole file.
- The constants are named, in `standard_staff.cpp` and (spacing)
  `layout_common.hpp`; changing one shows up in the layout tests and, later,
  the SVG snapshots.
- ADR-0013 adds tablature, ties, articulations and staves aligned in systems.
  Not covered yet: slurs, tuplet numbers and brackets, other markings, grace
  notes, notes of different voices colliding on one line, justification.
