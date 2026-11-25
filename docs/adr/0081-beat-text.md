# 0081. Beat text

- Status: Accepted
- Date: 2026-11-25

## Context

Tablature and scores write short texts above the music at a point in time:
"let it ring", "ad lib.", "solo", "dolce", "Ped. ad lib.", a cue for the band.
After chord symbols (ADR-0080) they were the most frequent loss when importing
songs: songbook files carry dozens of them and every importer dropped them
with a warning.

- Guitar Pro 3-5 files store a text per beat, a string after the beat's chord.
- Guitar Pro 6-8 files write `<FreeText>` in a `<Beat>`.
- MusicXML writes `<direction>` elements with `<words>`, before the note they
  belong to (after an `<offset>`). Words also spell jumps and signs ("D.S. al
  Coda", "To Coda", "Fine"), which Elara reads as measure marks (ADR-0049), and
  they name tempos next to a `<metronome>` ("Allegro").

## Decision

**A text belongs to a beat.** `Beat::text` holds at most one string, written
above the beat as the file wrote it. Like a chord symbol it is a
`SharedOptional<std::string>`: a beat without text costs a null pointer, and
copying a beat copies a reference count. A text does not sound: playback,
audio rendering and MIDI export ignore it, and a beat with a text and no notes
is a rest.

**Reading.**

- Guitar Pro 3-5 and 6-8: the beat's text as written; an empty one is none.
- MusicXML: the words of a `<direction>` that do not spell a jump or a sign go
  on the beat of its staff that starts where the direction does, the voices
  looked at in order, as chord symbols do. The words of one direction join as
  written (runs of differently formatted text keep the spaces between them),
  and two directions on one beat join with a space. Words next to a metronome
  are its tempo's name and are kept as text too. Words between beats split
  the beat sounding across them into tied beats (ADR-0096); where none can,
  and over a measure rest written as one (an empty voice, which has no beat
  to hold them), they are reported as dropped (`text directions dropped`).
  Placement below the staff is not kept.

**Writing.** MusicXML writes a beat's text as words in a `<direction
placement="above">` just before its note, on the beat's staff; the native
format writes `"text": "let ring"` on the beat, left out when there is none.
Older files without texts read as before.

**Drawing.** A beat's text is written in italics above its staff, starting
at its beat's column (the left of its notehead), over everything else of the
staff: the chord symbols, the technique lanes and the notes. Texts take no
room between the columns, so they never move the notes: one that would run
into the text before it on its row (closer than a staff space) goes on the
row above, and the next that fits goes back to the lowest row. Like chord
symbols they are drawn once, over the standard notation of their staff when
the system shows it, otherwise over its tablature, and a text on a beat
where no notes start gets a column of its own. The display list's role for
them is `beatText`.

**Editing.** `setBeatText()` writes a text above the caret's beat, a rest as
well as a note, as one undo step, its surrounding whitespace dropped; an
empty text takes it away. The command line types `text <words>` and `text
none`. In the app, Ctrl+T or the entry bar's text button opens a popup under
the caret's beat, the one chord symbols use: words typed and Enter, or one of
the texts the song already has, set it; the bin takes it away. The caret's
status shows the text in quotes.

## Consequences

- The model, the native format, the Guitar Pro 3-8 importers and MusicXML
  both ways keep beat texts, layout draws them, the caret types them, and
  `elara-cli dump` and `import --json` show them.
- A text longer than the room before the line's end runs past it, and a
  line with texts on several rows is taller by each row (about 1.5 staff
  spaces).
- Copying, pasting and transposing carry a beat's text with it.
- A text that reads like a jump ("D.C. al Fine") comes back from MusicXML as
  that jump: the format cannot tell them apart.
- Tests: `io.Guitar Pro 3-5: beats keep their text`, `io.Guitar Pro: beats
  keep their free text`, `io.MusicXML: words are the text of the beat they
  start on`, `io.MusicXML: signs and jumps become measure marks`,
  `io.MusicXML: what the model cannot hold yet is reported, not lost
  silently`,
  `io.MusicXML export: beat text comes back as words`, the native round trip,
  `io.song.json uses text forms and omits defaults`, the `notation.Beat text:
  ...` layout tests (place, above chord symbols, rows, rests and views), the
  render snapshots of the corpus, `core.Entry: beat text is written above the
  caret's beat`, `cli.edit_writes_text` with `cli.dump_text`, and the app's
  smoke test, which writes a text and takes it away.
