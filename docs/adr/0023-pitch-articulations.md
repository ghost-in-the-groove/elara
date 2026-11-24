# 0023. Bends, slides and legato in playback

- Status: Accepted
- Date: 2026-11-23

## Context

ADR-0022 plays the articulations that decide how loud a note is, how long it
lasts, which pitch it sounds and how its string is struck. The rest move the
pitch of a note that is already sounding:

- a **bend** follows a curve of cents over the note, and the **whammy bar**
  does the same for every note of its beat;
- a **slide** glides into a note from below or above, off it downwards or
  upwards, or from it to the next note on its string, which is struck again
  (shift) or not (legato);
- a **hammer-on** or **pull-off** sounds the next note on the same string
  without picking it;
- **vibrato** swings the pitch either side of the note.

A synthesizer that only knows note-on and note-off cannot play these: a new
note-on restarts the string, and nothing changes a voice once it sounds.

## Decision

**A third event.** `EventKind::Pitch` moves the voice struck as `key` to
`cents` above that key over `glide` seconds (at once for 0). Events at one
time come in the order releases, attacks, pitch changes, so a bend that starts
bent follows its attack at the same frame. The offset and the glide fit in
the padding after the event's kind, so `NoteEvent` stays 32 bytes.

**Notes joined by legato.** A note marked as a hammer-on or pull-off, or the
note after a legato slide, is joined to the previous note on its string when
that note is still sounding at its start: it is not struck (`PlayedNote::struck`
is false) and the voice of the first note of the chain moves to its pitch.
The chain is released when its last note ends. A legato note after a rest, or
on a string that has not sounded, is struck as usual.

**Every note's pitch is a line.** The sequencer adds up, for each note, the
offsets below, each a line through a few points over the note's written
length, so their sum is a line too, and sends one pitch event per segment:

| Offset | Line |
|---|---|
| Bend, whammy bar | The curve's points, at their percent of the note |
| Slide in | From 500 cents below (from below) or above, to the note in 0.1 s, or half the note if shorter |
| Slide out | Over the last 0.2 s (or half the note): 500 cents down or up, or to the next note on the string for shift and legato slides |
| Vibrato | A triangle wave at 5.5 Hz, 20 cents either side (slight) or 50 (wide) |

A joined note first jumps to its own pitch relative to the chain's first key,
then follows its line.

**The synth glides held voices.** `Synth::glide(key, cents, seconds)` moves
every voice still holding the key; a released voice or a new note is not
moved, and drums ignore it. While a voice glides it is retuned every 32 of its
own frames, so the result does not depend on how `render()` splits the
frames. Retuning sets each partial's rotation (piano, mallet, organ), the
sawtooth's step (bowed, wind) or the plucked string's loop delay. The string
now reads its delay line behind a write position that goes round the whole
line, so the delay can change while it rings; with a fixed delay it sounds as
before.

`elara-cli sequence` and `render-audio` print pitch events with their `cents`
and `glide`, and `"struck":false` for joined notes.

## Consequences

- Hammer-ons, pull-offs, bends, the whammy bar, slides and vibrato play in
  every song. Songs with them have more events: a bent note with vibrato has
  one per quarter of the vibrato's cycle.
- A hammer-on changes the string's pitch at once. A real string also loses a
  little energy and brightness there; this one does not.
- Tremolo picking and strums are not played yet. They strike notes again or
  spread a chord's attacks, which the sequencer can add without changing the
  synth (done in ADR-0025).
