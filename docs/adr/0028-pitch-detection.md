# 0028. Pitch detection for the tuner

- Status: Accepted
- Date: 2026-11-23

## Context

Milestone 6 needs a tuner: play a string, see the note and how far it is from
in tune. The part that listens to the microphone needs the system audio input
(through JUCE, ADR-0002), but the part that turns samples into a pitch is pure
signal processing that can live in `elara_audio` and be tested without a sound
card (ADR-0003).

A tuner for guitars and basses has to hear from a five-string bass's low B
(30.9 Hz) to past a guitar's 24th fret (1319 Hz), to a fraction of a cent on a
steady note, and must not jump an octave on a plucked string, whose second
harmonic is often louder than its fundamental. It must also say nothing when
nothing clear is played, rather than show noise as a note.

Common methods:

- **Peak of the spectrum (FFT).** Cheap, but at 48 kHz a bin of a 4096-point
  transform is 11.7 Hz wide, a semitone at 200 Hz, and the loudest peak is
  often a harmonic.
- **Autocorrelation.** Finds the period, but its peaks at zero lag and at
  multiples of the period make octave errors common.
- **YIN** (de Cheveigné and Kawahara, 2002): the squared difference of the
  signal with itself at each lag, divided by its running mean, so that the
  first lag that dips under an absolute threshold is the period. It is
  published, widely reimplemented and accurate on plucked strings.

## Decision

**YIN in `elara_audio`.** `PitchDetector` implements the published method's
steps: the difference function, its cumulative mean normalisation, the
absolute threshold (0.15), following the first dip under it to its bottom,
and parabolic interpolation of that lag on the raw difference function. It
returns a `PitchEstimate` (frequency and clarity, 1 minus the normalised
difference at the period) or nothing.

**Range and window.** The range is 30 to 1600 Hz by default. The window is the
longest period, so `detect()` looks at two longest periods and one sample
(3201 samples, 67 ms, at 48 kHz). The constructor sizes every buffer, so
`detect()` never allocates, locks or throws and can run on an audio input
callback. Below a mean square of 10⁻⁸ (-80 dBFS) the frame counts as silent.

**Readings.** `tunerReading()` turns a frequency into the nearest
equal-tempered note (spelled with sharps), its distance in cents and, given a
tuning, the nearest open string and its distance. A4 is 440 Hz unless given.

**A recording can be tuned from the command line.** `elara-cli tune FILE.wav
[--a4 440] [--tuning guitar]` reads a WAV file (`readWav()`: PCM of 8, 16, 24
and 32 bits and 32 or 64-bit float, plain or extensible, any channels mixed to
mono), runs the detector on windows half a window apart and reports the median
pitch of the windows where it heard one, with their mean clarity.

**Microphone input is later.** Opening the input device belongs in
`elara_audio_juce`, next to output; the app's tuner panel will feed its
callback's samples to a `PitchDetector`.

## Consequences

- Tests check sine waves from 31 Hz to 1319 Hz at 44.1 and 48 kHz to within a
  cent, a sawtooth and a tone whose second harmonic is louder than its
  fundamental, the built-in synthesizer's guitar and bass strings to within
  5 cents, and silence, noise, a faint note and too short a frame, which give
  nothing.
- A frequency above the range is reported as one of its subharmonics, as YIN
  only looks at lags of the range; the range covers the instruments Elara
  writes for, and a caller can widen it.
- The difference function costs one multiply-add per window sample and lag,
  and lags past the bottom of the first dip are not computed: a 48 kHz window
  takes 0.39 ms for a guitar's low E and 1.07 ms for noise, where every lag is
  computed (`docs/performance.md`), so a tuner looking twice per window uses at
  most 3 % of one core. Computing the difference function through a Fourier
  transform would be cheaper if that ever mattered.
