# 0102. Measure repeats

- Status: Accepted
- Date: 2026-11-25

## Context

A measure repeat sign (simile, "%") tells the player to play the measure
before again; its two-measure form, drawn over the barline between two
measures, repeats the two measures before them. Drum parts and riff-based
guitar parts use them for long stretches, so that the eye catches where the
music changes. Elara dropped the sign with a warning ("repeat signs (simile)
dropped") and kept what the file holds in its measure, which is no music at
all: those measures stayed silent.

How the formats keep them:

- Guitar Pro 6-8 give a bar a `<SimileMark>`: `Simple` (the bar before),
  `FirstOfDouble` and `SecondOfDouble` (the two bars of a two-bar repeat).
  The bar holds a whole rest, which players do not play: they play the bars
  repeated. Guitar Pro 3-5 have no such mark.
- MusicXML writes `<measure-repeat type="start">` in a measure's
  `<measure-style>`, with the number of measures each repeat holds, until a
  `type="stop"`; the measures under the sign still carry the notes they
  repeat, written out.
- SMuFL draws them with `repeat1Bar` (U+E500), centred in its measure on the
  middle line, and `repeat2Bars` (U+E501), centred on the barline between the
  two measures.

## Decision

**A measure of a staff can repeat** the music before it:
`Measure::repeat`, a `MeasureRepeat` (`One`, or `TwoFirst` and `TwoSecond`
for the two measures of a two-measure repeat). `repeatSource(song, staff, m)`
says which measure measure m plays: itself, or the measure it repeats (one
measure back, or two for both measures of a two-measure repeat), followed
through repeats of repeats. The sign counts only on a measure with no beats of
its own, enough measures from the start and of the same length as the measure
it repeats; otherwise the measure plays and shows its own music, so music
written into the measure wins over the sign. Like clef changes (ADR-0087), the
sign lives in the measure, so inserting, deleting, copying and undoing
measures carry it with no more code.

**Heard** through the measure repeated: `soundingNotes()` plays its music in
the measure with the sign, so the sequencer, the timeline and MIDI export
hear it, and each note's measure stays the one it sounds in. `beatOf()` and
`noteOf()` find a sounding note's beat in the measure written, so
articulations follow; the pedal of the measure repeated holds too. Editing
the measure repeated changes what every repeat of it plays.

**Drawn** as SMuFL says, on standard notation and on tablature: a
one-measure sign in the middle of its measure on the middle line, instead of
a measure rest; a two-measure sign centred on the barline between its
measures, or in the middle of each when a line ends between them. A measure
with a sign is at least as wide as the sign.

**Kept** by the native format as a staff's `"repeats"`: a list of the measures
with a sign and its kind (`"one"`, `"two-first"`, `"two-second"`), since a
measure with a sign holds no beats and trailing empty measures are not
written. Guitar Pro 6-8 simile marks become signs, dropping the whole rest of
the bar; a mark Elara does not know keeps the bar's beats, with a warning
("simile marks dropped").

**MusicXML** reads a `<measure-repeat type="start">` of one or two measures
as signs on the staff its `<measure-style>` numbers (or on all of them), from
that measure until a `type="stop"`, and drops the notes written out under
them: the sign plays the measures before. Longer repeats warn and stay as
written. Writing does the reverse: each run of signs that count starts with
its size and stops in the first measure after it, numbered by staff when the
part has more than one (a TAB staff written from its staff shares its
style), and each measure with a sign writes out the music it plays, so that
readers that ignore the sign still play it. A sign that does not count is
dropped with a warning, since MusicXML has no sign without its music.

## Consequences

- Imported drum and riff parts sound as written, and their pages show the
  signs the file has instead of measure rests.
- **Set at the caret** (`setMeasureRepeat()`, the CLI's `simile` and the
  measure popup): a sign of one measure, or of two from the caret's measure,
  drops the music of the measures it goes on, as one undo step; taking it away
  writes out the music it played, so that what is heard stays and can be
  edited from there. A two-measure repeat that loses one of its measures to a
  new sign is written out the same way. A sign that would not count (too few
  measures before, another length) is not set.
- A measure whose sign does not count (nothing before it, another length)
  shows a measure rest and keeps the sign, which counts again when the
  measures around it allow.
