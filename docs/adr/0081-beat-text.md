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
  are its tempo's name and are kept as text too. Words between beats, and
  words over a measure rest written as one (an empty voice, which has no beat
  to hold them), are reported as dropped (`text directions dropped`).
  Placement below the staff is not kept.

**Writing.** MusicXML writes a beat's text as words in a `<direction
placement="above">` just before its note, on the beat's staff; the native
format writes `"text": "let ring"` on the beat, left out when there is none.
Older files without texts read as before.

## Consequences

- The model, the native format, the Guitar Pro 3-8 importers and MusicXML
  both ways keep beat texts, and `elara-cli dump` and `import --json` show
  them. Drawing them above the staff and typing them at the caret come next,
  each recorded here as it lands.
- Copying, pasting and transposing carry a beat's text with it.
- A text that reads like a jump ("D.C. al Fine") comes back from MusicXML as
  that jump: the format cannot tell them apart.
- Tests: `io.Guitar Pro 3-5: beats keep their text`, `io.Guitar Pro: beats
  keep their free text`, `io.MusicXML: words are the text of the beat they
  start on`, `io.MusicXML: signs and jumps become measure marks`,
  `io.MusicXML: what the model cannot hold yet is reported, not lost
  silently`,
  `io.MusicXML export: beat text comes back as words`, the native round trip
  and `io.song.json uses text forms and omits defaults`.
