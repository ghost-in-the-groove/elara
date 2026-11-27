# Releasing Elara

How a version of Elara is made (ADR-0138). Nothing is published until a
maintainer publishes a draft by hand, and nothing is tagged by a script.

## The version and the changelog

- The version lives in `CMakeLists.txt` (`project(elara VERSION X.Y.Z)`) and
  `vcpkg.json` (`"version"`); `python scripts/elara.py release version` prints
  it and fails when the two differ. The gate runs it.
- `CHANGELOG.md` says what changed. Its `## Unreleased` section collects what
  the next version brings, in words for the people who use Elara, grouped as
  they read it (files, notation, playing, editing, the app). Write it when the
  release is prepared, from the commits since the last tag, not in every pull
  request.
- Versions are `major.minor.patch`, numbers only (CMake holds no labels). While
  the major version is 0 every version is a preview: the draft is marked as a
  pre-release.

## Making a release

1. Rehearse: run the `release` workflow by hand on `main` (Actions, release,
   Run workflow). It checks the version, builds and smoke-tests every package
   and shows the notes Unreleased would give, without a draft.
2. Prepare it in a branch:

   ```sh
   git switch -c release/X.Y.Z origin/main
   python scripts/elara.py release prepare X.Y.Z
   ```

   This sets `X.Y.Z` in `CMakeLists.txt` and `vcpkg.json` and turns Unreleased
   into `## X.Y.Z - <today>` under a new, empty Unreleased. With nothing in
   Unreleased it stops and lists the commits since the last release to write
   it from. Read the section, commit it as `Release X.Y.Z` and merge it through
   a pull request once CI is green.
3. Tag the merged commit on `main` and push the tag:

   ```sh
   git switch main && git pull
   git tag -a vX.Y.Z -m "Elara X.Y.Z"
   git push origin vX.Y.Z
   ```

4. The `release` workflow checks that the tag, the sources and the changelog
   name the same version, builds the packages on Windows, macOS and Linux
   (`package.yml`), and creates a draft release with the packages, a
   `SHA256SUMS` file and the changelog's section as its notes.
5. Read the draft, try a package, and publish it from the Releases page.

A tag pushed by mistake drafts a release that nobody needs to publish: delete
the draft and the tag.
