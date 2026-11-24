# 0058. Kinds of note effects at the caret

- Status: Accepted
- Date: 2026-11-24

## Context

The note effects toggled at the caret (ADR-0037, ADR-0038) each go on in one
usual form: a shift slide, slight vibrato, an accent, a natural harmonic. The
model holds more (slides in from below or above and out downwards or upwards,
legato slides, wide vibrato, marcato, artificial, pinch, tapped and
semi-harmonics), the importers read them, playback plays harmonics of every
kind and the score draws them, but nothing could set them: a pinch harmonic or
a slide into a note could not be typed.

## Decision

**One edit per effect.** `setSlides()`, `setVibrato()`, `setAccent()` and
`setHarmonic()` set one effect of the notes the caret is on (as for
`toggleEffect()`: the caret's string, the lane's hit, or every note of the beat
without strings) to any of its kinds, or none, as one undo step ("Change
slide", "Change vibrato", "Change accent", "Change harmonic"). Unlike the beat
effects (ADR-0056), these do not take a whole `NoteEffects`: the notes of a
chord keep their other effects, which may differ. `noteEffectsAt()` reads the
effects of the caret's first note. Nothing changes when the notes have that
kind already, on a rest or on the free slot.

**Harmonics.** A natural harmonic sits at the note's fret and needs a node
there, as with the toggle. The other kinds sound an octave above the fretted
note (a node 12 frets up), which is how Guitar Pro writes them by default; a
harmonic needs strings.

**CLI.** `elara-cli edit` gains `slide-in <from-below|from-above|none>`,
`slide-out <shift|legato|downwards|upwards|none>`, `vibrato <slight|wide|none>`,
`accent <accent|marcato|none>` and `harmonic
<natural|artificial|pinch|tapped|semi|none>`, named as in the native format.

**App.** The effects panel gains a Slide row and a row for the harmonic's kind
(N.H., A.H., P.H., T.H., S.H., as the score labels them; only with strings),
the vibrato's width and the accent's strength. A button is lit while the
caret's notes have its kind, and a click on a lit one takes it off. The status
line names the kind ("Slide in from below", "Wide vibrato", "Marcato", "Pinch
harmonic"). The one-key toggles keep their usual forms.

## Consequences

- A slide into a note does not count as the Slide toggle, which stays the
  slide out of it.
- Harmonics of another node than the octave (a pinch harmonic at the fifth,
  say) still come only from files.
