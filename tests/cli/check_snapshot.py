# SPDX-License-Identifier: AGPL-3.0-or-later
"""Snapshot test: `elara-cli --json import FILE` must print what SNAPSHOT holds.

    check_snapshot.py ELARA_CLI FILE SNAPSHOT

The output is compared as indented JSON, without the "file" path (it depends
on the checkout) and with the generator's version removed (it changes with
every release). On a difference the test fails with a unified diff.

Set ELARA_UPDATE_SNAPSHOTS=1 to write the snapshots instead, then review the
change in git like any other:

    ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot
"""

from __future__ import annotations

import difflib
import json
import os
import pathlib
import subprocess
import sys

MAX_DIFF_LINES = 200


def normalized(output: str) -> str:
    report = json.loads(output)
    report.pop("file", None)
    song = report.get("song")
    if isinstance(song, dict) and isinstance(song.get("generator"), str):
        song["generator"] = song["generator"].split(" ")[0]
    return json.dumps(report, indent=1, ensure_ascii=False) + "\n"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    cli, source, snapshot = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
    run = subprocess.run([cli, "--json", "import", source], capture_output=True, text=True,
                         encoding="utf-8", check=False)
    if run.returncode != 0:
        print(f"elara-cli exited with {run.returncode}:\n{run.stderr}", file=sys.stderr)
        return 1
    actual = normalized(run.stdout)

    if os.environ.get("ELARA_UPDATE_SNAPSHOTS") == "1":
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        snapshot.write_text(actual, encoding="utf-8", newline="\n")
        print(f"updated {snapshot}")
        return 0
    if not snapshot.is_file():
        print(f"no snapshot {snapshot}; run with ELARA_UPDATE_SNAPSHOTS=1 to write it",
              file=sys.stderr)
        return 1
    expected = snapshot.read_text(encoding="utf-8")
    if actual == expected:
        return 0
    diff = list(difflib.unified_diff(expected.splitlines(), actual.splitlines(),
                                     fromfile=str(snapshot), tofile="elara-cli output",
                                     lineterm=""))
    print("\n".join(diff[:MAX_DIFF_LINES]))
    if len(diff) > MAX_DIFF_LINES:
        print(f"... {len(diff) - MAX_DIFF_LINES} more lines")
    print("\nIf the change is intended, rerun with ELARA_UPDATE_SNAPSHOTS=1 and review it.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
