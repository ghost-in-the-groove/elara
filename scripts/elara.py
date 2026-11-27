#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Elara task runner - the single entry point for developers and CI.

    python scripts/elara.py doctor               check the toolchain
    python scripts/elara.py build [--preset P]   configure if needed, then build
    python scripts/elara.py test  [--preset P] [-R REGEX] [--no-build]
    python scripts/elara.py check                fast gate: format check, guard, build, tests
    python scripts/elara.py bench [-- ARGS]      benchmarks in the release preset
    python scripts/elara.py format [--check]     clang-format every C++ source
    python scripts/elara.py guard                module-boundary check
    python scripts/elara.py tidy [FILES...]      clang-tidy (default: changed .cpp files)
    python scripts/elara.py cli -- ARGS...       run the built elara-cli
    python scripts/elara.py app [-- ARGS...]     run the built desktop app
    python scripts/elara.py translations         refresh the UI translation files (lupdate)
    python scripts/elara.py release version|notes|prepare   versions and the changelog
    python scripts/elara.py ci                  what CI runs (fresh configure)

On Windows the MSVC developer environment is loaded automatically (vswhere +
vcvarsall.bat), and VCPKG_ROOT / QT_ROOT_DIR are read from the user
environment when the current shell was started before they were set.
Output is kept short: commands are echoed, tool output is shown as-is.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import datetime
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
from collections.abc import Iterator, Sequence
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IS_WINDOWS = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"
EXE = ".exe" if IS_WINDOWS else ""
CPP_DIRS = ("src", "tests")
CPP_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".mm"}
CLANG_FORMAT_MAJOR = 23
MIN_CMAKE = (3, 28)
DEFAULT_PRESET = "dev"


def log(message: str) -> None:
    print(f"[elara] {message}", flush=True)


# --------------------------------------------------------------------------- environment


def _user_env_var(name: str) -> str | None:
    value = os.environ.get(name)
    if value:
        return value
    if IS_WINDOWS:
        import winreg

        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
                raw, _ = winreg.QueryValueEx(key, name)
                return os.path.expandvars(str(raw))
        except OSError:
            return None
    return None


def _registry_path_entries() -> list[str]:
    """Machine + user PATH from the registry (tools installed after this shell started)."""
    import winreg

    entries: list[str] = []
    for hive, subkey in (
        (winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"),
        (winreg.HKEY_CURRENT_USER, "Environment"),
    ):
        try:
            with winreg.OpenKey(hive, subkey) as key:
                raw, _ = winreg.QueryValueEx(key, "Path")
        except OSError:
            continue
        entries += [os.path.expandvars(p) for p in str(raw).split(os.pathsep) if p]
    return entries


def find_vcvars() -> Path | None:
    vswhere = (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.exists():
        return None
    result = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        capture_output=True, text=True, check=False)
    lines = result.stdout.strip().splitlines()
    if not lines:
        return None
    vcvars = Path(lines[0]) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    return vcvars if vcvars.exists() else None


def _msvc_env() -> dict[str, str] | None:
    vcvars = find_vcvars()
    if vcvars is None:
        return None
    # A string (not a list) keeps cmd's quoting intact; /s strips the outer quotes.
    result = subprocess.run(f'cmd /d /s /c ""{vcvars}" x64 >nul && set"',
                            capture_output=True, text=True, errors="replace", check=False)
    if result.returncode != 0:
        return None
    env = {}
    for line in result.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key.upper()] = value
    # vcvarsall points VCPKG_ROOT at the copy bundled with Visual Studio; Elara
    # pins its own clone (vcpkg.json builtin-baseline), so never take that one.
    env.pop("VCPKG_ROOT", None)
    return env


_ENV: dict[str, str] | None = None


def build_env() -> dict[str, str]:
    """Environment for build tools: MSVC (Windows), VCPKG_ROOT, QT_ROOT_DIR, LLVM."""
    global _ENV
    if _ENV is not None:
        return _ENV
    env = {k.upper() if IS_WINDOWS else k: v for k, v in os.environ.items()}
    for name in ("VCPKG_ROOT", "QT_ROOT_DIR"):
        value = _user_env_var(name)
        if value:
            env[name] = value
    if IS_WINDOWS:
        current = env.get("PATH", "").split(os.pathsep)
        known = {p.lower().rstrip("\\") for p in current}
        extra = [p for p in _registry_path_entries() if p.lower().rstrip("\\") not in known]
        env["PATH"] = os.pathsep.join(current + extra)
    if IS_WINDOWS and not shutil.which("cl", path=env.get("PATH")):
        msvc = _msvc_env()
        if msvc:
            env.update(msvc)
    if IS_WINDOWS:
        llvm = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin"
        if llvm.is_dir() and str(llvm).lower() not in env.get("PATH", "").lower():
            env["PATH"] = env.get("PATH", "") + os.pathsep + str(llvm)
    _ENV = env
    return env


