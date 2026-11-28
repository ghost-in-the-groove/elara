# 0170. Following playback, and the caret marks where it starts

- Status: Accepted
- Date: 2026-11-28

## Context

The score view drew the playback cursor whenever playback had a position, and
scrolled after it whenever it left the view. Two things went wrong in use:

- **Following crawled.** While a song plays the cursor moves on every frame
  (the transport is read every 16 ms). Each move that found the cursor out of
  view started the 420 ms scroll again from where the view was, so the scroll
  never got past its first, slowest frames: the view lagged a system or more
  behind the music, and a system taller than a third of the view never came in
  whole. A smoke check that moves playback every tick, as playing does, saw the
  view travel 19 of the 800 pixels it had to in 1.2 s.
- **Two marks for one place.** A click on a beat to write there put the caret
  on it and moved playback to it, so the cursor's line stood over the caret's
  band while nothing played: two marks in two colours for one place, the line
  across every staff of the system.

## Decision

- **The view follows systems, not frames.** When the cursor's system is not
  wholly in view, the view scrolls so that it is, its top a fifth of the view
  down at most (less when the system needs the room), so that the systems
  after it show below; a system taller than the view comes in from its top. A
  scroll under way to the same place goes on; the cursor moving inside its
  system never starts one again.
- **A scroll by hand holds following off** (a drag, a flick, the wheel or a
  scroll bar): the view follows again once the cursor goes into another
  system, and not before 1.5 s after the last scroll by hand. Playback that
  starts, and following turned back on, go to the cursor at once. Following
  can be turned off in the Sound menu ("Follow playback").
- **While playback is stopped, the caret marks where it starts.** A click on
  the score, or Left and Right (with Ctrl, by measure), put the caret there
  and playback with it, and the cursor hides until playback plays; moving
  playback on the song string shows it again. Where playback was paused, the
  cursor stays until the caret is put elsewhere.
- **The measure is shaded across its system**: the one playing while the
  cursor shows, the caret's otherwise. The shade covers every staff the system
  shows, of every track and voice, under the caret's band and cell, so that
  the caret says the beat and the string, and the shade the measure in every
  part.

## Consequences

- What plays from where is one mark when stopped and one moving line, over a
  shaded measure, when playing.
- Behaviour that needs a screen is checked by the smoke test: `--follow` moves
  playback through a song three pages long every tick as playing does, scrolls
  by hand with the wheel and turns following off; the file smoke test clicks
  the score and presses Right with playback stopped and checks the cursor, the
  shade and the transport.
- Following is not remembered from one run to the next; the window state
  (ADR-0137) can take it on if it is turned off often.
