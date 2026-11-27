# 0137. The window remembers its state

- Status: Accepted
- Date: 2026-11-27

## Context

Every start opened the window at 1280 by 860 wherever the system put it, the
note effects palette open, the song panel closed, the page fitted across the
view and dark in the dark theme, in page view. Someone who works with the
window maximized on a second screen, the palette closed and the score at
175 % set all of that again at each start. Only the recent scores (ADR-0071)
and the sound choices were kept.

Checks and screenshots must leave the settings of the person using the app
alone (`rememberSettings: false`), yet what is kept has to be proved by a
test that runs the app twice.

## Decision

- `WindowState` (`src/app/src/window_state.hpp`, in `elara_app_view`) keeps
  the window's place and size, whether it was maximized, the note effects
  palette, the song panel, the zoom (what it follows, "page", "width" or a
  level chosen by hand), the dark page and the continuous view, in the
  settings' `window` group. It reads them when `remember` turns on and writes
  them as the app quits (`QCoreApplication::aboutToQuit`).
- The place and size are followed in C++ from the window itself: kept once
  they have been still for 250 ms and only while the window is neither
  maximized nor full screen. Maximizing moves and resizes the window before it
  says it is maximized, so the wait keeps the maximized size from replacing
  the window's own; a window that comes back maximized un-maximizes to the
  place it had.
- A window comes back where it was while all of it is still on the screens
  there are. Otherwise (a screen unplugged, a smaller resolution) it opens in
  the middle of the screen that holds most of it, or the primary one, no
  larger than that screen with room for its frame (`WindowState::placed()`).
- The window restores the panels, the zoom, the page and the view at start,
  then binds `WindowState` to them. What the command line asks for wins
  (`--song-pane`, `--tuner`). The tuner is not kept: it opens the microphone,
  which a start should not do unasked.
- A song opens at the zoom chosen last, within a run too: its page across the
  view by default (ADR-0133), its width after "Fit the page width", or the
  level chosen by hand, kept instead of fitted again.
- `--window-state <file>` keeps the state in an INI file instead, also during
  checks. `app.smoke_window_state_keep` changes the window and quits;
  `app.smoke_window_state` starts again on the same file and finds it as it
  was left.

## Consequences

- The window opens as it was closed, on the screen it was on.
- A zoom chosen by hand no longer resets when another song opens; Ctrl+0 or
  the status bar's fit gives the page width back.
- A crash keeps the state of the run before, since it is written as the app
  quits.
- Mixed-scale screens on Windows are placed through Qt's own conversion of
  the saved geometry; the tests run on one offscreen screen.
