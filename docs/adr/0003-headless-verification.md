# 0003. Headless verification

- Status: Accepted
- Date: 2026-11-22

## Context

Playback and engraving are what users care about most, yet CI machines have no
speakers or screen, and a reviewer reading a diff cannot hear or see the result
either. A feature that is only observable through the GUI cannot be verified
automatically, so regressions in it go unnoticed.

## Decision

1. **`elara-cli` exposes every capability** with stable `--json` output:
   model dumps, imports, notation rendered to SVG/JSON, audio rendered offline
   to WAV plus event logs and level metrics.
2. **Snapshot tests** compare those outputs with reviewed files under `tests/`.
   Updating a snapshot is an explicit, reviewed change.
3. **The app has a smoke test** (`elara --smoke-test`, offscreen) that fails on
   any QML warning, so UI changes are at least load-checked in CI.
4. **One gate**: `python scripts/elara.py check` runs format check, architecture
   guard, build and all tests; a change is done only when it passes.

## Consequences

- Each feature costs a CLI subcommand and tests in addition to its UI.
- Humans still review audio quality and engraving aesthetics; snapshots make
  those reviews diffable.
