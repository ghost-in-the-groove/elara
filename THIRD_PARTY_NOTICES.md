# Third-party notices

Every dependency must be compatible with Elara's licence (AGPL-3.0-or-later)
and be listed here before it is merged.

## In use

| Component | Version | Licence | Used for | How it is obtained |
|---|---|---|---|---|
| [JUCE](https://juce.com) | 9.0.6 | AGPL-3.0 (dual-licensed with a commercial licence) | Audio devices and DSP (`elara_audio_juce`) | FetchContent, pinned archive + SHA-256 |
| [Qt](https://www.qt.io) | 6.12.1 | LGPL-3.0 | User interface | Prebuilt kit (`scripts/install_qt.py`), linked dynamically |
| [CLI11](https://github.com/CLIUtils/CLI11) | vcpkg baseline | BSD-3-Clause | `elara-cli` argument parsing | vcpkg |
| [Catch2](https://github.com/catchorg/Catch2) | vcpkg baseline | BSL-1.0 | Tests only (not distributed) | vcpkg |
| [yyjson](https://github.com/ibireme/yyjson) | 0.13.0 | MIT | JSON in the native format (`elara_io`) | vcpkg |
| [miniz](https://github.com/richgel999/miniz) | 3.1.2 | MIT | Zip container of the native format and `.mxl` (`elara_io`) | vcpkg |
| [pugixml](https://github.com/zeux/pugixml) | 1.16 | MIT | XML parsing for MusicXML (`elara_io`) | vcpkg |
| [Bravura](https://github.com/steinbergmedia/bravura) | 1.482 | SIL OFL 1.1 | Music notation font and its SMuFL metadata (`elara_notation`, ADR-0011) | Three pinned files + SHA-256, downloaded at configure time into `bin/fonts/` with the licence (`OFL.txt`); the outline of its eighth rest is the r of the logo (`src/app/icons/elara-logo.svg`, `src/app/qml/Logo.qml`) |
| [Arimo](https://github.com/googlefonts/arimo) | Google Fonts commit `903d466` | SIL OFL 1.1 | Sans-serif text of scores in the app, metric-compatible with Arial and Helvetica (`elara`, ADR-0015) | Regular and italic, pinned files + SHA-256, downloaded at configure time into `bin/fonts/` and embedded in the app, with the licence (`Arimo-OFL.txt`) |
| [FluidSynth](https://www.fluidsynth.org) | 2.6.2 | LGPL-2.1-or-later | SoundFont playback (`elara_audio_fluid`, ADR-0043) | vcpkg, through the overlay port in `cmake/vcpkg-ports/fluidsynth` (based on vcpkg's port, MIT) that leaves out its audio drivers |
| [libsndfile](https://github.com/libsndfile/libsndfile) | 1.2.2 | LGPL-2.1-or-later | Reading SF3 SoundFonts (through FluidSynth) | vcpkg, without its MPEG codecs |
| [libogg](https://xiph.org/ogg/), [libvorbis](https://xiph.org/vorbis/) | 1.3.6, 1.3.7 | BSD-3-Clause | The compressed samples of SF3 SoundFonts (through libsndfile) | vcpkg |
| [libFLAC](https://xiph.org/flac/), [Opus](https://opus-codec.org) | 1.5.0, 1.6.1 | BSD-3-Clause | Linked by libsndfile; libFLAC also encodes the FLAC test files (ADR-0067) | vcpkg |
| [gcem](https://github.com/kthohr/gcem) | 1.18.0 | Apache-2.0 | Compile-time maths inside FluidSynth | vcpkg |
| [FreePats FSBS Electric Guitar Clean](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html) | 2026-08-07, small bridge set | CC0 1.0 (FreePats) | The guitar SFZ instrument the packages come with (ADR-0072) | Downloaded by `elara.py package` from the library's repository at a pinned commit, pinned SHA-256 for every file, and installed with its licence (`LICENSE.txt`) next to it, the instrument renamed `FreePats Clean Guitar.sfz`; not in the repository |
| [MuseScore General](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/) | 0.2 | MIT (FluidR3 by Frank Wen, FluidR3Mono by Michael Cowgill, adapted by S. Christian Collins) | The SoundFont the packages come with and the app plays through by default (ADR-0069) | Downloaded by `elara.py package` from MuseScore's mirror, pinned SHA-256, and installed with its licence (`MuseScore_General_License.md`) next to it; not in the repository |

FluidSynth and libsndfile are LGPL: Windows builds load them as DLLs, other
builds link them statically, and Elara's complete source (AGPL) lets anyone
relink it with a modified version, as LGPL-2.1 section 6 asks.

## Planned (licence checked, not yet integrated)

| Component | Licence | Planned use |
|---|---|---|
| Steinberg VST3 SDK | MIT | Plugin hosting (after 1.0) |
