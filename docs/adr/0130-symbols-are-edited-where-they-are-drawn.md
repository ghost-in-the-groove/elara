# 0130. Symbols are edited where they are drawn

- Status: Accepted
- Date: 2026-11-27

## Context

A time signature, a key signature, a clef, a tempo, the name of a section, a
chord symbol and a beat's text are drawn on the page, and each was changed
somewhere else: the first five in the popup of the caret's measure (Ctrl+M or
a right click), the last two in the popups of the caret's beat (Ctrl+K,
Ctrl+T). To change the key signature a system starts with, one had to know
that it belongs to a measure, find the measure it was set at, put the caret
there and open its popup. What is seen is the symbol; what had to be known is
the model.

The display list says what each of its pieces is: a role and a measure. It
does not say which pieces make one symbol, how large the symbol is, or which
staff it stands on.

## Decision

- `notation::symbolAt` says which symbol of a page is drawn at a point: the
  smallest piece under it among the roles asked for, with the pieces of its
  role and measure drawn beside it as one box, and the staff nearest to it in
  its system. It reads the display list and the font's metrics, nothing of
  the song; `elara-cli render --symbol-at` prints what it finds.
- A double click on a symbol edits it. A click still puts the caret under it
  and moves playback there, so a double click does that and then edits.
  Under the pointer, the symbol a double click would edit is lit and the
  pointer becomes a hand; nothing else on the page says it can be edited.
- What is edited is the value in force, at the measure it starts at. A key
  signature or a clef written again where a system starts is the one set
  some measures before: its card changes that one, and makes no new change at
  the system's first measure. The card says from which measure it changes
  the song.
- One symbol, one card (`SymbolCard`): a time signature's has the time
  signature and nothing else. It changes the song through the setters that
  take a measure's number (`setTimeSignatureFrom`, `setKeySignatureFrom`,
  `setTempoFrom`, `setSection`, and `CaretEditor::setClefFrom` for a staff's
  clef), each one undo step, and the caret stays where it was. The measure
  popup keeps everything about the caret's measure, and the keyboard its way
  to all of it.
- A chord symbol and a beat's text are edited in the popups they have: the
  double click puts the caret on their beat and opens the popup.
- Not edited this way: the "TAB" of a tablature and the clef of a drum staff,
  which are not chosen, and a triplet feel mark, which is drawn as a tempo is
  and is not one.

## Consequences

- Seven kinds of symbol are changed where they are seen, without knowing
  which measure holds them. Another kind takes a role in `kEditedRoles`, a
  kind in `ScoreDocument::symbolAt()` and a card or a popup.
- A double click costs a click: the caret and playback go to the symbol
  first.
- The pages show a song that an edit still being laid out is ahead of. A
  symbol found on them is answered only when its measure and its staff are
  in both songs.
- A symbol is picked by the box of its ink and 0.4 staff spaces round it, so
  that a flat or a tempo's note can be hit; of two symbols under the point,
  the one with the smaller piece is taken.
- A triplet feel mark has the role of a tempo mark, so the document tells
  them apart by the measure: one with no tempo change of its own has a feel
  mark and no tempo to edit. On the first measure, which always has a tempo,
  a feel mark opens the tempo's card. A role of its own would end that.
- The gesture is checked with the pointer in the smoke test, on the first
  page's time signature: it is lit under the pointer and a double click opens
  its card. What is found, and at which measure, is tested without a window.
