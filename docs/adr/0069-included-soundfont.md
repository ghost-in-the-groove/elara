# 0069. MuseScore General comes with the packages

- Status: Accepted
- Date: 2026-11-25

## Context

The scope (`docs/SCOPE.md`) plans FluidSynth with MuseScore General SF3 for
every instrument. The app plays any SoundFont the user chooses (ADR-0043), but
a new install plays through the built-in synthesizer until someone finds,
downloads and chooses one: the first song a user opens sounds like a test
tone generator.

MuseScore General 0.2 is a General MIDI SoundFont of 309 presets, MIT licensed
(FluidR3 by Frank Wen, FluidR3Mono by Michael Cowgill, adapted by
S. Christian Collins). Its SF3 file is 40 MB. FluidSynth loads samples only as
presets play (`synth.dynamic-sample-loading`), on a worker away from the
interface (ADR-0043), so the memory it takes is that of the instruments a song
uses. A debug build opens it and plays band.gp's 7 s in under 4 s.

## Decision

**The packages carry MuseScore General, and the app plays through it until
another sound is chosen.**

- `elara.py package` downloads `MuseScore_General.sf3` and its licence from
  MuseScore's mirror into `build/downloads/soundfont` once, checks both
  against pinned SHA-256 digests, and configures the release build with
  `ELARA_SOUNDFONT_DIR`. The repository holds neither file; CI keeps the folder
  in its cache. `--no-soundfont` packs without it, with no download.
- CMake installs that folder where the app looks for it:
  `share/elara/soundfonts` next to `bin` (Windows, Linux, the AppImage) and
  `Resources/soundfonts` in the macOS bundle. The app takes the first SF2 or
  SF3 file there (`includedSoundFont()`).
- At start the app opens the SoundFont saved last time; when the user never
  chose a sound, or the file chosen is gone, it opens the included one
  (`startingSoundFont()`). Choosing the built-in synthesizer is remembered as
  a choice, so it stays. "Use the included SoundFont" in the menu brings it
  back.
- `elara --included-soundfont` plays through it, or fails with a message when
  the build has none. The package smoke test plays a song through it from the
  installed tree, and the MSI and the AppImage through theirs.

## Consequences

- A new install plays every instrument with sampled sounds, with no setup.
- Packages grow by 40 MB. Development builds have no SoundFont unless one is
  chosen, so the tests never depend on the download:
  `app.smoke_included_soundfont` puts the test SoundFont where a package would.
- Packaging needs the network the first time, or `--no-soundfont`.
- A guitar SFZ library is still the user's to choose (ADR-0066): the free
  ones are hundreds of megabytes.
