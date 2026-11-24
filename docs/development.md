# Development setup

`python scripts/elara.py doctor` tells you what is missing. Versions in use:

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.28+ | presets v6 |
| Ninja | any recent | generator used by every preset |
| Compiler | MSVC 2022 (17.x), GCC 13+, Clang 17+ | C++23, no modules |
| Qt | 6.12.1 | `QT_ROOT_DIR` = kit folder containing `lib/cmake/Qt6` |
| vcpkg | baseline in `vcpkg.json` | `VCPKG_ROOT` = clone of microsoft/vcpkg |
| Python | 3.11+ | task runner and scripts |
| clang-format | 23.x | formatting is checked in CI |

## Windows

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM Python.Python.3.13
git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
py -m pip install --user py7zr
py scripts\install_qt.py --version 6.12.1 --outdir C:\Qt
[Environment]::SetEnvironmentVariable('VCPKG_ROOT','C:\vcpkg','User')
[Environment]::SetEnvironmentVariable('QT_ROOT_DIR','C:\Qt\6.12.1\msvc2022_64','User')
```

You do not need a "Developer PowerShell": `scripts/elara.py` finds Visual Studio
with `vswhere` and loads `vcvarsall.bat x64` itself.

## macOS

```sh
xcode-select --install
brew install cmake ninja llvm python
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
python3 -m pip install py7zr
python3 scripts/install_qt.py --version 6.12.1 --outdir ~/Qt
export VCPKG_ROOT=~/vcpkg QT_ROOT_DIR=<path printed by install_qt.py>
```

## Linux (Ubuntu 24.04)

```sh
sudo apt install build-essential g++-14 cmake ninja-build python3-pip \
  libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxcomposite-dev libgl1-mesa-dev libxkbcommon-dev
```

Then vcpkg and Qt as on macOS. Use `CXX=g++-14` (GCC 13 also works).

## Why `scripts/install_qt.py`

Qt 6.12 changed the layout of its online repository; aqtinstall 3.3.0 cannot
read it and the official MaintenanceTool requires an interactive Qt account.
The script downloads the same official archives from `download.qt.io`, checks
their SHA-1 and extracts them to `<outdir>/<version>/<kit>`. CI uses it too.

## Build trees

| Path | Content |
|---|---|
| `build/dev/` | Debug build (`dev` preset), `bin/` holds every executable |
| `build/release/` | RelWithDebInfo build |
| `build/_downloads/` | Cached third-party downloads (JUCE, the Bravura and Arimo fonts) shared by build trees |
| `build/package/` | `elara.py package` output: the install tree (`stage/`) and the package with its SHA-256 |

The first configure downloads JUCE (~25 MB), builds JUCE's helper tool and
builds the vcpkg packages; later builds are incremental.

## Troubleshooting

- **`MSVC not found`**: install the Build Tools workload above.
- **Windows Installer error 1618 while installing**: another installation is
  running (often a driver installer). Let it finish or reboot, then retry.
- **`Qt6Config.cmake` not found**: `QT_ROOT_DIR` must point at the kit folder
  (`.../6.12.1/msvc2022_64`), not at `C:\Qt`.
- **A test crashes after a header changed (Windows)**: under a non-English
  MSVC, Ninja records header dependencies only when the build tree was generated
  and built from consoles with the same code page (for example not generated
  from `cmd` and built from a UTF-8 terminal). The task runner always uses the
  OEM code page and regenerates a tree written for another one, so go through
  it. After calling `cmake --build` directly, list the objects without
  dependencies with `ninja -C build/dev -t deps | findstr "#deps 0"` and delete
  them to rebuild them.
