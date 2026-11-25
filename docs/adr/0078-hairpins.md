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
- MusicXML: a `<wedge>` that starts (`crescendo`, `diminuendo`) and later
  stops puts its hairpin on the beats of the staff's first voice that start
  between the two, carried across measures; export writes a wedge from the
  first beat of each run to the end of its last, numbered by voice, under the
  staff it belongs to.
- The MIDI export writes the velocities hairpins ramp (below).

**Playback ramps the velocity.** `soundingNotes()` turns the hairpins of a
staff, whichever of its voices hold them, into wedges and moves the velocity
of every note of the staff struck under one:

- A wedge leads from the loudest notes struck at or before its start (mf when
  none) to the loudest struck at or after its end, when they are louder for a
  crescendo or softer for a diminuendo; otherwise to one dynamic further (16
  velocity), clamped to 1 to 127.
- A note under the wedge moves by the share of that change its start has
  reached: the first note keeps its velocity, a note halfway gets half the
  change, and the note after the wedge plays its own dynamic.
- Ties only sustain, so a tied note keeps the velocity of its attack. The
  sequencer, audio rendering and MIDI export all play these velocities.

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

**Editing.** A hairpin is typed like the other beat effects (ADR-0056): the
CLI step `hairpin <crescendo|diminuendo|none>` and the entry bar's two wedge
buttons, after the dynamics, set it on the caret's beat as one undo step
("Change beat effects"). A longer wedge is typed beat by beat, so it needs no
selection and no new kind of edit; the status line names it.

## Consequences

- Hairpins from Guitar Pro and MusicXML survive import, saving and MusicXML
  export, are drawn, are heard and are typed at the caret.
- A MusicXML wedge in a second voice comes back on the first voice.
- A hairpin with no dynamic after it rises or falls one step and the music
  goes back to its written dynamic after it; writing the dynamic it leads to
  says how far it goes.
- A staff with hairpins is taller by its dynamics lane, so lines and strips
  may break differently; the render snapshots of the corpus change.
- A wedge that should stop in the middle of a long note cannot: it ends with
  the note. Guitar Pro has the same limit.
- Tests: `io.Guitar Pro: a guitar keeps ...` (a crescendo over two beats, a
  diminuendo written as a decrescendo), the native round trip and text forms,
  `io.MusicXML: wedges become hairpins ...` (across a barline, stopped at the
  end of the song), the MusicXML round trip of a song with every feature
  (a wedge over a barline, one on the upper staff of a piano),
  `core.A hairpin ramps the velocity of the notes under it` (to the forte
  after a crescendo, over two voices; one step down without a softer dynamic),
  `cli.sequence_hairpins`, `notation.Hairpins: ...` (a wedge clear of the
  dynamics, a diminuendo over a barline, cut by the end of a line, under
  tablature alone), `cli.render_hairpins`, `cli.edit_sets_hairpins` with
  `cli.dump_hairpins` and `cli.edit_rejects_unknown_hairpin`, and the app
  smoke test's diminuendo.
