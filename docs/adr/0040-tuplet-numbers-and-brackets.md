# 0040. Tuplet numbers and brackets

- Status: Accepted
- Date: 2026-11-23

## Context

Triplets and other tuplets were imported from every format, played with the
right lengths and beamed, but the standard staff drew nothing to show them: a
triplet of eighths looked like three plain eighths that did not fill their
beat. Readers expect a number over the group, and a bracket when the beam does
not show which notes belong to it.

The model stores a tuplet ratio on each `Duration` ("3 in the time of 2"), not
groups: the formats we read either mark only each note's ratio (Guitar Pro,
MIDI after quantization) or mark group starts and stops that do not always
match (MusicXML). Layout has to find the groups itself.

## Decision

**Groups.** In each voice of a measure, consecutive notes and rests with the
same ratio form a group. A group closes as soon as its written length (note
values and dots, without the ratio) is `actual` times a note value no shorter
than any of its notes: three eighths, a quarter and an eighth, or a dotted
eighth, a sixteenth and an eighth close a 3; six sixteenths close a 6 but not
a 3 after three sixteenths, which closes its own 3. A group still open at a
change of ratio, a note without a tuplet or the end of the measure is shown as
it is. Ambiguous writing (a quarter triplet split into two eighths) may show
as two smaller groups; both read correctly.

**Number.** The `actual` count only ("3", "5", "10"), in the font's tuplet
digits (`tuplet0`-`tuplet9`), on the stem side of the group. Ratios such as
"3:2" are not written yet.

**Beam or bracket.** When one beam carries the whole group and the beam holds
only tuplets (one triplet, or two triplets of sixteenths under one beam), the
number stands alone beyond the primary beam, centred between the first and the
last stem, 0.4 spaces from the beam. Otherwise (unbeamed notes, a rest at an
end, a beam shared with plain notes) a horizontal bracket goes from the left of
the first notehead to the right of the last, beyond every notehead, accidental,
stem and flag of the group by 0.4 spaces plus half the number's height, with
0.6-space hooks towards the notes and a gap for the number in its middle. The
bracket uses the font's `tupletBracketThickness`.

The side is the stems' side: the beam's direction for a beamed group, the first
note's stem otherwise, and the voice's direction for a group of rests.

**Output.** Numbers and brackets carry a new `Role::Tuplet` ("tuplet" in JSON
and SVG), so `elara-cli render --json` counts them and renderers draw them with
the rest of the page. They count in the staff's ink, so systems make room for
them.

## Consequences

- Tablature without standard notation still shows no rhythm, so no tuplets
  either; they come with rhythm stems under the tablature.
- The number of a beamed group can sit on a staff line when the beam lies
  inside the staff, as in most engraving programs; moving it out of the staff
  is a later refinement.
- Brackets are horizontal; slanting them with the notes, ratios ("3:2") and
  nested tuplets are not covered.
- Tests: `notation.Staff layout: tuplets show their number` (beamed above and
  below, brackets, a rest at an end, group closing, two digits), and
  `cli.render_both` counts one tuplet in the corpus file.