def which(tool: str) -> str | None:
    return shutil.which(tool, path=build_env().get("PATH"))


# --------------------------------------------------------------------------- MSVC header dependencies
#
# Under a non-English MSVC, /showIncludes lines start with a localized prefix
# ("Nota: inclusión del archivo:"). CMake writes that prefix into rules.ninja
# encoded for the console code page it generates under, cl prints it in the code
# page it compiles under, and Ninja compares bytes. When the two differ (a tree
# generated from a cmd window and built from a UTF-8 terminal), Ninja records no
# header dependencies, and a later header change leaves stale objects behind.
# The runner therefore runs every tool with the console's OEM code page, and
# regenerates a tree whose prefix was written for another one.


def _console_code_pages() -> tuple[int, int]:
    """(current output code page, OEM code page); the first is 0 without a console."""
    import ctypes
    kernel32 = ctypes.windll.kernel32
    return kernel32.GetConsoleOutputCP(), kernel32.GetOEMCP()


@contextlib.contextmanager
def oem_console() -> Iterator[None]:
    """Sets the console's output code page to the OEM one while build tools run."""
    if not IS_WINDOWS:
        yield
        return
    import ctypes
    kernel32 = ctypes.windll.kernel32
    previous, oem = _console_code_pages()
    changed = previous not in (0, oem) and bool(kernel32.SetConsoleOutputCP(oem))
    try:
        yield
    finally:
        if changed:
            kernel32.SetConsoleOutputCP(previous)


def _deps_prefix_is_stale(preset: str) -> bool:
    """True when rules.ninja holds the localized prefix encoded for a code page
    other than the OEM one, so Ninja would not recognise what cl prints."""
    if not IS_WINDOWS or _console_code_pages()[0] == 0:
        return False
    build = build_dir(preset)
    rules = build / "CMakeFiles" / "rules.ninja"
    written = re.search(rb"msvc_deps_prefix = ([^\r\n]*)", rules.read_bytes()) if rules.exists() else None
    if written is None or written.group(1).isascii():
        return False  # an English prefix reads the same in every code page
    for compiler in build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"):
        found = re.search(r'set\(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "([^"]*)"\)',
                          compiler.read_text(encoding="utf-8", errors="replace"))
        if found:
            try:
                expected = found.group(1).encode(f"cp{_console_code_pages()[1]}")
            except (LookupError, UnicodeEncodeError):
                return False
            return written.group(1) != expected
    return False


def _forget_objects_without_dependencies(preset: str) -> None:
    """Deletes the objects Ninja recorded no header dependencies for, so the next
    build compiles them again and records them."""
    ninja = which("ninja")
    if not ninja:
        return
    build = build_dir(preset)
    result = subprocess.run([ninja, "-C", str(build), "-t", "deps"], capture_output=True,
                            text=True, errors="replace", env=build_env(), check=False)
    for line in result.stdout.splitlines():
        if line and not line[0].isspace() and ": #deps 0," in line:
            (build / line.split(": #deps", 1)[0]).unlink(missing_ok=True)


def run(cmd: Sequence[str], *, env: dict[str, str] | None = None, cwd: Path = ROOT) -> int:
    print("$ " + " ".join(cmd), flush=True)
    with oem_console():
        return subprocess.run(list(cmd), env=env or build_env(), cwd=cwd, check=False).returncode


def build_dir(preset: str) -> Path:
    return ROOT / "build" / preset


# --------------------------------------------------------------------------- sources


def cpp_sources() -> list[Path]:
    files = []
    for top in CPP_DIRS:
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix in CPP_SUFFIXES and path.is_file():
                files.append(path)
    return files


def changed_cpp_files() -> list[Path]:
    out = subprocess.run(["git", "status", "--porcelain", "--untracked-files=all"],
                         cwd=ROOT, capture_output=True, text=True, check=False).stdout
    files = []
    for line in out.splitlines():
        name = line[3:].split(" -> ")[-1].strip('"')
        path = ROOT / name
        if path.suffix == ".cpp" and path.exists():
            files.append(path)
    return files


# --------------------------------------------------------------------------- commands


