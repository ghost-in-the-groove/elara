# 0043. SoundFont playback

- Status: Accepted
- Date: 2026-11-23

## Context

The built-in synthesizer (ADR-0018) plays every track from a handful of
timbres: good enough to check what was written, far from what a guitarist,
a bassist or a drummer expects to hear. SoundFonts are the open format for
sampled instruments: General MIDI fonts (SF2, or SF3 with compressed samples)
hold a sound for every program and a drum kit, under free licences.
FluidSynth (LGPL-2.1) is the reference SoundFont synthesizer and a library.

## Decision

**A seam in `elara_audio`.** A `SoundBank` makes a `TrackSound` for a track's
instrument; the `TrackSound` has the calls of `Synth` (strike, release, glide,
release all, reset, render, active voices) and the same real-time rule. The
built-in synthesizer is one bank (`builtinBank()`); the offline renderer
(`RenderOptions::bank`) and the player (`Player::setSoundBank()`) play every
track through the bank they are given. The metronome and the count-in keep
the built-in clicks, so practice sounds the same whatever the bank.

**A backend for SoundFonts.** `elara_audio_fluid` (`src/audio/backend_fluid`)
is the only target that sees FluidSynth, behind a plain C++ header.
`SoundFont::open()` checks the file and reads its presets.
`presetFor(instrument)` picks the instrument's General MIDI program in bank 0
(the kit of bank 128 for percussion), or else the same program in another
bank, the first program of its family of eight, or the first program.

**One synth per track.** Each track's sound is a FluidSynth synth of its own,
loaded with dynamic sample loading so it reads only the samples of its
presets. Tracks then render on their own threads as before, a track only
pays for what it plays, and the mix (volume, pan, mute, solo) is unchanged.
Reverb and chorus are off: an effect per track would cost a lot, and effects
belong on a master bus later.

**Glides and touches.** A synth has sixteen MIDI channels on the track's
presets; each note takes the channel free the longest, so a pitch bend (24
semitones of wheel) moves only the notes of its key, as the built-in
synthesizer's glides do. A glide over time moves the wheel at the start of
each of FluidSynth's 64-frame blocks, so the output does not depend on how
`render()` is split. For guitars, three channels play General MIDI's muted
guitar (28) for palm mutes and dead notes (struck lighter) and three its
guitar harmonics (31), when the font has them. Drums ignore note offs and ring
out, like the built-in kit.

**Building it.** vcpkg's FluidSynth port compiles its own audio drivers and,
on Linux, a second copy of ALSA next to the system's. An overlay port in
`cmake/vcpkg-ports/fluidsynth`, based on vcpkg's, builds the same pinned
release with every driver off and libsndfile (without its MPEG codecs) for
SF3. FluidSynth's log goes to a fixed buffer that open errors quote, never to
the console.

**Headless.** `elara-cli soundfont FILE --json` lists the presets;
`render-audio` and `play` take `--soundfont FILE`, and `render-audio` prints
each track's sound. Tests write a tiny SoundFont of their own: one looped sine
wave that each preset plays from a different root key, so the pitch shows
which preset sounded.

## Consequences

- With a General MIDI font, songs sound like their instruments; without one,
  nothing changes.
- A track's sound takes a moment to make (its samples are read and, for SF3,
  decoded), so the player keeps it across loads while the track's instrument
  (kind and program) stays the same: an edit reloads no samples.
- FluidSynth applies a note at the start of its next 64-frame block: up to
  1.3 ms late at 48 kHz, the same in every render.
- FluidSynth and libsndfile are LGPL; Elara's source lets anyone relink them
  (THIRD_PARTY_NOTICES.md).
- The app opens the SoundFont chosen in its menu (or given with
  `--soundfont`) on a worker, plays the tracks through it from then on,
  playing or not, and keeps the choice for the next start; checks and
  screenshots leave that setting alone.
- Not yet: a default font to download, SFZ (sfizz) and a master reverb.
- Tests: `audio.Sound banks: ...`, `audio.Offline rendering: tracks play
  through the render's sound bank`, `audio.Playback: tracks play through the
  player's sound bank`, `audio.SoundFont: ...` (presets, errors, General MIDI
  choice, pitch and release, touches, glides, split rendering, drums), and
  `cli.soundfont*`, `cli.render_audio_soundfont`.
