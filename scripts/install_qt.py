#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install a prebuilt Qt kit from the official Qt online repository.

Why this exists: aqtinstall 3.3.0 does not understand the per-kit repository
layout that Qt uses since 6.12 (qt6_6121/qt6_6121_msvc2022_64/Updates.xml), and
the Qt MaintenanceTool needs an interactive Qt account. The repository itself
is public, so this script reads its Updates.xml, downloads the archives of the
base package plus the requested add-ons, verifies their SHA-1 and extracts them.

Example:
    python scripts/install_qt.py --version 6.12.1 --outdir C:/Qt
    python scripts/install_qt.py --version 6.12.1 --modules qtshadertools,qtmultimedia

Prints the kit directory (the folder that contains lib/cmake/Qt6) on success.
Extraction uses py7zr when available, otherwise the 7z command.

The macOS kit is universal: its binaries hold an x86_64 and an arm64 build.
--thin x86_64 (or arm64) keeps only that one in each, about half the kit, for
a machine that builds for one architecture; CI caches the thinned kit.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = "https://download.qt.io/online/qtsdkrepository"

# host -> (repository host dir, default kit arch, package arch prefix)
HOSTS = {
    "windows": ("windows_x86", "msvc2022_64", "win64_"),
    "linux": ("linux_x64", "linux_gcc_64", ""),
    "mac": ("mac_x64", "clang_64", ""),
}

# Archives in the base package that a build never needs.
SKIP_ARCHIVE_PREFIXES = ("qtdoc-",)


def detect_host() -> str:
    system = platform.system()
    if system == "Windows":
        return "windows"
    if system == "Darwin":
        return "mac"
    return "linux"


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "elara-install-qt"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def find_updates_xml(host_dir: str, vnum: str, arch: str) -> tuple[str, ET.Element]:
    """Return (kit base URL, parsed Updates.xml), trying new then old layout."""
    candidates = [
        f"{REPO}/{host_dir}/desktop/qt6_{vnum}/qt6_{vnum}_{arch}",
        f"{REPO}/{host_dir}/desktop/qt6_{vnum}/qt6_{vnum}",
        f"{REPO}/{host_dir}/desktop/qt6_{vnum}",
    ]
    errors = []
    for base in candidates:
        try:
            return base, ET.fromstring(fetch(f"{base}/Updates.xml"))
        except urllib.error.HTTPError as exc:
            errors.append(f"{base}: HTTP {exc.code}")
    raise SystemExit("Could not find Updates.xml:\n  " + "\n  ".join(errors))


def select_packages(root: ET.Element, vnum: str, pkg_arch: str, modules: list[str]):
    wanted = {f"qt.qt6.{vnum}.{pkg_arch}"}
    wanted |= {f"qt.qt6.{vnum}.addons.{m}.{pkg_arch}" for m in modules}
    found = {}
    for pkg in root.iter("PackageUpdate"):
        name = pkg.findtext("Name", "")
        if name in wanted:
            archives = [
                a.strip()
                for a in (pkg.findtext("DownloadableArchives") or "").split(",")
                if a.strip() and not a.strip().startswith(SKIP_ARCHIVE_PREFIXES)
            ]
            found[name] = (pkg.findtext("Version", ""), archives)
    missing = sorted(wanted - found.keys())
    if missing:
        raise SystemExit("Packages not found in repository: " + ", ".join(missing))
    return found


def download(url: str, dest: Path) -> None:
    data = fetch(url)
    try:
        expected = fetch(url + ".sha1").decode().split()[0].strip().lower()
    except urllib.error.HTTPError:
        expected = None
    if expected and hashlib.sha1(data).hexdigest() != expected:
        raise SystemExit(f"SHA-1 mismatch for {url}")
    dest.write_bytes(data)


def extract_raw(archive: Path, dest: Path) -> None:
    try:
        import py7zr  # type: ignore[import-not-found]
    except ImportError:
        py7zr = None
    if py7zr is not None:
        with py7zr.SevenZipFile(archive, mode="r") as z:
            z.extractall(path=dest)
        return
    seven = shutil.which("7z") or shutil.which("7za")
    if not seven:
        raise SystemExit("Install py7zr (pip install py7zr) or 7-Zip to extract .7z archives.")
    subprocess.run([seven, "x", "-y", f"-o{dest}", str(archive)], check=True,
                   stdout=subprocess.DEVNULL)


