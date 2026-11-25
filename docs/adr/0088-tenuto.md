# 0088. Tenuto and portato

- Status: Accepted
- Date: 2026-11-25

## Context

A tenuto is a short line over or under a notehead: hold the note for its full
value, often leaning on it a little. With a staccato dot it is portato (also
called mezzo-staccato or louré): detached, but less short than staccato.
Elara kept staccato, accent and marcato but dropped tenuto:

- MusicXML writes `<tenuto/>` in a note's `<articulations>`, and
  `<detached-legato/>` for portato as one mark. The importer warned
  "articulations other than staccato and accents dropped".
- Guitar Pro 6-8 keep the note's articulations as flags in `<Accent>`
  (1 staccato, 4 heavy accent, 8 accent, 16 tenuto). The importer warned
  "tenuto marks dropped". Guitar Pro 3-5 have no tenuto.
- Engraving manuals put staccato and tenuto nearest the notehead, each in a
  space when inside the staff, with the dot closer than the line when both
  are there, and accents outside them. SMuFL has the line as
  `articTenutoAbove`/`articTenutoBelow`.

## Decision

**A note can be tenuto**: `NoteEffects::tenuto`, a flag next to `staccato`.
Both together are portato; there is no third value.

**Played**: a staccato note sounds half its written length (ADR-0022); a
portato note three quarters of it; a tenuto note all of it, as a note
without marks already does.

**Drawn** on the notehead side of the chord: the staccato dot nearest, the
tenuto line beyond it, each in a space when it falls inside the staff, then
the accent. Portato is drawn as the two marks, not as SMuFL's combined glyph:
the combined glyph is about a staff space high, so inside the staff its dot
or its line would sit on a staff line.

**Kept** by the native format as the note effect `"tenuto"`. MusicXML reads
`<tenuto/>` and `<detached-legato/>` (tenuto and staccato) and writes
`<tenuto/>` beside `<staccato/>`; Guitar Pro 6-8 read the tenuto flag. MIDI
export reports "tenuto dropped", as it does for staccato.

**Edited** as the other note effects (ADR-0037): `NoteEffect::Tenuto` toggles
it on the caret's notes, the CLI's `effect tenuto` and the `_` key in the app.

## Consequences

- Portato needs no state of its own: every reader and writer that knows
  staccato and tenuto handles it.
- Not covered: staccatissimo (since ADR-0094) and other articulations
  (stress, unstress, spiccato), which still read as staccato or warn.
