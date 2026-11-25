# 0103. Double barlines

- Status: Accepted
- Date: 2026-11-25

## Context

A double barline, two thin lines, closes a section of a piece: the verse
before the chorus, the bars before a key change or a coda. Guitar Pro and
MusicXML files carry it, and Elara dropped it without a warning, drawing a
single barline, so the sections of an imported score no longer stood out.

How the formats keep it:

- Guitar Pro 3-5 set bit `0x80` of a measure header's flags.
- Guitar Pro 6-8 give the master bar a `<DoubleBar/>` element.
- MusicXML writes `<barline>` with `<bar-style>light-light</bar-style>` on the
  right of the measure, or on the left of the next one.
- SMuFL's engraving defaults space the two lines by `barlineSeparation`,
  from the right edge of the first to the left edge of the second.

## Decision

**A measure header keeps it**: `MeasureHeader::doubleBar`, song-wide like the
repeat barlines (ADR-0016), so inserting, deleting and undoing measures carry
it with no more code. The native format writes it as the header's
`"doubleBar": true`.

**Drawn** as two thin lines `barlineSeparation` apart, on every staff, in the
place of the measure's single barline. An end repeat, or the song's final
barline, shows instead of it; so does a start repeat opening the next
measure, which marks the division and would otherwise overlap its second
line. The flag stays in the model either way.

**Read** from Guitar Pro 3-8 and from MusicXML's `light-light` barlines, left
or right, and **written** to MusicXML as a `light-light` barline on the right
of the measure where it shows. One under an end repeat or at the song's end
is not written, since it does not show; other barline styles (dashed, heavy)
are read as before, as single barlines.

## Consequences

- Imported scores keep the ends of their sections; songs without double
  barlines lay out and save exactly as before.
- A double barline costs its measure the separation and a line's width.
- The measure popup sets it with a Double chip under the repeats, and
  `elara-cli edit` with `barline <m> double|single`; each is one undo step,
  "Change barline".
