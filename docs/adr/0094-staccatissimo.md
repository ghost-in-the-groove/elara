# 0094. Staccatissimo

- Status: Accepted
- Date: 2026-11-25

## Context

A staccatissimo is a small wedge over or under a notehead: play the note
shorter and sharper than a staccato. Elara read MusicXML's `<staccatissimo/>`
as a plain staccato (ADR-0088), so the wedge came back as a dot and the note
sounded as long as a staccato one.

- MusicXML writes `<staccatissimo/>` in a note's `<articulations>`, apart
  from `<staccato/>` and `<spiccato/>`.
- SMuFL has the wedge as `articStaccatissimoAbove`/`articStaccatissimoBelow`.
  Engraving manuals place it as a staccato dot: nearest the notehead, in a
  space when inside the staff.
- Guitar Pro 3-5 have no staccatissimo; how Guitar Pro 6-8 would keep one is
  not documented where we could check it.

## Decision

**A note can be staccatissimo**: `NoteEffects::staccatissimo`, a flag next
to `staccato` that takes one of the padding bits: `Note` stays 48 bytes.
Toggling one of the two on takes the other off; a note read with both is
staccatissimo.

**Played**: a staccatissimo note sounds a quarter of its written length,
with a tenuto too; a staccato one half of it (ADR-0022, ADR-0088).

**Drawn** where the staccato dot goes, in its place, the tenuto line and the
accents beyond it as before.

**Kept** by the native format as the note effect `"staccatissimo"`. MusicXML
reads and writes `<staccatissimo/>`; `<spiccato/>` still reads as a
staccato. MIDI export reports "staccatissimo dropped", as it does for
staccato.

**Edited** as the other note effects (ADR-0037): `NoteEffect::Staccatissimo`
toggles it on the caret's notes, the CLI's `effect staccatissimo` and the `"`
key in the app.

## Consequences

- Every reader that knew staccato needs nothing more to read a file without
  staccatissimo; files with it now keep the wedge.
- Not covered: spiccato as its own mark, the stroke form of the wedge
  (`articStaccatissimoStroke`), and stress and unstress marks.
