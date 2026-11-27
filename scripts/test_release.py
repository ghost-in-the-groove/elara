# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests of scripts/release.py, run by `elara.py check` (ADR-0138)."""

from __future__ import annotations

import datetime
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import release
from release import ReleaseError

ROOT = Path(__file__).resolve().parent.parent

CMAKE = """cmake_minimum_required(VERSION 3.28)

project(elara
  VERSION 0.1.0
  DESCRIPTION "Tablature"
  LANGUAGES C CXX)
find_package(Qt6 6.12 REQUIRED)
"""

VCPKG = """{
  "name": "elara",
  "version": "0.1.0",
  "builtin-baseline": "abc",
  "overrides": [{ "name": "fluidsynth", "version": "2.4.0" }]
}
"""

CHANGELOG = """# Changelog

What changed in each version.

## Unreleased

- Notes are typed.

## 0.1.0 - 2026-12-01

- The first preview.
"""

DAY = datetime.date(2027, 1, 15)


class VersionTest(unittest.TestCase):
    def test_versions_are_three_numbers(self) -> None:
        self.assertEqual(release.parse_version("0.10.2"), (0, 10, 2))
        for wrong in ("1.2", "v1.2.3", "1.2.3-beta", "01.2.3", "1.2.3.4"):
            with self.assertRaises(ReleaseError, msg=wrong):
                release.parse_version(wrong)

    def test_cmake_changes_only_the_project_version(self) -> None:
        self.assertEqual(release.cmake_version(CMAKE), "0.1.0")
        changed = release.with_cmake_version(CMAKE, "0.2.0")
        self.assertEqual(release.cmake_version(changed), "0.2.0")
        self.assertIn("cmake_minimum_required(VERSION 3.28)", changed)
        self.assertIn("find_package(Qt6 6.12 REQUIRED)", changed)
        self.assertEqual(changed.replace("0.2.0", "0.1.0"), CMAKE)

    def test_vcpkg_changes_only_the_manifest_version(self) -> None:
        changed = release.with_vcpkg_version(VCPKG, "0.2.0")
        self.assertEqual(json.loads(changed)["version"], "0.2.0")
        self.assertEqual(json.loads(changed)["overrides"][0]["version"], "2.4.0")
        self.assertEqual(changed.replace('"0.2.0"', '"0.1.0"'), VCPKG)

    def test_the_sources_carry_one_version(self) -> None:
        version = release.sources_version(ROOT)
        release.parse_version(version)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "CMakeLists.txt").write_text(CMAKE, encoding="utf-8")
            (root / "vcpkg.json").write_text(release.with_vcpkg_version(VCPKG, "0.2.0"),
                                             encoding="utf-8")
            with self.assertRaisesRegex(ReleaseError, "0.1.0 and vcpkg.json 0.2.0"):
                release.sources_version(root)


class ChangelogTest(unittest.TestCase):
    def test_a_section_is_read_up_to_the_next(self) -> None:
        self.assertEqual(release.changelog_section(CHANGELOG), "- Notes are typed.\n")
        self.assertEqual(release.changelog_section(CHANGELOG, "0.1.0"), "- The first preview.\n")
        with self.assertRaisesRegex(ReleaseError, "no 0.3.0 section"):
            release.changelog_section(CHANGELOG, "0.3.0")

    def test_unreleased_becomes_the_version(self) -> None:
        cut = release.cut_release(CHANGELOG, "0.2.0", DAY)
        self.assertIn("## Unreleased\n\n## 0.2.0 - 2027-01-15\n\n- Notes are typed.\n", cut)
        self.assertEqual(release.changelog_section(cut, "0.2.0"), "- Notes are typed.\n")
        self.assertEqual(release.changelog_section(cut, "0.1.0"), "- The first preview.\n")
        # Nothing waits for the next one yet.
        with self.assertRaisesRegex(ReleaseError, "says nothing"):
            release.changelog_section(cut)

    def test_a_version_is_cut_once_and_says_something(self) -> None:
        with self.assertRaisesRegex(ReleaseError, "already has a 0.1.0 section"):
            release.cut_release(CHANGELOG, "0.1.0", DAY)
        empty = CHANGELOG.replace("- Notes are typed.\n", "")
        with self.assertRaisesRegex(ReleaseError, "says nothing"):
            release.cut_release(empty, "0.2.0", DAY)

    def test_the_changelog_has_something_to_release(self) -> None:
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertTrue(release.changelog_section(text).strip())


class PrepareTest(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.mkdtemp()
        self.root = Path(self.folder)
        (self.root / "CMakeLists.txt").write_text(CMAKE, encoding="utf-8")
        (self.root / "vcpkg.json").write_text(VCPKG, encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")

    def tearDown(self) -> None:
        shutil.rmtree(self.folder)

    def test_a_release_is_set_in_the_sources(self) -> None:
        changed = release.prepare(self.root, "0.2.0", DAY)
        self.assertEqual(changed, ["CMakeLists.txt", "vcpkg.json", "CHANGELOG.md"])
        self.assertEqual(release.sources_version(self.root), "0.2.0")
        text = (self.root / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertEqual(release.changelog_section(text, "0.2.0"), "- Notes are typed.\n")

    def test_the_version_never_goes_back(self) -> None:
        (self.root / "CHANGELOG.md").write_text(
            CHANGELOG.replace("0.1.0 - 2026-12-01", "0.0.9 - 2026-11-01"), encoding="utf-8")
        with self.assertRaisesRegex(ReleaseError, "comes before 0.1.0"):
            release.prepare(self.root, "0.0.9", DAY)
        # The version the sources already carry is released as it is.
        self.assertEqual(release.prepare(self.root, "0.1.0", DAY), ["CHANGELOG.md"])

    def test_nothing_changes_when_it_cannot_be_released(self) -> None:
        with self.assertRaises(ReleaseError):
            release.prepare(self.root, "0.1.0", DAY)
        self.assertEqual((self.root / "CMakeLists.txt").read_text(encoding="utf-8"), CMAKE)
        self.assertEqual((self.root / "CHANGELOG.md").read_text(encoding="utf-8"), CHANGELOG)


if __name__ == "__main__":
    unittest.main()
