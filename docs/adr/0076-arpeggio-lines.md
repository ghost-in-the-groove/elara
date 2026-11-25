# 0076. Strums of keyboards are arpeggio lines

- Status: Accepted
- Date: 2026-11-25

## Context

A beat's strum (`BeatEffects::strum`) is drawn as an arrow above the staff
the way the pick goes, as strumming patterns write it for guitars. The same
strum comes from a piano's rolled chord: Guitar Pro 3-5 store it as a
stroke, like a guitar's strum, and MusicXML's `<arpeggiate>` is read as one
(rolled upwards is a downstroke, ADR-0025). Keyboard and harp music writes a
rolled chord as a vertical wavy line left of the chord, plain when it rolls
upwards and with an arrowhead at the bottom when it rolls downwards. An arrow above a
piano chord reads as a guitar strum and says nothing about which notes roll.

## Decision

**On the standard staff of an instrument that is neither fretted nor
percussion, a strummed chord is drawn with an arpeggio line left of it
instead of an arrow above the staff.**

- The line is a run of cubic Bézier half waves (role `arpeggio`), about
  0.4 staff spaces from side to side, from half a space above the column's
  top notehead to half a space below its bottom one, over every voice of the
  column. It stands 0.25 spaces left of the column's leftmost ink
  (accidentals included), in 0.6 spaces of room the column takes for it, so
  spacing keeps it clear of the previous column.
- A chord rolled upwards (`Strum::Down`, low notes first) has a plain line;
  one rolled downwards (`Strum::Up`) ends in a filled arrowhead pointing
  down.
- Fretted instruments keep their arrows above the staff and in tablature.
- The line is drawn with curves and a polygon, so the SVG and PDF writers
  and the app draw it as they draw ties and bend arrowheads, with no new
  glyph and no rotated text.

## Consequences

- Pianos, harps and other instruments read like engraved keyboard music.
- On a grand staff each staff of a chord rolled across both hands has its own
  line; a single line through both staves would need the layout of a system
  to know the columns of the staves under each other.
- A roll whose direction the source leaves open (a MusicXML `<arpeggiate>`
  without `direction`) rolls upwards, and so draws the plain line.
