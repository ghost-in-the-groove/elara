# 0070. Hammer-ons and pull-offs play a library's legato samples

- Status: Accepted
- Date: 2026-11-25

## Context

A hammer-on, a pull-off or a legato slide sounds a new note on a string
that is not picked again. The sequencer sends it as a glide of the voice
already sounding (ADR-0023): the built-in synthesizer and FluidSynth move
that voice to the new pitch, which keeps the string ringing as a real one
does.

A sampled guitar does worse that way. Its sample was recorded at one fret
and stretched to another it sounds thin, and the pick attack heard once
belongs to the first note only. Guitar libraries record the joined notes
too, and reach them in one of two ways:

- regions with `trigger=legato`, which the SFZ format plays for a key going
  down while another is held, as a keyboard player plays legato;
- a keyswitch for a group of hammer-on and pull-off samples, named for the
  player by `sw_label` ("Legato", "Hammer-ons", "HO/PO slur").

The sampler cannot tell a joined note from a bend: both arrive as glides.
And the keyboard rule misfires on a guitar: every string of a chord is held
while the next is picked, so a strummed chord would play legato samples on
all its strings but the first.

## Decision

**The sequencer tells a joined note apart, and the sampler plays it on the
library's legato samples when it has them.**

- `EventKind::Legato`: a note joined to the voice struck as `key`, `cents`
  above it, with the joined note's velocity. The sequencer sends it where it
  sent a glide at once before: for every note that is not struck. At one time
  it comes after the attacks and before the pitch changes.
- `TrackSound::legato(key, cents, velocity)`. Sounds with no samples for it
  (the built-in synthesizer, FluidSynth) glide there at once, as before.
- `SfzBank` finds a keyswitch whose label holds "legato", "hammer", "pull" or
  "slur" (`techniques().legato`, as ADR-0068 finds the touches). A joined
  note plays that keyswitch's regions on its own key, for that note alone;
  without such a keyswitch, or when it has no region for the key, the
  `trigger=legato` regions of the key, played as the picked note was; and
  when neither has a region, the voice glides as before.
- When the library has regions for it, the picked note fades out at once
  (the 6 ms of a choked group) and the joined note starts in its place. It
  keeps answering to the picked key: later bends and vibrato are reckoned
  from that key, and the key's release ends it.
- On a fretted track every picked note is a first note: chords included, a
  key going down never plays `trigger=legato` regions, and `trigger=first`
  regions always play. Other tracks keep the keyboard rule.
- `elara-cli sequence --events` prints `"kind": "legato"` with `cents` and
  `velocity`; `elara-cli sfz` reports `"legato"` among the techniques.

## Consequences

- A library's recorded hammer-ons and pull-offs play with no setup; a library
  without them, and every other sound, plays joined notes as before.
- Release regions of a joined note still play at the picked key, as they did
  for a glide.
- A library that uses `trigger=legato` on a fretted track for something
  other than joined notes loses it; none we know of does.
- Tests: the sequencer sends Legato events for hammer-ons, pull-offs and
  legato slides; the sampler plays a keyswitch named for legato, then
  `trigger=legato` regions, or glides, and a bend after the joined note moves
  it; a fretted chord plays no legato regions; and through the CLI a hammered
  E3 on the test library's legato keyswitch, an octave up, tunes as an E4.
