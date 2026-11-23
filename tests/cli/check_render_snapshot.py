# SPDX-License-Identifier: AGPL-3.0-or-later
"""Snapshot test: `elara-cli render FILE ... --svg` must write the pages SNAPSHOT holds.

    check_render_snapshot.py ELARA_CLI FILE SNAPSHOT [RENDER OPTIONS...]

FILE is laid out on pages (`--layout page --tracks all` unless the options say
otherwise) and written as SVG. SNAPSHOT is the path of the expected SVG without
its extension: `SNAPSHOT.svg` for a score on one page, `SNAPSHOT-1.svg`,
`SNAPSHOT-2.svg`... for more.

Pages are compared as text, except numbers, which may differ by up to
TOLERANCE staff spaces: compilers may round the last bit of a computation
differently, so the third decimal of a coordinate is allowed to move. The font
path, which depends on where the SVG is written, is left out. On a difference
the test fails with a unified diff of the page.

Set ELARA_UPDATE_SNAPSHOTS=1 to write the snapshots instead, then review the
change in git like any other:

    ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot
"""

from __future__ import annotations

import difflib
import os
import pathlib
import re
import subprocess
import sys
import tempfile

TOLERANCE = 0.002
MAX_DIFF_LINES = 120
FONT_URL = re.compile(r'src:url\("[^"]*"\)')
NUMBER = re.compile(r"-?\d+(?:\.\d+)?")


def normalized(svg: str) -> str:
    return FONT_URL.sub('src:url("Bravura.otf")', svg)


def same(actual: str, expected: str) -> bool:
    """True when the texts match, numbers within TOLERANCE of each other."""
    if NUMBER.sub("#", actual) != NUMBER.sub("#", expected):
        return False
    return all(abs(float(a) - float(b)) <= TOLERANCE
               for a, b in zip(NUMBER.findall(actual), NUMBER.findall(expected)))


def pages(directory: pathlib.Path) -> list[pathlib.Path]:
    single = directory / "score.svg"
    if single.is_file():
        return [single]
    found = sorted(directory.glob("score-*.svg"), key=lambda p: int(p.stem.split("-")[-1]))
    return found


def snapshot_pages(stem: pathlib.Path, count: int) -> list[pathlib.Path]:
    if count == 1:
        return [stem.with_name(stem.name + ".svg")]
    return [stem.with_name(f"{stem.name}-{n}.svg") for n in range(1, count + 1)]


def existing_snapshots(stem: pathlib.Path) -> list[pathlib.Path]:
    single = stem.with_name(stem.name + ".svg")
    numbered = re.compile(re.escape(stem.name) + r"-\d+\.svg")
    found = [p for p in stem.parent.glob(stem.name + "-*.svg") if numbered.fullmatch(p.name)]
    return ([single] if single.is_file() else []) + sorted(found)


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    cli, source, stem = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
    options = sys.argv[4:]
    if "--layout" not in options:
        options += ["--layout", "page"]
    if "--tracks" not in options and "--track" not in options:
        options += ["--tracks", "all"]

    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        run = subprocess.run([cli, "render", source, *options, "--svg", str(directory / "score.svg")],
                             capture_output=True, text=True, encoding="utf-8", check=False)
        if run.returncode != 0:
            print(f"elara-cli exited with {run.returncode}:\n{run.stderr}", file=sys.stderr)
            return 1
        written = pages(directory)
        if not written:
            print("elara-cli wrote no SVG", file=sys.stderr)
            return 1
        actual = [normalized(p.read_text(encoding="utf-8")) for p in written]

    targets = snapshot_pages(stem, len(actual))
    if os.environ.get("ELARA_UPDATE_SNAPSHOTS") == "1":
        for stale in set(existing_snapshots(stem)) - set(targets):
            stale.unlink()
        stem.parent.mkdir(parents=True, exist_ok=True)
        for target, svg in zip(targets, actual):
            if not (target.is_file() and same(svg, target.read_text(encoding="utf-8"))):
                target.write_text(svg, encoding="utf-8", newline="\n")
                print(f"updated {target}")
        return 0

    expected_files = existing_snapshots(stem)
    if not expected_files:
        print(f"no snapshot {stem}.svg; run with ELARA_UPDATE_SNAPSHOTS=1 to write it",
              file=sys.stderr)
        return 1
    if len(expected_files) != len(actual):
        print(f"{len(actual)} pages laid out, {len(expected_files)} in the snapshot",
              file=sys.stderr)
        return 1
    failed = False
    for target, svg in zip(targets, actual):
        if not target.is_file():
            print(f"missing snapshot page {target}", file=sys.stderr)
            failed = True
            continue
        expected = target.read_text(encoding="utf-8")
        if same(svg, expected):
            continue
        failed = True
        diff = list(difflib.unified_diff(expected.splitlines(), svg.splitlines(),
                                         fromfile=str(target), tofile="elara-cli render",
                                         lineterm=""))
        print("\n".join(diff[:MAX_DIFF_LINES]))
        if len(diff) > MAX_DIFF_LINES:
            print(f"... {len(diff) - MAX_DIFF_LINES} more lines")
    if failed:
        print("\nIf the change is intended, refresh the snapshots with "
              "ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
