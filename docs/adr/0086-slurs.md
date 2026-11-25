# 0086. Slurs

- Status: Accepted
- Date: 2026-11-25

## Context

A slur is a curve over notes played smoothly, as one phrase: on a wind or a
voice in one breath, on a string in one bow, on a keyboard joined. It is the
most frequent mark of standard notation that Elara still dropped: every
MusicXML file with slurs warned "slurs dropped", and the notes stood loose.

How the formats keep them:

- MusicXML writes `<slur type="start">` on a note's `<notations>` and
  `<slur type="stop">` on a later one, matched by `number` (1 to 16) within
  the part, `continue` for the bends of a long one. A note can stop one slur
  and start the next, slurs can nest (a phrase over shorter slurs), start or
  stop on grace notes, and join notes of different voices or staves.
- Guitar Pro has no slurs of its own: its arcs are hammer-ons, pull-offs and
  legato slides, which Elara keeps as note effects (ADR-0070). MIDI has none.

Engraving manuals put a slur on the notehead side when all its stems go one
way, above when they differ (from the tip of a stem going up), above for the
upper voice and below for the lower when two share a staff; its ends by the
noteheads, its arch clear of the notes between, flatter as it gets longer, and
cut where a line ends, going on from the start of the next.

## Decision

**A beat is slurred to the next.** `BeatEffects::slur` says that a beat is
slurred to the next beat of its voice; a slur runs from the first of the beats
slurred in a row to the beat after the last, across barlines. The model needs
no matching of starts and stops: inserting, deleting or pasting beats keeps
every slur whole, as ties are kept (`NoteEffects::tiedFromPrevious`). Two slurs
in a row are two runs; what one bit cannot hold is a note that ends one slur
and starts the next, which becomes one slur over both, and slurs inside
another, which add nothing to it. A rest can be slurred over (with a hairpin
and a fermata, the only effects a rest takes). Slurs do not change playback:
notes already sound their whole length, and hammer-ons stay their own effect.

**MusicXML.** Starts and stops are matched by number within the part, a stop
before a start on the same note; the beats from a slur's start to the beat
before its stop are slurred, a note of a chord counting as its beat and a
grace note as the beat it leads into. Dropped with a warning: a slur that
joins two voices or staves ("slurs between voices dropped"), one from a grace
note to its own beat ("slurs on grace notes dropped") and one never stopped
("slurs without an end dropped"). Written back, a slur starts on the first
note of its first beat and stops on the beat after its last, numbered by
staff and voice (1 to 16, as voices are numbered); a beat slurred to nothing
after it, at the end of the song or of its voice, starts none.

**The native format** writes `"slur":true` in a beat's effects, and nothing
for false.

**Drawing.** Standard notation draws a slur from the middle of its first
beat's noteheads to its last beat's, by the outer notehead (and the staccato or
accent on its side), or from the tip of the stem on the slur's side. It goes on
the notehead side when every stem under it goes up, above otherwise, and above
for the upper voice and below for the lower ones of several. Its height grows
with its length (0.08 of it, from half a space to a space and a half) and grows
more to clear the ink of the beats between its ends by 0.4 of a space; past 2.5
spaces, the whole slur moves away instead. A slur that goes on into the next
line ends at the edge of the line, and the next line's goes on from its start.
A `slur` role marks them in the display list, as it marks hammer-on arcs on
tablature. Tablature does not draw slurs: its arcs are the hammer-ons and
pull-offs, which standard notation slurs as well (ADR-0097).

**Editing.** The app's entry bar has a slur button next to the hairpins that
slurs the caret's beat to the next one, lit while it is; the caret's status
names it. `elara-cli edit` has `slur <on|none>`. Both are one undo step
("Change beat effects").

## Consequences

- Slurs come through MusicXML both ways and the native format and show in
  `elara-cli import --json` and `dump`.
- Slurs draw in every layout of standard notation (lines, pages, strips, SVG,
  PDF and the app).
- A note that ends one slur and starts another comes back as one slur, nested
  slurs as the outer one, and cross-voice, cross-staff and grace-note slurs are
  dropped with a warning.
- Slurs are not drawn on tablature shown alone, and do not change playback.
- Tests: `core.Effects: strums, pick strokes and tremolo picking on the caret's
  beat` (a rest takes a slur), `io.MusicXML: slurs join the beats from their
  start to their stop`, `io.MusicXML: slurs from grace notes, between voices or
  without an end are dropped`, `io.MusicXML export: slurs come back on their
  voices`, the native round trip in `io.song.json uses text forms and omits
  defaults`, the `notation.Slurs: ...` layout cases, `cli.edit_sets_slurs`,
  `cli.dump_slurs`, `cli.edit_rejects_unknown_slur`, the slur button in
  `app.smoke` and the import and render snapshots of
  `tests/corpus/musicxml/slurs.musicxml`.
