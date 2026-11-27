# 0052. Incremental relayout

- Status: Accepted
- Date: 2026-11-24

## Context

The app lays out the shown tracks on pages after every edit (ADR-0014,
ADR-0031). On the 500-measure benchmark track that took 3 ms, and 27 ms for all
20 staves, although an edit usually changes one measure. Everything was laid
out again: every measure measured, every system built, every page assembled.

A system's layout does not depend only on its own measures. It reads the
measure before it (a time or key signature change, a repeat barline, an
alternate ending that continues) and the one after it (ties and endings that
go on into the next system), the tempo in force where it starts, and on each
staff the dynamic in force there, which can come from any measure before.
Line breaking is global: a measure that gets wider can move the breaks of
several systems.

## Decision

**`PageLayoutCache` lays out the versions of a song as it is edited.** Each
call gives the pages `layoutPages()` gives, and keeps the song, the measure
widths and every system as laid out before stacking. The next call compares
the new song with the kept one:

- **Measures.** A measure changed when its header differs, or its content on
  one of the shown staves. Measures shared between the two versions
  (ADR-0051) compare by address; others by value. A changed measure and the
  one after it are measured again; the other widths are kept.
- **Line breaks** are found again from the widths: cheap, and the only way
  to keep them what a full layout finds.
- **Systems.** A system is reused when it covers the same measures at the same
  stretch, none of those measures nor the one before or after it changed, and
  what it reads from before it is the same: the tempo in force (checked when a
  header before it, or the start tempo, changed) and, on each staff whose
  notes before it changed, the dynamic in force. Every other system is laid
  out again, on all cores.
- **Pages** are stacked again from the systems. Each system is copied into its
  page and moved into place in one pass, the page's room reserved first, and
  pages are filled on several threads when there is much to copy. Full
  layouts stack the same way.

Other staves or options, another font, another number of measures, or a shown
track whose name, instrument, clefs, tuning or capo differ start again.

**Correctness is tested against a full layout.** A property test plays the
corpus files through until they are 40 measures long, applies random edits
(notes pasted or cleared, ties, palm mutes, dynamics, tempo changes, the start
tempo, repeats, endings, key signatures) and requires every incremental
layout to equal `layoutPages()` exactly, with most systems reused. Removing
any of the conditions above makes it fail.

**The app keeps one cache per document**, shared with the layouts running on
workers; calls run one at a time.

## Consequences

- After a one-note edit, laying out the benchmark track takes 0.59 ms instead
  of 3 ms, and all 20 staves 11 ms instead of 27 ms (most of the rest compares
  measures by value, which ADR-0051 turns into address comparisons).
- The cache holds a copy of every system besides the pages: as much memory as
  the pages of the view shown.
- A new dependency of a system's layout on something outside its measures and
  their neighbours must be added to the reuse conditions; the property test is
  the guard.
- The breaks a writer stores (ADR-0152) are part of what the pages depend on:
  the commands that change them are notation changes, and once the line
  breaker reads them the reuse conditions must too.
