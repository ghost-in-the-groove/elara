# 0104. Triplet feel

- Status: Accepted
- Date: 2026-11-25

## Context

Blues, shuffles and swing are written in straight eighths and played long
and short: the first eighth of each pair takes two thirds of the beat, the
second one third, as if the pair were a triplet quarter and eighth. Scores say
so once, with a mark above the staff, instead of writing every triplet. Guitar
Pro calls it triplet feel; Elara dropped it with a warning ("triplet feel
dropped") and played those songs straight.

How the formats keep it:

- Guitar Pro 3-4 have one flag for the whole song: triplet eighths.
- Guitar Pro 5 give each measure header a byte: none, triplet eighths or
  triplet sixteenths.
- Guitar Pro 6-8 give a master bar a `<TripletFeel>`: `NoTripletFeel`,
  `Triplet8th`, `Triplet16th`, `Dotted8th`, `Dotted16th`, `Scottish8th` or
  `Scottish16th`. Dotted pairs play three quarters and one quarter of their
  length; Scottish ones (the Scotch snap) one quarter and three quarters.
- MusicXML 4.0 writes `<swing>` in a `<sound>` element: `<straight/>`, or the
  ratio of the pair as `<first>` and `<second>` (2 and 1 for triplets, 3 and 1
  for dotted, 1 and 3 for Scottish) with a `<swing-type>` of `eighth` (the
  default) or `16th`.
- Engraving shows it above the staff where it starts, like a tempo mark: two
  beamed eighths equal a quarter and an eighth under a triplet bracket. SMuFL
  has text-based notes, beams and tuplet brackets for such marks (U+E1F0 to
  U+E20F), which Bravura draws.

## Decision

**A measure header keeps it**: `MeasureHeader::tripletFeel`, a `TripletFeel`
(`None`, `Eighth`, `Sixteenth`, `DottedEighth`, `DottedSixteenth`,
`ScottishEighth`, `ScottishSixteenth`), song-wide like the tempo (ADR-0042),
one value per measure as Guitar Pro 5-8 store it. The native format writes it
as the header's `"tripletFeel"`, by name, only where it is not `None`.

**Played** by moving time inside each pair of the feel's note value, counted
from the start of the measure: `swung(feel, offset, measureLength)` maps the
first half of a pair onto its first part (two thirds, three quarters or one
quarter of it) and the second half onto the rest, in straight lines. The
edges of a pair do not move, so quarter notes and longer play as written and
the measure keeps its length; notes inside a half move with it, so sixteenths
under an eighth feel swing too. A pair that the end of the measure cuts (the
last eighth of a 7/8 measure) plays straight. The sequencer, the timeline and
MIDI export use it for the start and the end of every note (MIDI export for
the lyrics too, `applyTripletFeel()`); it is exact in fractions, so MIDI
export still finds whole ticks, and a swung pair read back from the file is
the triplet it was played as.

**Read** from Guitar Pro 3-8 (Guitar Pro 3-4's song flag on every measure)
and from MusicXML's `<swing>`, which holds from its measure until the next
one; other ratios and note values are dropped with a warning ("unusual swing
dropped"). **Written** to MusicXML as a `<sound>` with `<swing>` in the first
part where the feel changes, `<straight/>` where it ends.

**Changed** from a measure until the next change of it, like the signatures
(ADR-0033): `changeTripletFeel()`, one undo step ("Change triplet feel"), and
the CLI's `feel <m> <name>` step; new measures continue the feel before them.

## Consequences

- Shuffles and swing tunes from Guitar Pro play as they are meant to.
- The mark above the staff and setting the feel in the measure popup come
  next; until then the feel is heard but not shown.