def extract(archive: Path, outdir: Path, version: str, arch: str) -> None:
    """Extract into <outdir>/<version>/<arch>.

    Older archives carry the "<version>/<arch>/" prefix themselves; the 6.12
    per-kit archives do not, so stage first and place the content explicitly.
    """
    with tempfile.TemporaryDirectory(prefix="elara-qt-stage-", dir=outdir) as stage:
        staged = Path(stage)
        extract_raw(archive, staged)
        if (staged / version).is_dir():
            source, target = staged / version, outdir / version
        elif archive.name.startswith("icu-"):
            # The Linux ICU archive holds bare libicu*.so files, while Qt's tools
            # look for them through RPATH $ORIGIN/../lib.
            source, target = staged, outdir / version / arch / "lib"
        else:
            source, target = staged, outdir / version / arch
        target.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target, dirs_exist_ok=True)


# Mach-O universal ("fat") files: a big-endian header, then one entry per
# architecture giving where its complete Mach-O image lies in the file.
FAT_MAGIC, FAT_MAGIC_64 = 0xCAFEBABE, 0xCAFEBABF
CPU_TYPES = {"x86_64": 0x01000007, "arm64": 0x0100000C}


def thin_file(path: Path, cpu_type: int) -> int:
    """Replace a universal Mach-O file by its slice for `cpu_type`, as
    `lipo -thin` does: the slice is a whole Mach-O file, its code signature
    included. Returns the bytes saved; other files are left alone."""
    with path.open("rb") as f:
        head = f.read(8)
        if len(head) < 8:
            return 0
        magic, count = struct.unpack(">II", head)
        # Java class files share FAT_MAGIC; their next word is a version, not
        # a handful of architectures.
        if magic not in (FAT_MAGIC, FAT_MAGIC_64) or not 0 < count < 16:
            return 0
        entry = ">iiQQII" if magic == FAT_MAGIC_64 else ">iiIII"
        size = struct.calcsize(entry)
        slices = [struct.unpack(entry, f.read(size))[:4] for _ in range(count)]
        match = [(offset, length) for cpu, _, offset, length in slices if cpu == cpu_type]
        if not match:
            raise SystemExit(f"{path}: no slice for the requested architecture")
        offset, length = match[0]
        f.seek(offset)
        data = f.read(length)
    before = path.stat().st_size
    mode = path.stat().st_mode
    staged = path.with_name(path.name + ".thin")
    staged.write_bytes(data)
    os.chmod(staged, mode)
    staged.replace(path)
    return before - length


def thin_kit(kit: Path, arch: str) -> None:
    saved = sum(thin_file(p, CPU_TYPES[arch]) for p in kit.rglob("*")
                if p.is_file() and not p.is_symlink())
    print(f"thinned to {arch}: {saved / 2**20:.0f} MB less", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", default="6.12.1")
    parser.add_argument("--host", choices=sorted(HOSTS), default=detect_host())
    parser.add_argument("--arch", help="kit directory name, e.g. msvc2022_64")
    parser.add_argument("--modules", default="", help="comma-separated add-ons")
    parser.add_argument("--outdir", default="C:/Qt" if detect_host() == "windows" else str(Path.home() / "Qt"))
    parser.add_argument("--force", action="store_true", help="download even if the kit exists")
    parser.add_argument("--thin", choices=sorted(CPU_TYPES),
                        help="keep only this architecture in the macOS kit's universal binaries")
    args = parser.parse_args()

    host_dir, default_arch, pkg_prefix = HOSTS[args.host]
    arch = args.arch or default_arch
    vnum = args.version.replace(".", "")
    modules = [m.strip() for m in args.modules.split(",") if m.strip()]
    outdir = Path(args.outdir)

    existing = [p.parent.parent.parent for p in (outdir / args.version).glob("*/lib/cmake/Qt6")]
    if existing and not modules and not args.force:
        print(f"kit already installed (use --force to reinstall)", file=sys.stderr)
        print(existing[0].resolve())
        return 0

    base, root = find_updates_xml(host_dir, vnum, arch)
    packages = select_packages(root, vnum, pkg_prefix + arch, modules)

    outdir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="elara-qt-") as tmp:
        for name, (version, archives) in sorted(packages.items()):
            for archive in archives:
                url = f"{base}/{name}/{version}{archive}"
                local = Path(tmp) / archive
                print(f"download {archive}", file=sys.stderr, flush=True)
                download(url, local)
                extract(local, outdir, args.version, arch)
                local.unlink()

    kits = [p.parent.parent.parent for p in (outdir / args.version).glob("*/lib/cmake/Qt6")]
    if not kits:
        raise SystemExit(f"No kit with lib/cmake/Qt6 found under {outdir / args.version}")
    if args.thin:
        thin_kit(kits[0], args.thin)
    print(kits[0].resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
