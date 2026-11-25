# 0084. Lyrics

- Status: Accepted
- Date: 2026-11-25

## Context

Songbook files sing: a vocal track, or the melody line of a band, carries its
words under the notes. Every importer dropped them with a warning ("lyrics
dropped"), and among the Guitar Pro files tried while testing, lyrics were the
most frequent loss left. SCOPE listed lyrics after 1.0 with orchestral
notation; for tablature songbooks they are not an orchestral feature, so they
are written early, as PDF export was (ADR-0074).

How the formats keep them:

- Guitar Pro 4 and 5 keep the lyrics of one track: five lines of text, each
  from a measure (1 for the first).
- Guitar Pro 6-8 keep five lines per track, each from a bar (`<Offset>`, 0 for
  the first). Once the lyrics are dispatched (`<Lyrics dispatched="true">`,
  which Guitar Pro 7 and 8 write) each beat holds its syllables, one `<Line>`
  per verse, a hyphen after a syllable that goes on in the next.
- Guitar Pro's manual describes the text: a space or a hyphen changes the
  syllable, "+" keeps two words on one beat, several spaces or hyphens in a row
  leave beats blank, line breaks count as spaces (several as one) and words in
  [brackets] are comments. Dispatched files show that syllables go on the beats
  of the first voice with notes, past rests and beats whose notes are all tied.
- MusicXML writes `<lyric number="n">` per note with `<syllabic>` (single,
  begin, middle, end) and `<extend/>` for a syllable held over several notes.

## Decision

**A beat holds a syllable per verse.** `Beat::lyrics` is a
`SharedOptional<std::vector<Syllable>>`: verse n + 1 at index n. A `Syllable`
is its text, whether a hyphen joins it to the next syllable of its word, and
whether it is held over the notes after it that sing nothing in its verse (an
extender, drawn as a line to the last of them: a melisma). A verse that sings
nothing on the beat has empty text; the list never ends with
one, and a beat that sings nothing holds none, so a beat without lyrics costs a
null pointer, like its chord symbol and text (ADR-0080, ADR-0081). Lyrics do
not sound.

**Verses typed as text** follow Guitar Pro's syntax, so the same code reads
Guitar Pro's lines and, later, what is typed in the app: `syllablesOf(text)`
splits a verse into syllables, and `placeVerse(staff, verse, first, text)`
puts them on the beats of the staff's first voice from a measure on, one per
beat with notes, past rests and beats whose notes are all tied from the beat
before, replacing what that verse held there. The rules, where the manual is
loose:

- the first space or hyphen after a syllable ends it, and a hyphen as that
  first one joins it to the next; every further space or hyphen in the row
  leaves a beat blank, and so does each one before the first syllable;
- a line break separates syllables but leaves no beat blank, whatever spaces
  are next to it count on their own (`"stare \nA"` leaves none,
  `"go  \nnow"` one);
- a comment in brackets, with the spaces around it, separates like a line
  break.

Files written by hand were found to disagree with the manual in places (a
double space inside a line leaving no blank); the beats Guitar Pro dispatched
are what Elara reads from those files, so the rule only places Guitar Pro 3-6
lyrics and typed verses.

**Reading.** Guitar Pro 4-5: the five lines go on the first staff of their
track. Guitar Pro 6-8: the beats' syllables when the lyrics are dispatched,
otherwise the track's lines on its first staff. Syllables left over after the
last note are reported: `lyrics after the last note dropped`.

**MusicXML.** Each `<lyric>` of a note is a verse: `number` 1 to 16 is that
verse, a lyric without one is verse 1, and a name (some programs write
`part1verse1`) takes its place among the names its part uses; verses after the
16th are dropped with a warning. `<syllabic>` `begin` or `middle` joins the
syllable to the next one, `<elision>` joins two texts on one beat with its
symbol (`‿` when the file leaves it to the reader), and `<extend>` holds the
syllable; the `continue` and `stop` extenders on the notes after it add
nothing the model needs. A chord's other notes fill the verses its first note
leaves empty. Hidden lyrics (`print-object="no"`) are not sung, and lyrics on
grace notes are dropped with a warning. Written back, a beat's verses are
`<lyric number>`s on its first note, the syllabic worked out from the hyphens
of each verse in its voice (`middle` or `end` after a syllable joined to the
next, `begin` or `single` otherwise), an extender a plain `<extend/>`, which
readers stretch to the next syllable.

**The native format** writes a beat's verses as strings, a hyphen after a
syllable joined to the next, an underscore after one held and `""` for a verse
that sings nothing there: `"lyrics":["be-","","low_"]`. A syllable's text
never ends with a hyphen or an underscore of its own: readers, of song.json and
of MusicXML, fold them into the joining hyphen and the extender.

## Consequences

- Lyrics come through Guitar Pro 4-8 files, MusicXML both ways and the
  native format, and show in `elara-cli import --json` and `dump`.
- Drawing under the staff and typing verses at the caret are follow-up work;
  until MIDI lyric events are read and written, MIDI keeps warning that
  lyrics are dropped.
- Guitar Pro has no extenders: its lyrics come without them, and a held
  syllable written to MusicXML and read back keeps its line.
- Tests: the `core.Lyrics text: ...` cases (syllables, blanks, line breaks,
  comments), `core.A verse goes on the beats with notes, past rests and tied
  notes`, `core.A verse starts at its measure and replaces what the verse held
  there`, `io.Guitar Pro 4: a band keeps its instruments, drums and tempo
  changes` (the corpus melody sings two verses, with a hyphen, a blank beat, a
  tied note and a word too many), `io.Guitar Pro: beats keep the syllables
  Guitar Pro dispatched to them`, `io.Guitar Pro: lyrics not yet dispatched go
  on the beats with notes`, `io.Guitar Pro: a band keeps its instruments,
  staves, voices and drums` (the clarinet's dispatched syllables), `io.MusicXML:
  lyrics give each beat a syllable per verse`, `io.MusicXML: lyrics without a
  number, on chords, hidden or on grace notes`, `io.MusicXML export: lyrics
  come back with their verses, hyphens and extenders` and the native round trip
  with `io.song.json uses text forms and omits defaults`.
