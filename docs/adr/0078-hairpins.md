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

## Consequences

- Guitar Pro hairpins survive import and saving; they are not yet drawn,
  played, read from MusicXML or typed. Those come next, each recorded here as
  it lands: a wedge under the staff on the dynamics baseline, a velocity ramp
  towards the next dynamic, MusicXML wedges both ways, and editing at the
  caret.
- A wedge that should stop in the middle of a long note cannot: it ends with
  the note. Guitar Pro has the same limit.
- Tests: `io.Guitar Pro: a guitar keeps ...` (a crescendo over two beats, a
  diminuendo written as a decrescendo), the native round trip and text forms.
