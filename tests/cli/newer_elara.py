# SPDX-License-Identifier: AGPL-3.0-or-later
"""Copies a .elara file as a newer Elara would have saved it: the same song,
with "generator" naming Elara 99.0.0 and a key this Elara does not know."""

import json
import sys
import zipfile


def main(source: str, target: str) -> None:
    with zipfile.ZipFile(source) as archive:
        song = json.loads(archive.read("song.json"))
    song["generator"] = "Elara 99.0.0"
    song["comingLater"] = {"what": "data an older Elara does not read"}
    with zipfile.ZipFile(target, "w") as archive:
        archive.writestr("mimetype", "application/vnd.elara.song+zip",
                         compress_type=zipfile.ZIP_STORED)
        archive.writestr("song.json", json.dumps(song, separators=(",", ":")),
                         compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
