#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fail if a module includes something its layer may not depend on.

The rules mirror docs/architecture.md. CMake target links enforce the same
boundaries at link time; this guard catches header-only leaks early and gives
a precise file:line message. Add a rule here when you add a module.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

QT = re.compile(r"^(Q[A-Za-z]|Qt[A-Za-z]*/|qqml)")
JUCE = re.compile(r"^juce_")

# (path prefix relative to src/, allowed Elara modules, forbidden third-party patterns)
# The first matching prefix wins, so list sub-directories before their parents.
RULES: list[tuple[str, set[str], list[tuple[str, re.Pattern[str]]]]] = [
    ("audio/backend_juce/", {"core", "audio"}, [("Qt", QT)]),
    ("core/", set(), [("Qt", QT), ("JUCE", JUCE)]),
    ("audio/", {"core"}, [("Qt", QT), ("JUCE", JUCE)]),
    ("io/", {"core"}, [("Qt", QT), ("JUCE", JUCE)]),
    ("notation/", {"core"}, [("Qt", QT), ("JUCE", JUCE)]),
    ("cli/", {"core", "audio", "io", "notation"}, [("Qt", QT), ("JUCE", JUCE)]),
    ("app/", {"core", "audio", "io", "notation"}, [("JUCE", JUCE)]),
]

# Public headers of elara_audio_juce must stay JUCE-free even though its sources use JUCE.
PUBLIC_HEADER_EXTRA = {"audio/backend_juce/include/": [("JUCE", JUCE)]}


def module_of(include: str) -> str | None:
    match = re.match(r"^elara/([a-z_]+)/", include)
    return match.group(1) if match else None


def main() -> int:
    src = ROOT / "src"
    violations = []
    checked = 0
    for path in sorted(src.rglob("*")):
        if path.suffix not in {".cpp", ".hpp", ".h", ".mm"} or not path.is_file():
            continue
        rel = path.relative_to(src).as_posix()
        rule = next((r for r in RULES if rel.startswith(r[0])), None)
        if rule is None:
            violations.append(f"src/{rel}: no architecture rule covers this directory "
                              "(add one to scripts/verify_architecture.py)")
            continue
        prefix, allowed, forbidden = rule
        own_module = prefix.split("/")[0]
        extra = next((v for k, v in PUBLIC_HEADER_EXTRA.items() if rel.startswith(k)), [])
        checked += 1
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = INCLUDE.match(line)
            if not match:
                continue
            target = match.group(1)
            module = module_of(target)
            if module is not None and module != own_module and module not in allowed:
                violations.append(f"src/{rel}:{number}: '{own_module}' may not include "
                                  f"<{target}> (allowed: {sorted(allowed) or 'none'})")
            for label, pattern in [*forbidden, *extra]:
                if pattern.match(target):
                    violations.append(f"src/{rel}:{number}: {label} include <{target}> "
                                      f"is not allowed in src/{prefix}")
    for violation in violations:
        print(violation)
    print(f"guard: {checked} files checked, {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
