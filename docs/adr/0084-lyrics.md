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
Guitar Pro's lines and what is typed in the app: `syllablesOf(text)`
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
  break;
- an underscore after a syllable holds it (Elara's addition: Guitar Pro has
  no extenders), so `"love_ me"` draws a line after "love".

Files written by hand were found to disagree with the manual in places (a
double space inside a line leaving no blank); the beats Guitar Pro dispatched
are what Elara reads from those files, so the rule only places Guitar Pro 3-6
lyrics and typed verses.

**Typing.** A verse is typed whole, as text, not syllable by syllable at the
caret: the text is how songbooks and Guitar Pro users write lyrics, and a
syllable per beat would make every correction a walk along the staff.
`setVerse(song, caret, verse, first, text)` replaces the whole verse of the
caret's staff with `text` placed from measure `first`, as one undo step ("Set
lyrics", "Remove lyrics" when nothing is left), and counts the syllables after
the last note, which are dropped. `verseText(staff, verse)` gives the verse
back as text that places it again as it is, from the measure of its first
syllable: a space or hyphen after each syllable, a space more for each blank
beat, "+" for a space inside a syllable and "_" after a held one. Typing works
on the first voice, where lyrics are placed; syllables on rests or tied notes
read from files are not part of the text and go when the verse is typed
again. A staff sings at most 16 verses, as many as MusicXML numbers in
practice. The app types a verse in a lyrics popup (Ctrl+Shift+L) with a chip
per verse, the first measure and the text; `elara-cli edit` has
`lyrics <verse> <measure> <text>`.

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

**MIDI.** A Standard MIDI File holds one line of lyrics: a Lyric meta event
per syllable at the tick of its note. The MMA's recommended practice for these
events (RP-017) is not freely available, and files disagree, so the reader is
lenient: spaces, carriage returns, line feeds and karaoke's `/` and `\` around
a syllable are not part of it, a hyphen after it, or alone or before the next
one, joins it to the next, and an event left empty sings nothing. A syllable
goes on the note that starts nearest it, at most a 32nd note away, the highest
of a chord; a track's events go to the channel of that track whose notes start
with most of them, or, for a track of words only, to the channel of the whole
file that does (the melody, not the bass under it). Syllables away from any
note, or on a note that already sings, are dropped with a warning. The writer
puts each syllable of a track's first staff that sings in an event right
before the note it starts with: a hyphen after it when the word goes on, a
space when it ends, the text as written (UTF-8). Repeats are written as played
(ADR-0083), so each pass through a repeated measure sings the verse of that
pass, the first verse on the first pass, when the measure has it and the first
verse otherwise; other verses, a second staff's lyrics and extenders are
dropped with a warning.

Many files end each word with a space, or start the next word with one or
with a new line, and leave the syllables of a word bare ("Twin" "kle "). When
most of a track's syllables that no hyphen joins are parted from the next
that way, a syllable that is not joins the next. Karaoke files (.kar, marked
by a text event "@KMIDI KARAOKE FILE") keep their words in text events after
a header of "@" lines: their words are the lyrics unless the file also has
Lyric events, and the first two "@T" lines are the song's title and artist.
Elara does not write .kar files: its MIDI lyrics are Lyric events, which
karaoke players read too.

**Drawing.** Lyrics go right under the staff's notes, where the eye looks
for them, and dynamics and hairpins go under the lyrics. Engraving manuals put
the dynamics of a vocal staff above it instead; Elara keeps them in one place
on every staff, and a vocal line in a songbook seldom has many. Each verse has
its own line, in order, also where only a later verse sings, so a verse is
read along one line. On a staff shown both ways the syllables go under the
standard notation; tablature shown alone has them under its rhythm. A syllable
is centred under its note, in regular type; a held one starts at the notehead,
as engravers align melismas. A hyphen goes between the syllables of a word, one
for every six staff spaces of room, and after the last syllable of a line when
its word goes on in the next (none at the start of that line). A held syllable
has a line at its baseline under the notes it is held over, to the end of the
last one: the notes up to the next syllable of its verse, a rest or sixteen
measures; it runs to the end of the line when they go on, and from the first
note of the next line. A held syllable joined to the next by a hyphen draws the
hyphens across instead. Like chord symbols, syllables keep apart from each
other and from the barlines, with room for their hyphens: the notes under them
spread when they need to (ADR-0080).

**The native format** writes a beat's verses as strings, a hyphen after a
syllable joined to the next, an underscore after one held and `""` for a verse
that sings nothing there: `"lyrics":["be-","","low_"]`. A syllable's text
never ends with a hyphen or an underscore of its own: readers, of song.json and
of MusicXML, fold them into the joining hyphen and the extender.

## Consequences

- Lyrics come through Guitar Pro 4-8 files, MusicXML both ways and the
  native format, and show in `elara-cli import --json` and `dump`.
- Lyrics draw in every layout (lines, pages, strips, SVG, PDF and the app);
  a `lyric` role marks their texts and lines in the display list.
- Verses are typed whole in the app and the CLI.
- MIDI carries the verse each pass sings, so a song with its verses under a
  repeat exports to a karaoke-ready file; read back, it is one verse over the
  measures as played.
- Guitar Pro has no extenders: its lyrics come without them, and a held
  syllable written to MusicXML and read back keeps its line.
- Tests: the `core.Lyrics text: ...` cases (syllables, blanks, line breaks,
  comments, held syllables), `core.A verse reads back as the text that places
  it`, `core.Entry: a verse typed as text replaces the verse of the caret's
  staff`, `cli.edit_types_lyrics`, `cli.dump_lyrics`,
  `cli.edit_removes_lyrics`, the lyrics popup in `app.smoke`, `core.A verse goes on the beats with notes, past rests and tied
  notes`, `core.A verse starts at its measure and replaces what the verse held
  there`, `io.Guitar Pro 4: a band keeps its instruments, drums and tempo
  changes` (the corpus melody sings two verses, with a hyphen, a blank beat, a
  tied note and a word too many), `io.Guitar Pro: beats keep the syllables
  Guitar Pro dispatched to them`, `io.Guitar Pro: lyrics not yet dispatched go
  on the beats with notes`, `io.Guitar Pro: a band keeps its instruments,
  staves, voices and drums` (the clarinet's dispatched syllables), `io.MusicXML:
  lyrics give each beat a syllable per verse`, `io.MusicXML: lyrics without a
  number, on chords, hidden or on grace notes`, `io.MusicXML export: lyrics
  come back with their verses, hyphens and extenders`, the native round trip
  with `io.song.json uses text forms and omits defaults`, the
  `notation.Lyrics: ...` layout cases (placement, held syllables, spacing,
  dynamics, lines that break a word or a melisma, tablature), the render
  snapshots of the corpus band, quartet and piano, `io.MIDI import puts lyric
  events on the notes they start with`, `io.MIDI import gives a lyrics track
  without notes to the melody it sings`, `io.MIDI export sings each pass of a
  repeat with its verse`, `io.MIDI import reads words parted by spaces
  instead of hyphens`, `io.MIDI import reads a karaoke file's words, title and
  artist`, `io.MIDI import leaves the text events of other files alone`,
  `cli.import_karaoke` and the corpus MIDI band snapshots.
