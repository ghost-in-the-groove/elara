# SPDX-License-Identifier: AGPL-3.0-or-later
"""Releases of Elara (ADR-0138): the version the sources carry, the sections
of CHANGELOG.md, and a release prepared in the sources.

Nothing here tags, pushes or publishes. A release is drafted by
.github/workflows/release.yml from a version tag and published by hand
(docs/releasing.md). `scripts/elara.py release` runs these:

    release version           the version, the same in CMakeLists.txt and vcpkg.json
    release notes [VERSION]   the changelog's section for VERSION (default: Unreleased)
    release prepare VERSION   VERSION in the sources, Unreleased turned into its section
"""

from __future__ import annotations

import datetime
import json
import re
import subprocess
from pathlib import Path

UNRELEASED = "Unreleased"

# A version is major.minor.patch, as CMake's project() holds it: no labels.
_VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
_CMAKE_VERSION = re.compile(r"(project\(\s*elara\s+VERSION\s+)([0-9][0-9.]*)")
_VCPKG_VERSION = re.compile(r'(\n\s*"version"\s*:\s*")([^"]*)(")')
_HEADING = re.compile(r"^## +(.+?)[ \t]*$", re.MULTILINE)


class ReleaseError(Exception):
    """What stops a release, said so that it can be fixed."""


def parse_version(text: str) -> tuple[int, int, int]:
    match = _VERSION.fullmatch(text)
    if match is None:
        raise ReleaseError(f"{text!r} is not a version: write major.minor.patch, as 0.2.0")
    return int(match[1]), int(match[2]), int(match[3])


def cmake_version(text: str) -> str:
    match = _CMAKE_VERSION.search(text)
    if match is None:
        raise ReleaseError("CMakeLists.txt has no project(elara VERSION ...)")
    return match[2]


def with_cmake_version(text: str, version: str) -> str:
    cmake_version(text)
    return _CMAKE_VERSION.sub(lambda m: m[1] + version, text, count=1)


def vcpkg_version(text: str) -> str:
    version = json.loads(text).get("version")
    if not isinstance(version, str):
        raise ReleaseError('vcpkg.json has no "version"')
    return version


def with_vcpkg_version(text: str, version: str) -> str:
    vcpkg_version(text)
    changed = _VCPKG_VERSION.sub(lambda m: m[1] + version + m[3], text, count=1)
    if vcpkg_version(changed) != version:
        raise ReleaseError('the first "version" of vcpkg.json is not the manifest\'s own')
    return changed


def sources_version(root: Path) -> str:
    """The version CMakeLists.txt and vcpkg.json both carry."""
    cmake = cmake_version((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    vcpkg = vcpkg_version((root / "vcpkg.json").read_text(encoding="utf-8"))
    if cmake != vcpkg:
        raise ReleaseError(f"CMakeLists.txt says {cmake} and vcpkg.json {vcpkg}")
    parse_version(cmake)
    return cmake


def _sections(text: str) -> list[tuple[str, int, int]]:
    """Each "## " section of the changelog: its name (the version, before
    " - date"), where its heading starts and where its body ends."""
    headings = list(_HEADING.finditer(text))
    sections = []
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
        sections.append((heading[1].split(" - ")[0].strip(), heading.start(), end))
    return sections


def changelog_section(text: str, version: str | None = None) -> str:
    """The body of a version's section, or of Unreleased; never empty."""
    name = version or UNRELEASED
    for section, start, end in _sections(text):
        if section == name:
            body = text[start:end].split("\n", 1)[1] if "\n" in text[start:end] else ""
            if body.strip():
                return body.strip() + "\n"
            raise ReleaseError(f"the changelog's {name} section says nothing")
    raise ReleaseError(f"the changelog has no {name} section")


def cut_release(text: str, version: str, date: datetime.date) -> str:
    """The changelog with Unreleased turned into the version's section, dated,
    under a new empty Unreleased."""
    parse_version(version)
    if any(section == version for section, _, _ in _sections(text)):
        raise ReleaseError(f"the changelog already has a {version} section")
    changelog_section(text, None)
    heading = _HEADING.search(text)
    while heading is not None and heading[1].strip() != UNRELEASED:
        heading = _HEADING.search(text, heading.end())
    assert heading is not None  # changelog_section() found it
    return (text[:heading.start()] + f"## {UNRELEASED}\n\n## {version} - {date.isoformat()}"
            + text[heading.end():])


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True,
                            encoding="utf-8", check=False)
    return result.stdout.strip() if result.returncode == 0 else ""


def changes_since_last_release(root: Path) -> list[str]:
    """The subjects of the commits since the last version tag (all of them
    before the first), to write Unreleased from."""
    tag = _git(root, "describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
    span = f"{tag}..HEAD" if tag else "HEAD"
    return [line for line in _git(root, "log", "--format=%s", span).splitlines() if line]


def prepare(root: Path, version: str, date: datetime.date) -> list[str]:
    """Sets `version` in CMakeLists.txt and vcpkg.json and turns the
    changelog's Unreleased into its section. Returns the files changed."""
    new = parse_version(version)
    current = sources_version(root)
    if new < parse_version(current):
        raise ReleaseError(f"{version} comes before {current}, the version of the sources")
    changelog = root / "CHANGELOG.md"
    text = changelog.read_text(encoding="utf-8")
    try:
        cut = cut_release(text, version, date)
    except ReleaseError as error:
        changes = changes_since_last_release(root)
        if not changes or "says nothing" not in str(error):
            raise
        listed = "\n".join(f"  {subject}" for subject in changes[:40])
        more = f"\n  ... and {len(changes) - 40} more" if len(changes) > 40 else ""
        raise ReleaseError(f"{error}: write what changed first. Since the last release:\n"
                           f"{listed}{more}") from error
    changed = []
    for name, rewrite in (("CMakeLists.txt", with_cmake_version), ("vcpkg.json", with_vcpkg_version)):
        path = root / name
        before = path.read_text(encoding="utf-8")
        after = rewrite(before, version)
        if after != before:
            path.write_bytes(after.encode("utf-8"))
            changed.append(name)
    changelog.write_bytes(cut.encode("utf-8"))
    changed.append("CHANGELOG.md")
    return changed
