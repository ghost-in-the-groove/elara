# 0181. The smoke test checks the window

- Status: Accepted
- Date: 2026-11-28

## Context

`app.smoke` and its variants run `src/app/src/startup.cpp` inside the
window: a queue of steps, each waiting for a layout and a paint before the
next. Over time most new behaviour got a step there, including behaviour of
`ScoreDocument` and `CaretEditor` that never touches the QML tree: a fret
typed, a tuplet, a voice, a drum lane, then undone. Such a check failed as
"smoke-test: FAILED (entry check failed: ...)", naming neither the class at
fault nor the song; it ran only with a window, once per variant of the
smoke test (a dozen songs and window sizes); and every step changed the
layout count the variants assert.

## Decision

- **The window's checks stay in the smoke test**: what needs the QML tree,
  such as items found by name, clicks, keys, popups and panels, painting, the
  window's state and its QML warnings.
- **The document's checks are Catch2 tests** in `tests/app`, named `app.*`,
  through the classes' own interface. `tests/app/songs_to_edit.hpp` opens
  each song the smoke tests edit (a new guitar song, `band.gp`,
  `quartet.gp4`, `guitar-techniques.gp`) as the window shows it and waits
  for each edit's layout as the window does, checking then that playback
  plays the song the pages show (ADR-0031). A test ends by undoing its edits
  and comparing the song with the one opened, not only by `modified()`.
- The steps move a group at a time; each group's tests land with the
  removal of its steps.

## Consequences

- A failing check names the test, the song (`INFO`) and the expression, and
  runs without a window.
- The layout counts the smoke variants assert drop with each group moved.
- A step that only exercised a panel's bindings while the song changed (the
  measure popup, the song panel, the note effects palette, the lyrics popup)
  keeps a lean driver in the smoke test, without the document's checks.
