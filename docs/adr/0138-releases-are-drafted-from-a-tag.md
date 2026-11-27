# 0138. Releases are drafted from a tag

- Status: Accepted
- Date: 2026-11-27

## Context

Every push to `main` builds and smoke-tests packages for the three platforms
(ADR-0048, ADR-0065), kept for two weeks as workflow artifacts. Nothing turned
those builds into a version: the version in `CMakeLists.txt` and `vcpkg.json`
was set by hand and never checked against anything, there was no changelog,
and making a release would have meant building, naming, checksumming and
uploading packages by hand.

The project stays in previews for now: no release is published until the
owner decides, so the automation must not publish anything on its own.

## Decision

- The version is `major.minor.patch`, numbers only, in `CMakeLists.txt` and
  `vcpkg.json`. `scripts/release.py` reads it, and `elara.py release version`
  fails when the two differ; the gate runs it with the scripts' own tests
  (`scripts/test_*.py`, a new `scripts` step).
- `CHANGELOG.md` keeps an `Unreleased` section written for the people who use
  Elara. It is written when a release is prepared, from the commits since the
  last tag, not in every pull request.
- `elara.py release prepare X.Y.Z` sets the version in the sources and turns
  Unreleased into the version's dated section. It refuses a version older than
  the sources', a version already in the changelog and an empty Unreleased,
  and never commits, tags or pushes. `elara.py release notes [X.Y.Z]` prints a
  section.
- `.github/workflows/release.yml` runs on a `v*` tag: it checks that the tag
  names the sources' version and that the changelog has its section, calls
  `package.yml` (now also a reusable workflow) for the packages, and creates a
  draft release with the packages, one `SHA256SUMS` and the section as notes,
  marked as a pre-release while the major version is 0. Run by hand it is a
  rehearsal: the same checks and packages, the notes of Unreleased, no draft.
- A maintainer publishes the draft by hand. `docs/releasing.md` says how.

## Consequences

- A release is a pull request (`Release X.Y.Z`) and a tag; the rest is built,
  checked and named the same way every time.
- No pre-release labels such as `-beta.1`: CMake's version holds numbers only,
  and while the major version is 0 every version is already a preview.
- The packages are unsigned, as before; signing on macOS and Windows needs
  paid certificates (`docs/SCOPE.md`).
- Nothing is released until someone pushes a tag.
