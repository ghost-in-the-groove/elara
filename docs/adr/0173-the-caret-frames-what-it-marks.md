# 0173. The caret frames what it marks, and the cursor hides while the score is worked on

- Status: Accepted
- Date: 2026-11-28

## Context

Three things on the score got in the way of reading and writing it:

- **The caret's cell covered the note it marked.** On tablature the cell is as
  high as the room between two strings, 1.5 staff spaces, and a fret number is
  1.3 high. The cell was filled with the accent and outlined with a line of up
  to three pixels drawn inside it, so the fill lay over the number and the
  line cut through its top and bottom: the one number being typed was the
  hardest to read.
- **The playback cursor stood over the score while it was edited.** ADR-0170
  hid it once a click or Left and Right put the caret somewhere. Up and Down,
  a typed fret, a note effect or an undo left it standing: a line across every
  staff that says nothing while nothing plays. It was also drawn in the accent
  with a glow five times its width, heavy beside the caret's band, and on the
  dark page (ADR-0114) the accent is too dim for a line.
- **A song opened with its title cut off.** The zoom keeps the middle of the
  view in place. A song opens at the top and is then zoomed to fit its page
  (ADR-0133), so the top of the page left the view by a quarter of its
  height; the same happened when the window changed size with the view at the
  top.

## Decision

- **The caret's cell is a frame.** It has no fill, and its line is as thin as
  the room between a fret number and the numbers on the strings next to it
  allows: 0.14 staff spaces, two pixels at most and one at least, half of it
  outside the cell. The band that says the beat stays a wash behind it.
- **Lines over the page take `markColor`**: the accent on the white page, the
  lighter accent for text (`Theme.accentInk`) on the dark page. The caret's
  frame and the playback cursor use it.
- **The playback cursor is a thin line with a head**: 0.2 staff spaces wide,
  2.5 pixels at most, with a small rounded head at its top that tells it from
  a barline and from the caret, and no glow. The shaded measure (ADR-0170)
  says where it is from afar.
- **Touching the score while playback is stopped hides the cursor.** The keys
  that move the caret or edit at it, and the commands that do (`reveal()`),
  take playback to the caret's beat and hide the cursor, as a click does. An
  edit made elsewhere (a panel, an undo, a redo: `ScoreDocument::scoreEdited`)
  hides it and leaves playback where it is. Changes that only sound, as a
  fader's, leave the cursor alone.
- **Playback moved by anything else shows the cursor again**: the song string,
  the transport, a loop mark. The view remembers where playback stood when it
  hid the cursor (`parkedAt`) and shows it once playback is elsewhere, so no
  caller has to say so.
- **A zoom that starts with the view at its top keeps it there** (`heldAtTop`):
  a song that opens and a window that changes size show the first page from
  its top. Anywhere else the middle of the view stays in place, as before.

## Consequences

- While the score is written, one mark says where: the caret. The cursor is
  what moves while the song plays, and where it was paused until the score is
  touched.
- Playback starts at the caret after any key that moved it or edited at it,
  not only after Left and Right.
- The smoke test checks the frame (no fill, a line of two pixels at most),
  that a song opens at its top, that the transport shows the cursor again, that
  a key and an edit hide it, and where each leaves playback.
