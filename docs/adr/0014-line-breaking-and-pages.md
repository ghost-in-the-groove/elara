# 0014. Line breaking, justification and pages

- Status: Accepted
- Date: 2026-11-23

## Context

ADR-0012 and ADR-0013 lay out measures on a single line at their natural
spacing. A score is read on pages, or in the app on a screen of a given width:
measures must be broken into systems that fill the width, the systems stacked
into pages, and a score of several tracks must say which staff is which. The
app will lay out long scores after every edit, so the cost matters, and every
core of the machine should be able to help.

## Decision

**Springs.** Spacing becomes a set of springs per measure: one after each column
of notes, with an ideal length (the rhythmic space of ADR-0012) and the length
its ink needs at least. At stretch 0 a spring is as long as the larger of the
two, which is the natural spacing of ADR-0012 unchanged. A positive stretch adds
that share of the ideal length to every spring, so notes keep their rhythmic
proportions; a negative stretch, down to -0.2, shrinks the ideal lengths but
never below the ink. A system is justified by the one stretch that makes it
exactly as wide as the page: linear above 0, bisected below.

**Measuring pass.** Before breaking lines, every measure is measured with the
same staff writers that draw it, without drawing: the width of its header (if
a system started there), of its signature changes (if one does not), of its
barline, and its springs. Measures are measured in independent blocks of 32 on
every core.

**Line breaking.** The breaks are chosen by dynamic programming over the
measures, minimising for each system the square of its stretch (tightening costs
6.25 times as much, so that tightening by the whole 0.2 weighs like stretching by
half) plus a small cost per system. A measure wider than the page gets a system
of its own. The last system keeps its natural width when it fills less than a
set share of the width (60 % by default). A fixed number of measures per system
can be asked for instead.

**Systems on their own.** Each system is laid out by fresh writers from its
first measure to its last, so systems share nothing but the song: they are laid
out in parallel, and a later incremental relayout can redo one system alone.
What crosses a system break is resolved from the song: a tie into the next
system is drawn as a first half to the end of the line (the writer looks at the
first notes of the next measure) and a second half from the header of the next
line (its first tied note has no start in the line). Technique runs (P.M., let
ring, vibrato) end at the break and start again on the next system.

**Systems.** Every system starts with clef and key signature, and with the time
signature where it changes (a change at the start of a system goes into its
header). Systems after the first show the number of their first measure above
the clef. A system of several staves has a line joining them at its start. When
the staves come from more than one track, the first system is indented and each
track's name is written right-aligned before its staves.

**Pages.** Pages have their origin at the top-left corner and margins on every
side. The first page starts with the title, subtitle and artist centred, and
"Words by" and "Music by" on the left and right; later pages have their number
at the bottom, the first page the copyright. Systems are stacked at least 6 staff
spaces apart between their lines and 2 between their ink; a system that does
not fit goes to the next page. A full page spreads its systems down to the
bottom margin, adding at most 8 staff spaces to each gap. A page height of 0
gives one page as long as the music (the app's vertical scroll view). Page sizes
come from the paper and the staff space in millimetres (A4 with 1.75 mm by
default).

**Display list.** It gains system boxes (measures, position, staff extent and
ink extent), staff boxes with their horizontal extent, measure boxes with their
system's vertical extent, and roles for measure numbers, credits and track
names, so that the app can find what is under the pointer.

## Consequences

- `layoutPages()` gives pages; `layoutSystem()` keeps giving one line at its
  natural spacing, now with its system box. `elara-cli render --layout
  page|scroll` writes pages as JSON or one SVG per page, `--tracks` takes whole
  tracks.
- Layout output is the same on one thread and on many; a test checks it.
- Parallel jobs need threads in the notation library (`elara::core` gives the
  small `forEachIndex()` helper the io module already used).
- Not covered yet: cautionary key and time signatures at the end of a system,
  braces for two-staff instruments, abbreviated track names on later systems,
  hammer-on arcs and slides across a system break, line and page breaks stored
  in the song, tempo marks, and incremental relayout (milestone 3).
