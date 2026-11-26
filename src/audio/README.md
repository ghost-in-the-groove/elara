# elara_audio / elara_audio_juce / elara_audio_fluid

Three targets with different rules:

| Target | Files | May depend on | Must not include |
|---|---|---|---|
| `elara_audio` | `include/`, `src/` | std, `elara_core` | Qt, JUCE |
| `elara_audio_juce` | `backend_juce/` | std, `elara_core`, `elara_audio`, JUCE | Qt; JUCE types in its public header |
| `elara_audio_fluid` | `backend_fluid/` | std, `elara_core`, `elara_audio`, FluidSynth | Qt, JUCE; FluidSynth types in its public header |

`elara_audio` holds code that is easy to test without a sound card:

- the sequencer (`Sequence`: a song's measures in play order, its notes and
  note events in seconds, kept by track as playback reads them, ADR-0017; gradual tempo changes speed up or slow
  down along the score, ADR-0082, fermatas hold every voice, ADR-0085, and a
  triplet feel plays pairs long and short, ADR-0104), with accents, ghost notes, staccato, let
  ring and harmonics applied (ADR-0022), portato notes three quarters long (ADR-0088), staccatissimo ones a quarter (ADR-0094), a sixteenth or an eighth taken off the notes before a breath mark or a caesura (ADR-0095), bends, slides and vibrato sent as
  pitch events (ADR-0023), hammer-ons, pull-offs and legato slides as legato
  events (ADR-0070), and strums and tremolo picking
  spreading and repeating attacks (ADR-0025), trills repeating them on the
  note and the other note by turns (ADR-0090), and mordents and turns
  starting their notes with quick notes on the steps of the key (ADR-0091),
  and the sustain pedal holding every note of its track on until it comes up
  (ADR-0092). For practice it plays at 25 % to
  200 % of the written tempo and adds a metronome on a track after the song's
  (`SequenceOptions`, ADR-0024), which `OfflineRenderer` and `Player` play on
  wood blocks at a fixed level. `carriedPosition()` finds where playback
  goes on in a new sequence: the same place in the music after another speed
  or a tempo edit;
- the built-in synthesizer (`Synth`: one track in one `Timbre`, picked from the
  instrument by `timbreFor()`; plucked timbres also play each note's `Touch`:
  palm mute, dead note or harmonic, ADR-0022; `glide()` moves a sounding
  voice's pitch, ADR-0023) and `OfflineRenderer`, which plays a sequence
  through one sound per track into stereo, with an event log (ADR-0018). Each
  track sits in the stereo mix at its volume and pan, and mute and solo
  silence tracks (ADR-0020);
- sound banks (ADR-0043): a `SoundBank` gives each track a `TrackSound`, the
  real-time-safe interface the renderer and the player strike notes on.
  `builtinBank()` wraps the built-in `Synth`; `RenderOptions::bank` and
  `Player::setSoundBank()` choose another, such as a SoundFont. The player
  keeps a track's sound across loads while its instrument stays the same,
  and across banks that make it the same (`SoundBank::source()`, ADR-0120);
- SFZ instruments (ADR-0066): `parseSfz()`/`readSfz()` read the text format
  into regions (`SfzProgram`, `SfzRegion`), listing the opcodes not played,
  and `SfzBank` plays them through a sampler of Elara's own: WAV and FLAC
  samples read into memory on every core, each averaged to one channel in
  place by the task that read it (`WavAudio::intoMono()`), so that loading
  holds the final samples and only the files in flight, regions picked by
  key, velocity,
  controllers, keyswitch and round robin (in turn, or at random with
  `lorand`/`hirand` from a generator of each sound's own, the same sequence
  every time), the amplitude envelope, loops,
  release samples and exclusive groups, 64 voices per track, each note glided
  on its own.
  An instrument plays every pitched track, or only the guitars or the basses
  (`SfzTracks`); the other tracks play on another bank, and `with()` puts the
  same samples in front of another one. Palm mutes, dead notes and harmonics
  play on the library's own keyswitch or controller for them, found by name
  (`techniques()`, ADR-0068), or else are imitated. Hammer-ons and pull-offs
  play a keyswitch named for legato or the `trigger=legato` regions, and
  glide when the library has neither (ADR-0070); a fretted track's picked
  notes are never legato;
- `Player`, the same in real time: songs are handed to the audio thread through
  atomic pointers and the transport is a set of atomics (ADR-0019); a song can
  be prepared on another thread (`preparer()`, `PreparedSong`) and published
  on the control thread (`publish()`, ADR-0112); and
  `updateMix()` changes the track levels while it plays, from a song or from
  the tracks' mixes alone (a moving fader, ADR-0039). For practice it counts
  in before it starts (`play(CountIn)`, from `Sequence::countInAt()`) and
  loops a passage (`setLoop()`, ADR-0024). A player made at a device's new
  sample rate goes on from another's place (`takeOver()`: position, loop,
  playing or paused). Paused with no voice sounding, it fills its blocks
  with silence without running any track's sound. It is an `AudioSource`,
  the interface a device plays;
