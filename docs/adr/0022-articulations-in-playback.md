# 0022. Articulations in playback

- Status: Accepted
- Date: 2026-11-23

## Context

The model keeps how every note is played (`core::NoteEffects`): accents, ghost
notes, staccato, let ring, palm mutes, dead notes, harmonics, bends, slides,
hammer-ons and vibrato. Until now the sequencer read only the pitch, the
length and the dynamic of each note (`core::soundingNotes()`), so a palm-muted
riff, a dead-note scratch and a harmonic all played as plain notes.

Articulations change four things, and each belongs to a different stage:

- **when a note sounds and how loud** (staccato, let ring, accents): the
  sequencer can decide this from the score alone;
- **which pitch sounds** (a harmonic's partial): also the sequencer;
- **how the string is struck** (palm mute, dead note, harmonic): only the
  synthesizer can make a string sound damped or pure;
- **how the pitch moves while the note sounds** (bends, slides, hammer-ons,
  vibrato): the synthesizer has to retune a voice that is already sounding.

The last group needs new kinds of events and a synth that can glide a voice's
pitch. It is a separate change; this record covers the first three and leaves
room for it.

## Decision

**The sequencer finds each note's effects.** `core::SoundingNote` says where
its attack was written: measure, beat and note. The sequencer reads the
effects there and applies them to the `PlayedNote` and its events, so the
model still has one place for articulations and `soundingNotes()` stays the
plain score that MIDI export writes. A tied note takes the effects of its
attack.

**Velocity, by dynamics.** One dynamic is 16 velocity steps (mf 80, f 96). An
accent adds one dynamic, a marcato two, and a ghost note takes two away. The
result stays within 1 to 127.

**Length.** A staccato note sounds half its written length. A let-ring note
sounds until its string is struck again on its staff, in any voice, or to the
end of the performance; a note without a fret position rings until its key is
struck again. Let ring never makes a note shorter than written.

**Pitch.** A natural harmonic sounds the partial of its open string whose node
is at the touched fret (`core::naturalHarmonicSemitones()`): at fret 7 the open
string plus 19 semitones. Artificial, pinch and tapped harmonics sound that
partial of the fretted note. A semi-harmonic, and a node that is not one, keep
the fretted pitch.

**Touch.** Each note and each note-on event carries an `audio::Touch`: normal,
palm mute, dead or harmonic. A dead note is dead whatever else is marked on
it, and a harmonic wins over a palm mute. Only plucked timbres (guitar,
overdrive, bass) play touches; the others play every touch as normal:

| Touch | Plucked string |
|---|---|
| Palm mute | Rings at most 0.3 s (0.5 s on a bass) to -60 dB, from a darker excitation, a little softer |
| Dead | A bright click that falls 60 dB in 0.04 s, softer |
| Harmonic | One period of a sine instead of noise in the string, so only the fundamental of the sounding key rings, 1.5 times longer |

**Drums.** Accents, ghost notes and staccato apply to percussion tracks.
Harmonics and touches do not: their keys are drums, not pitches.

`elara-cli sequence --notes --events` and `elara-cli render-audio --events`
print a `touch` field (JSON) or the touch's name (text) for every note and
event that is not struck normally.

## Consequences

- Corpus songs now play their palm mutes, dead notes, harmonics, accents,
  staccato and let ring. Event counts do not change: let ring moves note-offs
  later but adds none.
- The note events stay 32 bytes: the touch fits in the padding after the
  event's kind.
- Bends, slides, hammer-ons and pull-offs, vibrato and the whammy bar need
  pitch events in the synth: ADR-0023. A `Touch` stays how a key is struck;
  pitch changes are events of their own.