def cmd_doctor(_: argparse.Namespace) -> int:
    env = build_env()
    failures = 0

    def report(ok: bool, what: str, hint: str = "", required: bool = True) -> None:
        nonlocal failures
        tag = "ok  " if ok else ("FAIL" if required else "warn")
        print(f"[{tag}] {what}" + ("" if ok or not hint else f"\n       -> {hint}"))
        if not ok and required:
            failures += 1

    py_ok = sys.version_info >= (3, 11)
    report(py_ok, f"python {platform.python_version()}", "install Python 3.11 or newer")

    cmake = which("cmake")
    cmake_version = ""
    if cmake:
        match = re.search(r"(\d+)\.(\d+)\.(\d+)",
                          subprocess.run([cmake, "--version"], capture_output=True, text=True,
                                         check=False).stdout)
        cmake_version = match.group(0) if match else "?"
        cmake_ok = bool(match) and (int(match.group(1)), int(match.group(2))) >= MIN_CMAKE
    else:
        cmake_ok = False
    report(cmake_ok, f"cmake {cmake_version or 'not found'}", "install CMake 3.28 or newer")

    report(bool(which("ninja")), "ninja", "install Ninja (winget install Ninja-build.Ninja)")

    if IS_WINDOWS:
        vcvars = find_vcvars()
        report(vcvars is not None or bool(which("cl")),
               f"MSVC {'(' + str(vcvars.parent.parent.parent.parent) + ')' if vcvars else 'not found'}",
               "install Visual Studio 2022 Build Tools with the C++ workload "
               "(Microsoft.VisualStudio.Workload.VCTools)")
    else:
        compiler = which("c++") or which("clang++") or which("g++")
        report(bool(compiler), f"C++ compiler {compiler or 'not found'}", "install GCC 13+ or Clang 17+")

    qt = env.get("QT_ROOT_DIR")
    qt_ok = bool(qt) and (Path(qt) / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").exists()
    report(qt_ok, f"Qt kit {qt or 'QT_ROOT_DIR not set'}",
           "python scripts/install_qt.py --version 6.12.1, then set QT_ROOT_DIR to the printed path")

    vcpkg = env.get("VCPKG_ROOT")
    vcpkg_ok = bool(vcpkg) and (Path(vcpkg) / f"vcpkg{EXE}").exists()
    report(vcpkg_ok, f"vcpkg {vcpkg or 'VCPKG_ROOT not set'}",
           "git clone https://github.com/microsoft/vcpkg, run bootstrap-vcpkg, set VCPKG_ROOT")

    clang_format = which("clang-format")
    cf_version = ""
    if clang_format:
        match = re.search(r"version (\d+)\.", subprocess.run(
            [clang_format, "--version"], capture_output=True, text=True, check=False).stdout)
        cf_version = match.group(0).removeprefix("version ").rstrip(".") if match else "?"
        cf_ok = bool(match) and int(match.group(1)) == CLANG_FORMAT_MAJOR
    else:
        cf_ok = False
    report(cf_ok, f"clang-format {cf_version or 'not found'} (need {CLANG_FORMAT_MAJOR}.x)",
           "install LLVM 23 (winget install LLVM.LLVM) or pip install 'clang-format>=23,<24'")

    report(bool(which("clang-tidy")), "clang-tidy (optional)", "comes with LLVM", required=False)

    print("doctor: " + ("all required tools found" if failures == 0 else f"{failures} problem(s)"))
    return 0 if failures == 0 else 1


def configure(preset: str, fresh: bool = False) -> int:
    cmd = ["cmake", "--preset", preset]
    if fresh:
        cmd.append("--fresh")
    return run(cmd)


def ensure_configured(preset: str) -> int:
    """Configures a new build tree, and regenerates one whose MSVC dependency
    prefix was written for another console code page (see oem_console)."""
    if not (build_dir(preset) / "CMakeCache.txt").exists():
        return configure(preset)
    if _deps_prefix_is_stale(preset):
        log("regenerating: the MSVC dependency prefix was written for another console code page")
        if (code := configure(preset)) != 0:
            return code
        _forget_objects_without_dependencies(preset)
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    if (code := ensure_configured(args.preset)) != 0:
        return code
    return run(["cmake", "--build", "--preset", args.preset])


def cmd_test(args: argparse.Namespace) -> int:
    # Build first so a test run never exercises stale binaries.
    if not getattr(args, "no_build", False) and (code := cmd_build(args)) != 0:
        return code
    # Every test is a process of its own and writes only its own files, so
    # they run side by side, one per core (CONTRIBUTING.md).
    cmd = ["ctest", "--preset", args.preset, "--parallel", str(os.cpu_count() or 1)]
    if getattr(args, "regex", None):
        cmd += ["-R", args.regex]
    return run(cmd)


def cmd_format(args: argparse.Namespace) -> int:
    clang_format = which("clang-format")
    if not clang_format:
        log("clang-format not found (run: python scripts/elara.py doctor)")
        return 1
    files = [str(p) for p in cpp_sources()]
    mode = ["--dry-run", "--Werror"] if args.check else ["-i"]

    # One clang-format per core, each on its own share of the files; the
    # reports are printed in file order once they have all finished.
    def format_chunk(chunk: list[str]) -> subprocess.CompletedProcess[str]:
        return subprocess.run([clang_format, *mode, *chunk], cwd=ROOT, capture_output=True,
                              text=True, errors="replace", check=False)

    chunks = [files[start:start + 25] for start in range(0, len(files), 25)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        results = list(pool.map(format_chunk, chunks))
    status = 0
    for result in results:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        status = status or result.returncode
    if args.check and status:
        log("formatting differs - run: python scripts/elara.py format")
    return status


def cmd_guard(_: argparse.Namespace) -> int:
    return run([sys.executable, str(ROOT / "scripts" / "verify_architecture.py")])


def cmd_tidy(args: argparse.Namespace) -> int:
    clang_tidy = which("clang-tidy")
    if not clang_tidy:
        log("clang-tidy not found")
        return 1
    db = build_dir(args.preset) / "compile_commands.json"
    if not db.exists():
        log(f"{db} missing - build the '{args.preset}' preset first")
        return 1
    files = args.files or [str(p) for p in changed_cpp_files()]
    if not files:
        log("no changed .cpp files")
        return 0
    return run([clang_tidy, "-p", str(db.parent), "--quiet", *files])


def _strip_dashes(extra: list[str]) -> list[str]:
    return extra[1:] if extra and extra[0] == "--" else extra


def cmd_cli(args: argparse.Namespace) -> int:
    exe = build_dir(args.preset) / "bin" / f"elara-cli{EXE}"
    if not exe.exists():
        log(f"{exe} not built - run: python scripts/elara.py build")
        return 1
    return subprocess.run([str(exe), *_strip_dashes(args.args)], cwd=ROOT,
                          env=build_env(), check=False).returncode


def cmd_bench(args: argparse.Namespace) -> int:
    """Build and run the micro-benchmarks in the optimised release preset."""
    preset = "release"
    if (code := ensure_configured(preset)) != 0:
        return code
    if (code := run(["cmake", "--build", "--preset", preset, "--target", "elara_benchmarks"])) != 0:
        return code
    filters = _strip_dashes(args.args)
    exe = build_dir(preset) / "bin" / f"elara_benchmarks{EXE}"
    code = subprocess.run([str(exe), *filters, "--allow-running-no-tests"], cwd=ROOT,
                          env=build_env(), check=False).returncode
    if code != 0:
        return code
    # The painting benchmarks live with the app tests (they need Qt).
    cache = (build_dir(preset) / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    if "ELARA_BUILD_APP:BOOL=ON" not in cache:
        return 0
    if (code := run(["cmake", "--build", "--preset", preset, "--target", "elara_app_tests"])) != 0:
        return code
    env = dict(build_env())
    qt = env.get("QT_ROOT_DIR")
    if IS_WINDOWS and qt:
        env["PATH"] = str(Path(qt) / "bin") + os.pathsep + env.get("PATH", "")
    app_tests = build_dir(preset) / "bin" / f"elara_app_tests{EXE}"
    app_filters = filters or ["[!benchmark]"]
    return subprocess.run([str(app_tests), *app_filters, "--allow-running-no-tests"], cwd=ROOT,
                          env=env, check=False).returncode


def cmd_app(args: argparse.Namespace) -> int:
    bin_dir = build_dir(args.preset) / "bin"
    exe = (bin_dir / "elara.app" / "Contents" / "MacOS" / "elara") if IS_MAC else bin_dir / f"elara{EXE}"
    if not exe.exists():
        log(f"{exe} not built - run: python scripts/elara.py build")
        return 1
    env = dict(build_env())
    qt = env.get("QT_ROOT_DIR")
    if IS_WINDOWS and qt:
        env["PATH"] = str(Path(qt) / "bin") + os.pathsep + env.get("PATH", "")
    return subprocess.run([str(exe), *_strip_dashes(args.args)], cwd=ROOT, env=env,
                          check=False).returncode


def _without_qt_on_path(env: dict[str, str]) -> dict[str, str]:
    """`env` with no Qt kit on PATH, so a run finds only the libraries a package
    brought along."""
    qt = env.get("QT_ROOT_DIR")
    clean = dict(env)
    clean.pop("QT_PLUGIN_PATH", None)
    clean.pop("QML_IMPORT_PATH", None)
    clean.pop("QML2_IMPORT_PATH", None)
    if qt:
        qt_dir = os.path.normcase(os.path.abspath(qt))
        entries = clean.get("PATH", "").split(os.pathsep)
        clean["PATH"] = os.pathsep.join(
            e for e in entries if not os.path.normcase(os.path.abspath(e)).startswith(qt_dir))
    return clean


# The SoundFont that comes with the packages (ADR-0069): MuseScore General 0.2
# (MIT) and its licence, from MuseScore's own mirror, checked against these
# SHA-256 digests.
SOUNDFONT_URL = "https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/"
SOUNDFONT_FILES = {
    "MuseScore_General.sf3": "5b85b6c2c61d10b2b91cddd41efcce7b25cd31c8271d511c73afafbef20b6fa3",
    "MuseScore_General_License.md":
        "5ad8d737e13c7f01f5b9674872a82a92b4ba253603e8ed14b9db12293550b4b9",
}

# The guitar SFZ instrument that comes with the packages (ADR-0072): the small
# bridge pickup set of FreePats' FSBS clean electric guitar (CC0), its licence
# and its samples, from the library's repository at a fixed commit, checked
# against these SHA-256 digests. The instrument is saved under a plainer name,
# the one the app shows (GUITAR_SFZ_RENAMED).
GUITAR_SFZ_URL = ("https://raw.githubusercontent.com/freepats/electric-guitar-FSBS-clean/"
                  "192cf0d9bf2c4ba6ead8e3524ba3f78818e4fe91/")
GUITAR_SFZ_FILES = {
    "EGuitarFSBS-clean bridge small 20260807.sfz":
        "aa74229ecf4a7ea8dc5773bb0a943481e146c767304222d8210a3b5fc368f095",
    "LICENSE.txt": "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499",
    "samples/bridge/small/A2_s2_01.flac":
        "c63270dbb8d4600600662abf55d94a63123a68ff26bc04f1f560ae209817c76e",
    "samples/bridge/small/B3_s5_01.flac":
        "3a02f89dcd9a2431ddd2f91391f2b9f1b39a4fa335a4db09bb387f0a5a8b8b24",
    "samples/bridge/small/B4_s6_01.flac":
        "56823645bde6d2d4571d9827800c5e6002bb9729918f63eebbbaa10036a9f78d",
    "samples/bridge/small/C#6_s6_01.flac":
        "1c4ab2abeb962c8b4e96bc72b67bacdbc803d3c07649f08a88b0bf81500ec143",
    "samples/bridge/small/C2_s1_01.flac":
        "769cef1cb056680d05ad093baf01ee1a3fbca521371290fe9a81cb2706c615ce",
    "samples/bridge/small/C3_s2_02.flac":
        "4f6a0f6411e6ccf0cbde6bbe6148d916d8e513a16484237245be2095b00c3169",
    "samples/bridge/small/D5_s6_01.flac":
        "2c718b0225600266bec188dc9d520d26169338b52e336404ad61eaff49043cce",
    "samples/bridge/small/E3_s3_01.flac":
        "80f77c9ba5845ac0e827187b8e3b3952b5fb8a818a1a1d2e65cd5d1d690a8213",
    "samples/bridge/small/E4_s6_01.flac":
        "d3797573c4eb28429484c923d3bf12157c7f409a64ad354601061134055d4f9f",
    "samples/bridge/small/F2_s1_01.flac":
        "1ec869eb4cc3e2f1c1751f6e495c551a2f542122a9d1ef6cf064ad37c2a3a1b5",
    "samples/bridge/small/G#5_s6_03.flac":
        "4027b0d7f6cab7df8398d161ed3f86a51cb621bf9a9e01f922455285bd91a4f6",
    "samples/bridge/small/G3_s4_01.flac":
        "37cdcff86d3d666dde739d96fe25f64ec864adbbf8adb6d131e2f24ea13dd200",
    "samples/bridge/small/G4_s6_01.flac":
        "33366b0a25842dc439b3be5d1b31b31ba44674d3c39348fa6eff6e39d5bc6314",
}
GUITAR_SFZ_RENAMED = {"EGuitarFSBS-clean bridge small 20260807.sfz": "FreePats Clean Guitar.sfz"}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _fetch(kind: str, url: str, files: dict[str, str],
           renamed: dict[str, str] | None = None) -> Path | None:
    """Downloads `files` (paths under `url`, with their digests) into
    build/downloads/`kind` the first time, under the names `renamed` gives
    them, checks them, and returns that folder; None when a file cannot be
    downloaded or does not match its digest."""
    folder = ROOT / "build" / "downloads" / kind
    for name, digest in files.items():
        path = folder / (renamed or {}).get(name, name)
        if path.exists() and _sha256(path) == digest:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = folder.parent / f"{kind}.part"  # outside the installed folder
        source = url + urllib.parse.quote(name)  # spaces, and `#` in sample names
        log(f"downloading {source}")
        try:
            with urllib.request.urlopen(source, timeout=300) as response, \
                    partial.open("wb") as out:
                shutil.copyfileobj(response, out)
            if _sha256(partial) != digest:
                log(f"{name} does not match its SHA-256 digest")
                return None
            partial.replace(path)
        except OSError as error:
            log(f"could not download {name}: {error}")
            return None
        finally:
            partial.unlink(missing_ok=True)
    return folder


def _fetch_soundfont() -> Path | None:
    """The included SoundFont and its licence, in build/downloads/soundfont."""
    return _fetch("soundfont", SOUNDFONT_URL, SOUNDFONT_FILES)


def _fetch_guitar_sfz() -> Path | None:
    """The included guitar SFZ instrument, its licence and its samples, in
    build/downloads/guitar-sfz."""
    return _fetch("guitar-sfz", GUITAR_SFZ_URL, GUITAR_SFZ_FILES, GUITAR_SFZ_RENAMED)


def _included_soundfont(stage: Path) -> Path | None:
    """The SoundFont `stage` installed, where the app looks for it."""
    folder = (stage / "elara.app" / "Contents" / "Resources" / "soundfonts" if IS_MAC
              else stage / "share" / "elara" / "soundfonts")
    fonts = sorted(folder.glob("*.sf[23]")) if folder.is_dir() else []
    return fonts[0] if fonts else None


def _included_guitar_sfz(stage: Path) -> Path | None:
    """The guitar SFZ instrument `stage` installed, where the app looks for it."""
    folder = (stage / "elara.app" / "Contents" / "Resources" / "sfz" if IS_MAC
              else stage / "share" / "elara" / "sfz")
    instruments = sorted(folder.glob("*/*.sfz")) if folder.is_dir() else []
    return instruments[0] if instruments else None


def _smoke_test_install(stage: Path) -> int:
    """Runs what `stage` installed the way a user would: the app's smoke test,
    a CLI render that needs its fonts and, when the SoundFont and the guitar
    SFZ instrument came along, the app playing a song through each of them,
    with no Qt kit on PATH."""
    tools = stage / "elara.app" / "Contents" / "MacOS" if IS_MAC else stage / "bin"
    app = tools / f"elara{EXE}"
    cli = tools / f"elara-cli{EXE}"
    env = _without_qt_on_path(build_env())
    if not IS_MAC:
        env["QT_QPA_PLATFORM"] = "offscreen"  # macOS: the cocoa plugin it ships
    work = stage.parent / "smoke"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    song = work / "song.elara"
    steps = [
        [str(app), "--smoke-test"],
        [str(cli), "--json", "version"],
        [str(cli), "edit", "--out", str(song), "add-track electric-guitar", "append-measures 1",
         "note 0 0 6 3 1"],
        [str(cli), "--json", "render", str(song), "--svg", str(work / "song.svg")],
    ]
    if _included_soundfont(stage) is not None:
        steps.append([str(app), "--smoke-test", str(song), "--included-soundfont"])
    if _included_guitar_sfz(stage) is not None:
        steps.append([str(app), "--smoke-test", str(song), "--included-guitar-sfz"])
    for step in steps:
        log("smoke: " + " ".join(Path(step[0]).name if i == 0 else a for i, a in enumerate(step)))
        result = subprocess.run(step, cwd=work, env=env, check=False, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=120)
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            log(f"smoke test failed (exit code {result.returncode})")
            return 1
    if not (work / "song.svg").stat().st_size:
        log("smoke test failed: the CLI wrote an empty SVG")
        return 1
    log("smoke: the installed app and CLI run without a Qt kit")
    return 0


APP_ID = "io.github.ghost_in_the_groove.Elara"


def _write_checksum(package: Path) -> None:
    """Writes `package`.sha256 the way CPack does for its packages."""
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    package.with_name(package.name + ".sha256").write_text(f"{digest}  {package.name}\n")


def _smoke_test_msi(out: Path, required: bool) -> int:
    """Unpacks the MSI package CPack made in `out` with an administrative
    install (the files, nothing registered) and smoke-tests that tree
    (ADR-0065)."""
    packages = sorted(out.glob("Elara-*.msi"))
    if not packages:
        log("no MSI installer (the WiX Toolset 3 was not found)")
        return 1 if required else 0
    target = out / "msi"
    shutil.rmtree(target, ignore_errors=True)
    if (code := run(["msiexec", "/a", str(packages[-1]), "/qn", f"TARGETDIR={target}",
                     "/l*v", str(out / "msi.log")])) != 0:
        log(f"the MSI installer did not install (exit code {code}); see {out / 'msi.log'}")
        return code
    apps = list(target.rglob(f"elara{EXE}"))
    if len(apps) != 1:
        log(f"the MSI installer holds {len(apps)} copies of the app, not one")
        return 1
    return _smoke_test_install(apps[0].parent.parent)


def _make_appimage(stage: Path, out: Path, required: bool) -> int:
    """Makes the Linux AppImage from the install tree with appimagetool (the
    APPIMAGETOOL variable or PATH), next to CPack's tarball, and runs the app's
    smoke test through it (ADR-0065)."""
    tool = os.environ.get("APPIMAGETOOL") or shutil.which("appimagetool")
    if not tool:
        log("no AppImage (appimagetool was not found)")
        return 1 if required else 0
    tarballs = sorted(out.glob("Elara-*.tar.gz"))
    if not tarballs:
        log("no tarball to name the AppImage after")
        return 1
    appimage = out / (tarballs[-1].name.removesuffix(".tar.gz") + ".AppImage")
    appdir = out / "Elara.AppDir"
    shutil.rmtree(appdir, ignore_errors=True)
    shutil.copytree(stage, appdir / "usr", symlinks=True)
    shutil.copy2(appdir / "usr" / "share" / "applications" / f"{APP_ID}.desktop", appdir)
    shutil.copy2(appdir / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps" /
                 f"{APP_ID}.png", appdir)
    (appdir / ".DirIcon").symlink_to(f"{APP_ID}.png")
    (appdir / "AppRun").symlink_to("usr/bin/elara")
    env = dict(build_env())
    env["ARCH"] = platform.machine()
    # appimagetool is an AppImage itself, and CI machines have no FUSE.
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    appimage.unlink(missing_ok=True)
    if (code := run([tool, "--no-appstream", str(appdir), str(appimage)], env=env)) != 0:
        return code
    _write_checksum(appimage)

    log(f"smoke: {appimage.name} --smoke-test")
    env = _without_qt_on_path(build_env())
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    # With the SoundFont that came along, a song (the install's smoke test
    # made it) plays through it.
    song = out / "smoke" / "song.elara"
    extra = ([str(song), "--included-soundfont"]
             if _included_soundfont(stage) is not None and song.exists() else [])
    result = subprocess.run([str(appimage), "--smoke-test", *extra], cwd=out, env=env,
                            check=False, capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=120)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        log(f"smoke test of the AppImage failed (exit code {result.returncode})")
        return 1
    log("smoke: the AppImage runs the app")
    return 0


def cmd_package(args: argparse.Namespace) -> int:
    """Builds the app and the CLI in the release preset, installs them into
    build/package/stage, smoke-tests that install and packs it with CPack
    (ADR-0048), then makes and smoke-tests the installers: the MSI package on
    Windows, the AppImage on Linux (ADR-0065)."""
    preset = "release"
    soundfont = None
    if not args.no_soundfont and (soundfont := _fetch_soundfont()) is None:
        log("no SoundFont to include (pack without one with --no-soundfont)")
        return 1
    guitar = None
    if not args.no_guitar_sfz and (guitar := _fetch_guitar_sfz()) is None:
        log("no guitar SFZ instrument to include (pack without one with --no-guitar-sfz)")
        return 1
    if (code := ensure_configured(preset)) != 0:
        return code
    soundfont_dir = soundfont.as_posix() if soundfont else ""
    guitar_dir = guitar.as_posix() if guitar else ""
    if (code := run(["cmake", "--preset", preset, f"-DELARA_SOUNDFONT_DIR={soundfont_dir}",
                     f"-DELARA_GUITAR_SFZ_DIR={guitar_dir}"])) != 0:
        return code
    if (code := run(["cmake", "--build", "--preset", preset, "--target", "elara", "elara-cli"])) != 0:
        return code
    out = ROOT / "build" / "package"
    stage = out / "stage"
    shutil.rmtree(stage, ignore_errors=True)
    if (code := run(["cmake", "--install", str(build_dir(preset)), "--prefix", str(stage)])) != 0:
        return code
    if not args.no_smoke_test and (code := _smoke_test_install(stage)) != 0:
        return code
    for old in out.glob("Elara-*"):
        if old.is_file():
            old.unlink()
    if (code := run(["cpack", "--config", str(build_dir(preset) / "CPackConfig.cmake"),
                     "-B", str(out)])) != 0:
        return code
    if IS_WINDOWS and not args.no_smoke_test:
        return _smoke_test_msi(out, args.installers)
    if not IS_WINDOWS and not IS_MAC:
        return _make_appimage(stage, out, args.installers)
    return 0


def cmd_translations(args: argparse.Namespace) -> int:
    """Refreshes the app's .ts files from its sources (lupdate). Translate the
    new or changed strings in Qt Linguist, or by hand, afterwards."""
    if (code := ensure_configured(args.preset)) != 0:
        return code
    return run(["cmake", "--build", "--preset", args.preset, "--target", "update_translations"])


def cmd_release(args: argparse.Namespace) -> int:
    """Versions and the changelog (ADR-0138, docs/releasing.md). `version`
    prints the version the sources carry, `notes` a version's section of
    CHANGELOG.md (Unreleased by default), `prepare` sets a version in the
    sources and turns Unreleased into its section. Never tags or pushes."""
    import release
    try:
        if args.action == "version":
            print(release.sources_version(ROOT))
        elif args.action == "notes":
            text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
            sys.stdout.write(release.changelog_section(text, args.version))
        else:
            if args.version is None:
                log("release prepare needs a version, as 0.2.0")
                return 2
            day = datetime.date.fromisoformat(args.date) if args.date else datetime.date.today()
            for name in release.prepare(ROOT, args.version, day):
                log(f"changed {name}")
            log(f"next: review CHANGELOG.md, then merge \"Release {args.version}\" through a pull "
                f"request; tagging v{args.version} on main drafts the release (docs/releasing.md)")
    except release.ReleaseError as error:
        log(str(error))
        return 1
    return 0


def cmd_scripts(_: argparse.Namespace) -> int:
    """The scripts' own tests, and the version the sources carry."""
    if (code := run([sys.executable, "-m", "unittest", "discover", "--start-directory",
                     str(ROOT / "scripts"), "--pattern", "test_*.py", "--quiet"])) != 0:
        return code
    return cmd_release(argparse.Namespace(action="version", version=None, date=None))


def _gate(steps: list[tuple[str, callable]]) -> int:
    results = []
    for name, step in steps:
        log(f"--- {name}")
        code = step()
        results.append((name, code))
        if code != 0:
            break
    print()
    for name, code in results:
        print(f"[{'ok  ' if code == 0 else 'FAIL'}] {name}")
    failed = [name for name, code in results if code != 0]
    print("check: " + ("passed" if not failed else f"failed at '{failed[0]}'"))
    return 0 if not failed else 1


def cmd_check(args: argparse.Namespace) -> int:
    ns = argparse.Namespace(check=True, preset=args.preset, regex=None, no_build=True)
    return _gate([
        ("format", lambda: cmd_format(ns)),
        ("guard", lambda: cmd_guard(ns)),
        ("scripts", lambda: cmd_scripts(ns)),
        ("build", lambda: cmd_build(ns)),
        ("test", lambda: cmd_test(ns)),
    ])


def cmd_ci(args: argparse.Namespace) -> int:
    ns = argparse.Namespace(check=True, preset=args.preset, regex=None, no_build=True)
    return _gate([
        ("format", lambda: cmd_format(ns)),
        ("guard", lambda: cmd_guard(ns)),
        ("scripts", lambda: cmd_scripts(ns)),
        ("configure", lambda: configure(args.preset, fresh=True)),
        ("build", lambda: run(["cmake", "--build", "--preset", args.preset])),
        ("test", lambda: cmd_test(ns)),
    ])


def main() -> int:
    parser = argparse.ArgumentParser(prog="elara.py", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name: str, func, help_text: str, preset: bool = True) -> argparse.ArgumentParser:
        p = sub.add_parser(name, help=help_text)
        if preset:
            p.add_argument("--preset", default=DEFAULT_PRESET, help="CMake preset (dev, release, sanitize)")
        p.set_defaults(func=func)
        return p

    add("doctor", cmd_doctor, "check the toolchain", preset=False)
    add("build", cmd_build, "configure if needed, then build")
    test = add("test", cmd_test, "build, then run CTest")
    test.add_argument("-R", dest="regex", help="test name regex")
    test.add_argument("--no-build", action="store_true", help="run the existing binaries")
    add("check", cmd_check, "fast gate: format check, guard, build, tests")
    add("format", cmd_format, "clang-format C++ sources", preset=False).add_argument(
        "--check", action="store_true", help="only report differences")
    add("guard", cmd_guard, "verify module boundaries", preset=False)
    add("tidy", cmd_tidy, "clang-tidy changed or given files").add_argument("files", nargs="*")
    add("cli", cmd_cli, "run elara-cli").add_argument("args", nargs=argparse.REMAINDER)
    add("app", cmd_app, "run the desktop app").add_argument("args", nargs=argparse.REMAINDER)
    add("translations", cmd_translations, "refresh the UI translation files from the sources")
    release = add("release", cmd_release, "versions and the changelog (never tags or pushes)",
                  preset=False)
    release.add_argument("action", choices=["version", "notes", "prepare"])
    release.add_argument("version", nargs="?", help="X.Y.Z (notes: Unreleased when left out)")
    release.add_argument("--date", help="the release's date for prepare, as 2027-01-15 "
                                        "(default: today)")
    add("ci", cmd_ci, "full CI sequence with a fresh configure")
    add("bench", cmd_bench, "build and run benchmarks (release preset)", preset=False).add_argument(
        "args", nargs=argparse.REMAINDER)
    package = add("package", cmd_package, "build, smoke-test and pack the app (release preset)",
        preset=False)
    package.add_argument("--no-smoke-test", action="store_true",
                         help="pack without running what was installed")
    package.add_argument("--installers", action="store_true",
                         help="fail when an installer cannot be made (MSI, AppImage)")
    package.add_argument("--no-soundfont", action="store_true",
                         help="pack without the SoundFont that comes with Elara (no download)")
    package.add_argument("--no-guitar-sfz", action="store_true",
                         help="pack without the guitar SFZ instrument that comes with Elara "
                              "(no download)")

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
