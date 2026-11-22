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
"""

from __future__ import annotations

import argparse
import hashlib
import platform
import shutil
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
        else:
            source, target = staged, outdir / version / arch
        target.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target, dirs_exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", default="6.12.1")
    parser.add_argument("--host", choices=sorted(HOSTS), default=detect_host())
    parser.add_argument("--arch", help="kit directory name, e.g. msvc2022_64")
    parser.add_argument("--modules", default="", help="comma-separated add-ons")
    parser.add_argument("--outdir", default="C:/Qt" if detect_host() == "windows" else str(Path.home() / "Qt"))
    parser.add_argument("--force", action="store_true", help="download even if the kit exists")
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
    print(kits[0].resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
