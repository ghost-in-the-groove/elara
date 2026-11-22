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

## Planned (licence checked, not yet integrated)

| Component | Licence | Planned use |
|---|---|---|
| FluidSynth | LGPL-2.1-or-later | SoundFont playback (milestone 4) |
| sfizz | BSD-2-Clause | SFZ sampler for guitar articulations (milestone 4) |
| Steinberg VST3 SDK | MIT | Plugin hosting (after 1.0) |
| Bravura (SMuFL) | SIL OFL 1.1 | Music notation font (milestone 3) |
| MuseScore General SF3 | MIT | Default sound bank (milestone 4) |
