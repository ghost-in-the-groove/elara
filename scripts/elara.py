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
    python scripts/elara.py ci                   what CI runs (fresh configure)

On Windows the MSVC developer environment is loaded automatically (vswhere +
vcvarsall.bat), and VCPKG_ROOT / QT_ROOT_DIR are read from the user
environment when the current shell was started before they were set.
Output is kept short: commands are echoed, tool output is shown as-is.
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from collections.abc import Sequence
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


def run(cmd: Sequence[str], *, env: dict[str, str] | None = None, cwd: Path = ROOT) -> int:
    print("$ " + " ".join(cmd), flush=True)
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


def cmd_build(args: argparse.Namespace) -> int:
    if not (build_dir(args.preset) / "CMakeCache.txt").exists():
        if (code := configure(args.preset)) != 0:
            return code
    return run(["cmake", "--build", "--preset", args.preset])


def cmd_test(args: argparse.Namespace) -> int:
    # Build first so a test run never exercises stale binaries.
    if not getattr(args, "no_build", False) and (code := cmd_build(args)) != 0:
        return code
    cmd = ["ctest", "--preset", args.preset]
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
    status = 0
    for start in range(0, len(files), 50):
        chunk = files[start:start + 50]
        result = subprocess.run([clang_format, *mode, *chunk], cwd=ROOT, check=False)
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
    if not (build_dir(preset) / "CMakeCache.txt").exists() and (code := configure(preset)) != 0:
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
        ("build", lambda: cmd_build(ns)),
        ("test", lambda: cmd_test(ns)),
    ])


def cmd_ci(args: argparse.Namespace) -> int:
    ns = argparse.Namespace(check=True, preset=args.preset, regex=None, no_build=True)
    return _gate([
        ("format", lambda: cmd_format(ns)),
        ("guard", lambda: cmd_guard(ns)),
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
            p.add_argument("--preset", default=DEFAULT_PRESET, help="CMake preset (dev, release)")
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
    add("ci", cmd_ci, "full CI sequence with a fresh configure")
    add("bench", cmd_bench, "build and run benchmarks (release preset)", preset=False).add_argument(
        "args", nargs=argparse.REMAINDER)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
