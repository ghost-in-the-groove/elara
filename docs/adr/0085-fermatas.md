# 0085. Fermatas

- Status: Accepted
- Date: 2026-11-25

## Context

A fermata holds a note or a rest longer than written, at the player's
discretion, and every voice waits for it. Songbooks end songs and phrases on
them, and MusicXML files of choral and piano music carry many. Every importer
dropped them with a warning ("fermatas dropped"), and playback ran straight
through them, so the last chord of a song was cut short.

How the formats keep them:

- MusicXML writes `<fermata>` in a note's (or a rest's) `<notations>`, upright
  or inverted, its text naming the shape: `normal` (or empty), `angled`,
  `square`, `double-angled`, `double-square`, `double-dot`, `half-curve`,
  `curlew`. A `<fermata>` can also stand on a `<barline>`.
- Guitar Pro 6-8 keep a bar's fermatas on its master bar (`<Fermatas>`), each
  with a type (short, medium, long), an offset in the bar and a length. No
  file tried while testing had one, so how the offset counts is not known.
- Guitar Pro 3-5 and MIDI have no fermatas.
- Engraving manuals draw a fermata over the staff, centred on its note or
  rest, clear of stems; under the staff, inverted, for the lower voice when
  two share it. Its shape tells the length: angled for short, curved for
  normal, square for long.

## Decision

**A beat holds a fermata.** `BeatEffects::fermata` is a `Fermata`: `None`,
`Short`, `Normal` or `Long`. Unlike the other beat effects, a rest can hold one
(and a hairpin, ADR-0078): fermatas over rests are common at the ends of
phrases. `setBeatEffects` on a rest changes only those two.

**A fermata holds its beat by a stretch**: `fermataStretch()` gives 1.5 for a
short one, 2 for a normal one and 3 for a long one, figures in the middle of
what players do. A hold is song-wide: every voice waits, so
`Song::fermataHolds(measure)` gives the stretches of a measure, the union of
the beats with fermatas on every track, staff and voice, as spans from the
start of the measure (`FermataHold`), the longest stretch where two overlap.
Within a span time runs `stretch` times slower: `Song::secondsAt()` and the
sequencer's tempo map (ADR-0017) both stretch the written time there, under a
gradual tempo change too (ADR-0082), so a note under a fermata, and whatever
other voices play under it, sound longer, and the song lasts longer. Count-ins
keep the written tempo: they count the meter, not the held beat.

**MIDI export** writes a hold as tempo events: the tempo in force, slowed by
the stretch, at the start of each held span of every pass through the measure
in play order (ADR-0083), and the tempo in force again at its end (unless the
next span or a written tempo change starts there). Readers of the file hear
the hold; MIDI import reads the slower tempo as written.

**MusicXML.** A note's or rest's `<fermata>` is its beat's: `angled` and
`double-angled` are short, `square` and `double-square` long, every other shape
(and none) normal; a chord's notes give it the longest of theirs. A fermata
over a whole-measure rest makes the rest a beat, so it has somewhere to be.
Fermatas on barlines are still dropped with a warning: the model holds beats,
and a barline fermata holds no time of its own. Written back, a beat's fermata
is an upright `<fermata>` on its first note or its rest, `angled`, `normal` or
`square`.

**Guitar Pro 6-8** keep warning "fermatas dropped" until a file shows how the
offset counts; a fermata on the wrong beat holds the wrong note.

**The native format** writes `"fermata":"short"`, `"normal"` or `"long"` in a
beat's effects, on rests too, and nothing for none.

**Drawing.** A fermata is centred on its note (its notehead, or the middle of
a whole-measure rest) half a space above the staff and above everything of
the beat drawn by then: stems, beams, ledger lines and articulations. The
lower voice of two draws it inverted, half a space under the staff and the
beat. Short, normal and long fermatas use SMuFL's `fermataShort*`,
`fermata*` and `fermataLong*` glyphs. On a staff shown both ways the fermata
goes over the standard notation only; tablature shown alone draws it above
the tablature, over the beat's fret numbers. A `fermata` role marks them in
the display list.

**Editing.** The app's entry bar has a short, a normal and a long fermata
next to the hairpins, lit while the caret's beat holds one; the caret's
status names it. `elara-cli edit` has `fermata <short|normal|long|none>`.
Both are one undo step ("Change beat effects").

## Consequences

- Fermatas come through MusicXML both ways and the native format, show in
  `elara-cli import --json` and `dump`, and hold their beats in playback,
  offline rendering, MIDI export and the song's duration.
- A hold slows every track of the song; a fermata meant for one part only
  (rare outside cadenzas) holds the others too.
- Fermatas draw in every layout (lines, pages, strips, SVG, PDF and the app).
- Guitar Pro 6-8 fermatas and MusicXML barline fermatas are still dropped
  with a warning.
- Tests: `core.A fermata holds its beat longer`, `core.A fermata in a gradual
  tempo change holds what it takes there`, `core.Effects: strums, pick strokes and
  tremolo picking on the caret's beat` (a rest takes a fermata and a hairpin), `audio.Sequence: a fermata holds its
  beat and what sounds under it`, `io.MusicXML: fermatas hold their beats`,
  `io.MusicXML export: fermatas come back with their lengths`, `io.MIDI export
  plays what fermatas hold at a slower tempo`, the native round trip in
  `io.song.json uses text forms and omits defaults`, the `notation.Fermatas:
  ...` layout cases, `cli.edit_sets_fermatas`, `cli.dump_fermatas`,
  `cli.edit_rejects_unknown_fermata`, the fermata buttons in `app.smoke` and
  the import and render snapshots of `tests/corpus/musicxml/fermatas.musicxml`.
