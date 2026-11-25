# 0078. Hairpins

- Status: Accepted
- Date: 2026-11-25

## Context

A dynamic holds until the next one (ADR-0044, ADR-0047), so the model can only
jump from one loudness to another. Printed music also writes hairpins: a wedge
under the staff that opens (crescendo) or closes (diminuendo) over the notes
that get louder or softer, usually towards the next dynamic mark. Guitar Pro
6-8 files store them as `<Hairpin>` on each beat they cover; MusicXML writes a
`<wedge>` that starts and stops at two points in time. The Guitar Pro importer
dropped them with a warning, and the MusicXML importer still does.

## Decision

**A hairpin belongs to a beat.** `BeatEffects::hairpin` is `None`, `Crescendo`
or `Diminuendo`. Consecutive beats of a voice with the same hairpin, rests
included and across measures, share one wedge, which ends where its last beat
ends. This is how Guitar Pro stores them, and it keeps every edit that copies,
moves, inserts or deletes beats correct with no spans to keep in step. Two
wedges of the same kind that touch become one.

**Files.**

- Guitar Pro 6-8: `<Hairpin>Crescendo</Hairpin>` and `Decrescendo` (or
  `Diminuendo`) are read; another value is dropped with a warning.
- The native format writes `"hairpin":"crescendo"` or `"diminuendo"` in a
  beat's effects and omits it otherwise; format version 1 still reads every
  older file (ADR-0006).
- MusicXML and MIDI exports report hairpins as dropped until they write them.

**The score draws a wedge.** Under the staff, on the baseline of the
dynamics marks (ADR-0044) and centred on the middle of their letters, as two
lines `hairpinThickness` thick with `Role::Hairpin` ("hairpin"):

- From the column of its first beat to 0.4 spaces before the column after its
  last, or before the barline; a run over a barline in one line is one wedge.
- A crescendo opens from a point to 1 space, a diminuendo closes from 1 space
  to a point. A wedge cut by the end of a line ends half open (0.5 space) and
  goes on from half open at the start of the next.
- It keeps 0.4 spaces from a dynamics mark it would touch: a mark in its first
  half moves its start right, one in its second half its end left; it is at
  least 1 space long.
- Under tablature when the system shows the staff only as tablature, like the
  marks.

## Consequences

- Guitar Pro hairpins survive import and saving and are drawn; they are not
  yet played, read from MusicXML or typed. Those come next, each recorded here
  as it lands: a velocity ramp towards the next dynamic, MusicXML wedges both
  ways, and editing at the caret.
- A staff with hairpins is taller by its dynamics lane, so lines and strips
  may break differently; the render snapshots of the corpus change.
- A wedge that should stop in the middle of a long note cannot: it ends with
  the note. Guitar Pro has the same limit.
- Tests: `io.Guitar Pro: a guitar keeps ...` (a crescendo over two beats, a
  diminuendo written as a decrescendo), the native round trip and text forms,
  `notation.Hairpins: ...` (a wedge clear of the dynamics, a diminuendo over a
  barline, cut by the end of a line, under tablature alone) and
  `cli.render_hairpins`.
