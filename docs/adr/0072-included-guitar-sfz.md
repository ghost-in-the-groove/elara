# 0072. A clean electric guitar SFZ comes with the packages

- Status: Accepted
- Date: 2026-11-25

## Context

The guitars play SFZ instruments through Elara's own sampler (ADR-0066), which
bends each note on its own and maps palm mutes, dead notes, harmonics and
legato to a library's samples (ADR-0068, ADR-0070). But a new install has no
SFZ instrument: the user must find one with a licence they can use, download
it and choose it. The free guitar libraries with articulations are hundreds of
megabytes (Karoryfer's Black And Green Guitars: 459 MB zipped), too large to
carry in every package.

FreePats' FSBS clean electric guitar is CC0. Its small bridge pickup set is
one sustained sample every three or four semitones, keys 35 to 86, in 13 FLAC
files of 2.9 MB. The sampler reads it with no unknown opcode but `width`, in
49 ms, and holds 21.7 MB of samples; a D3 rendered on it tunes as D3.

## Decision

**The packages carry FreePats' clean electric guitar, small bridge set, as an
SFZ instrument the guitars can play; MuseScore General stays the starting
sound (ADR-0069).**

- `elara.py package` downloads the instrument, its licence and its 13 samples
  file by file from the library's repository at a pinned commit into
  `build/downloads/guitar-sfz`, checks each against a pinned SHA-256 digest,
  and configures the release build with `ELARA_GUITAR_SFZ_DIR`. The SFZ file
  is saved as `FreePats Clean Guitar.sfz`, the name the app shows. The
  repository holds none of the files; CI keeps the folder in its cache.
  `--no-guitar-sfz` packs without it, with no download.
- CMake installs that folder as `share/elara/sfz/freepats-clean-guitar` next to
  `bin` (Windows, Linux, the AppImage) and `Resources/sfz/freepats-clean-guitar`
  in the macOS bundle. The app takes the first SFZ file of the first folder
  there (`includedGuitarSfz()`), so each instrument keeps its samples in a
  folder of its own.
- "Use the included guitar" in the menu plays the guitars through it, kept for
  the next start like any SFZ instrument chosen. `elara --included-guitar-sfz`
  does the same from the command line, or fails with a message when the build
  has none. The package smoke test plays a guitar song through it from the
  installed tree.

## Consequences

- Any install can play the guitars on a sampled instrument with bends, slides
  and vibrato note by note, with no download.
- The instrument is not the default: one velocity layer and no articulation
  samples of its own, against MuseScore General's guitars, which a new user
  hears first. Palm mutes, dead notes and harmonics on it fall back to the
  sampler's own simulation (ADR-0068).
- Packages grow by 3 MB. Development builds have no included instrument, so
  the tests never depend on the download: `app.smoke_included_guitar_sfz` puts
  the test instruments where a package would.
- A larger library with articulations (Karoryfer's, trimmed to FLAC) could
  come later as an optional download.
