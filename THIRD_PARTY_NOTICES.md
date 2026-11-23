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
| [Bravura](https://github.com/steinbergmedia/bravura) | 1.482 | SIL OFL 1.1 | Music notation font and its SMuFL metadata (`elara_notation`, ADR-0011) | Three pinned files + SHA-256, downloaded at configure time into `bin/fonts/` with the licence (`OFL.txt`) |

## Planned (licence checked, not yet integrated)

| Component | Licence | Planned use |
|---|---|---|
| FluidSynth | LGPL-2.1-or-later | SoundFont playback (milestone 4) |
| sfizz | BSD-2-Clause | SFZ sampler for guitar articulations (milestone 4) |
| Steinberg VST3 SDK | MIT | Plugin hosting (after 1.0) |
| MuseScore General SF3 | MIT | Default sound bank (milestone 4) |