- the tone generator, fade envelope and the WAV writers (whole buffers, or
  block by block with `WavFileWriter`), `readWav()`/`decodeWav()` for
  PCM and float WAV files, `readFlac()`/`decodeFlac()` for FLAC files, decoded
  by Elara from RFC 9639 (ADR-0067), and `readAudioFile()` for either;
- the tuner's `PitchDetector` (YIN, ADR-0028) and `tunerReading()`, which names
  the nearest note, its cents and the nearest open string; `Tuner`, which
  turns a stream of samples into steady readings, and `SampleRing`, the
  lock-free queue the audio input fills (ADR-0029).

`elara_audio_juce` opens the
system audio device through JUCE (ADR-0002) and exposes a plain C++ API
(`elara::audio::SystemAudio`, `listOutputDevices()`); `SystemAudio::setSource()`
plays an `AudioSource` such as a `Player`. `MicrophoneInput` (and
`listInputDevices()`) listens to the default input on a device of its own and
queues the loudest channel's samples in a `SampleRing` (ADR-0029).

`elara_audio_fluid` reads SoundFonts (SF2, and SF3 with compressed samples)
through FluidSynth (ADR-0043): `SoundFont::open()` lists the presets and
`presetFor()` picks each instrument's General MIDI preset. A track's sound is
a FluidSynth synth of its own with the track's presets only, sixteen MIDI
channels so each note takes one (a glide bends only its key), and a guitar's
muted and harmonics presets for palm mutes, dead notes and harmonics.
FluidSynth comes from the overlay port in `cmake/vcpkg-ports/fluidsynth`,
built without audio drivers.

## Real-time rule

Code that runs inside the audio callback (`ToneGenerator`, `fadeGain`,
`Synth` and every `TrackSound` after construction, `Player::render()`,
`PitchDetector::detect()`,
`SampleRing::write()`, the callbacks in
`system_audio.cpp` and `microphone_input.cpp`) must not allocate, lock, log, throw or do I/O.
Exchange data with the audio thread through atomics or lock-free queues only.

## Verifying without listening

`elara-cli render-tone --json --out tone.wav` renders the same DSP offline and
reports frames and peak level; `elara-cli sequence FILE --json --events` prints
what the sequencer would play; `elara-cli render-audio FILE --json --events
[--out song.wav]` plays it through the built-in synthesizer (or a SoundFont
with `--soundfont FILE.sf2`) and prints the level, each track's timbre and
sound and every event with the frame it was dispatched at. `elara-cli
soundfont FILE.sf2 --json` lists a SoundFont's presets. `elara-cli play FILE` plays it on the default device. All three take
`--speed PERCENT` and `--metronome`; `sequence --count-in-at SECONDS` prints a
count-in, and `play` takes `--count-in` and `--loop FROM TO`. `elara-cli tune
FILE.wav --json [--tuning guitar]` reports the pitch a tuner would show for a
recording. Tests in
`tests/audio/` check pitch, glides, envelopes, touches, voice stealing, event frames and the
player's transport by calling `render()` as an audio callback would; tests in
`tests/cli/` run the commands on corpus files. SoundFont tests write a tiny
SoundFont of their own (`tests/audio/test_soundfont.hpp`): one sine wave
that each preset plays from a different root key, so the pitch shows which
preset sounded.
