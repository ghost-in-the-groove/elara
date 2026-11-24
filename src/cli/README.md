# elara-cli

Headless command-line front end (ADR-0003). Tests and CI cannot hear audio or
look at a score, so every capability must be observable from here:

```text
elara-cli [--json] version
elara-cli [--json] render-tone --out tone.wav [--frequency 440] [--seconds 1] [--sample-rate 48000]
elara-cli [--json] devices                         # outputs and inputs
elara-cli [--json] tone [--frequency 440] [--seconds 1]
elara-cli [--json] tune <FILE.wav> [--a4 440] [--tuning guitar]   # the tuner's reading of a recording
elara-cli [--json] listen [--seconds 5] [--a4 440] [--tuning guitar] # tune from the default input
elara-cli [--json] pitch <PITCH|MIDI> [--flats] [--a4 440]
elara-cli [--json] fret --string N --fret N [--tuning guitar] [--capo 0] [--flats]
elara-cli [--json] positions <PITCH> [--tuning guitar] [--capo 0] [--max-fret 30]
elara-cli [--json] tunings
elara-cli [--json] key <KEY>                       # e.g. "D major", "Bb minor"
elara-cli [--json] spell <MIDI> [--key "C major"]
elara-cli [--json] meter <N/D> [--bpm 120] [--beat-unit quarter|dotted-quarter|eighth|...]
elara-cli [--json] rests <N/D> <DURATION>...      # e.g. rests 6/8 8 -> 8 8 4.
elara-cli [--json] new <FILE> [--title T] [--track PRESET]... [--measures 4] [--time 4/4]
                       [--key "C major"] [--tempo 120] [--force]
elara-cli [--json] dump <FILE>                    # with --json: the whole model
elara-cli [--json] import <FILE> [--out FILE.elara] [--force]  # .musicxml, .xml, .mxl, .mid, .midi, .smf, .gp, .gpx, .gp5, .gp4, .gp3
elara-cli [--json] export <FILE.elara> <OUT> [--force]        # .musicxml, .xml, .mxl, .mid, .midi
elara-cli [--json] edit [--in FILE] [--out FILE] <STEP>...   # undoable edits
elara-cli [--json] font [--metadata FILE] [--glyph NAME]      # the notation font
elara-cli [--json] sequence <FILE> [--notes] [--events] [--speed 100] [--metronome]
                             [--count-in-at SECONDS]
elara-cli [--json] render-audio <FILE> [--out FILE.wav] [--sample-rate 48000] [--tail 2]
                                 [--events] [--speed 100] [--metronome]
elara-cli [--json] play <FILE> [--from 0] [--seconds S] [--speed 100] [--metronome]
                         [--count-in] [--loop FROM TO]
elara-cli [--json] render <FILE> [--track 0] [--staff 0 | --tracks all|0,2] [--first 0]
                           [--count N] [--view standard|tab|both] [--layout line|page|scroll|strips]
                           [--paper a4|letter] [--staff-space 1.75] [--margin 15] [--width W]
                           [--measures-per-system N] [--no-credits] [--svg OUT]
                           [--cursor-at SECONDS] [--time-at PAGE X Y]
```

`pitch` accepts text (`F#3`, `Bb2`, `Cx4`) or a MIDI number and prints the
spelling, MIDI number and equal-tempered frequency. `--tuning` takes a preset
name from `tunings` or open strings from the lowest tab line, e.g.
`"D2 A2 D3 G3 B3 E4"`. Durations are written as in `rests`: `4` quarter, `8.`
dotted eighth, `8*3:2` triplet eighth, `breve` double whole note.

`tune` reads a WAV file (PCM or float, channels mixed to mono), detects the
pitch in windows half a window apart (ADR-0028) and reports the median of the
windows that have a clear one: `{"file", "sampleRate", "windows", "pitched",
"a4", "frequency", "note", "cents", "clarity"}`, and with `--tuning` also
`"tuning", "string", "stringPitch", "stringCents"` for the nearest open string.
It fails when no window has a clear pitch. `listen` opens the default audio
input (ADR-0029) and prints each change of the tuner's reading as it happens;
with `--json`, at the end: `{"device", "sampleRate", "seconds", "peak",
"dropped", "readings"}`, each reading `{"time", "note", "cents",
"frequency"}` (plus `"string", "stringCents"` with `--tuning`) or `{"time",
"note": null}` when the note stops. `devices` lists `"outputs"` and `"inputs"`.

`new` creates a `.elara` song (ADR-0006) and refuses to replace a file without
`--force`. `dump --json` prints `{"file", "song", "durationSeconds", "issues"}`
where `song` is the file's complete `song.json`: the model snapshot later tests
compare against. `import --json` prints `{"file", "format", "out", "warnings",
"song", "durationSeconds", "issues"}`, with `format` `musicxml`, `midi` or `guitarpro`:
`warnings` lists what the file had that the model cannot hold yet (ADR-0007,
ADR-0009); `--out` also saves the song as `.elara`. `export --json` writes
MusicXML (compressed for `.mxl`) or MIDI and prints `{"file", "out", "format",
"compressed", "bytes", "warnings"}`, where `warnings` lists what the format
could not hold (ADR-0008, ADR-0009).

`font` shows what the notation engine reads from the SMuFL font (ADR-0011),
by default `fonts/Bravura.json` next to `elara-cli`: `--json` prints `{"name",
"version", "metadata", "glyphs", "described", "missing", "defaults"}`, and
with `--glyph noteheadBlack` one glyph's `{"glyph", "codepoint", "bbox",
"advanceWidth", "anchors"}` in staff spaces, y up.

`render` lays out one staff of a `.elara` file or of any file `import` reads,
as the notation engine does (ADR-0012, ADR-0013), from measure `--first`
(0-based) for `--count` measures: in standard notation, as tablature
(`--view tab`, fretted staves only) or both, notation above tablature.
`--tracks` takes whole tracks instead (every staff of each, tablature only where
a staff has a tuning). `--layout line` (the default) puts the measures on one
line at their natural spacing; `--layout page` breaks them into systems on
pages of `--paper` with staff spaces `--staff-space` mm apart and `--margin` mm
margins, with the title and credits on the first page (unless `--no-credits`);
`--layout scroll` stacks the systems on one page as long as the music,
`--width` staff spaces wide (ADR-0014); `--layout strips` cuts that page into
strips of whole systems no higher than `--strip-height` staff spaces (the
paper's height by default), one display list and, with `--svg`, one file each
(ADR-0057). `--measures-per-system` fixes how many
measures each system holds. `--json` prints `{"file", "track", "staff",
"first", "count", "svg", "view", "layout", ...}` (`"tracks"` instead of
`"track"` and `"staff"` with `--tracks`) followed, for a line, by the display
list `"width", "top", "bottom", "counts", "systems", "staves", "measures",
"columns", "glyphs", "lines", "beams", "texts", "curves", "polygons"` and, for pages, by
`"pages"`: one display list per page. Lengths are in staff spaces, y down from
the top line of the first staff on a line and from the top of the page on a
page; `counts` are per role (noteheads, fret numbers, bends, measure numbers,
credits...); a glyph drawn at another size than the staff's has a `"scale"`;
`columns` say where the notes starting at each time of a measure
are placed (`measure`, `start` in whole notes, `x`), as the editor finds its
beats (ADR-0032). `--svg OUT` also writes SVG, loading the music font from `fonts/`
next to `elara-cli`; pages go to `OUT-1.svg`, `OUT-2.svg`... when there are
several.

The playback cursor and clicks on the score (the app's transport): with
`--cursor-at SECONDS`, `--json` adds `"cursor": {"seconds", "played",
"measure", "page", "x", "y", "height"}` before the display lists: the measure
played then (`played` counts repeats, `measure` is the written one) and the
line across its system, standing on each column of notes as it starts; `null`
after the end. `--time-at PAGE X Y` (the page from 0, x and y as the cursor
gives them) adds `"timeAt": {"page", "x", "y", "measure", "seconds"}`: where a
click there starts playback, on the column of notes under it the first time
its measure plays; `measure` and `seconds` are `null` away from every measure.

`edit` applies quoted steps to a new song, or to `--in FILE`, through the undo
history, saves the result to `--out FILE` if given, and prints the song, its
validation issues and the undo/redo state:

```text
elara-cli --json edit "add-track electric-guitar" "append-measures 4" \n    "time 2 6/8" "note 0 0 6 3 4" "fill 0 0" undo redo save
```

Steps: `add-track <preset> [at]`, `remove-track <i>`, `insert-measures <at> [n]`,
`append-measures <n>`, `remove-measures <at> [n]`, `time <m> <N/D>`,
`key <m> <key>`, `mix <track> <volume> <pan> [mute] [solo]` (volume 0 to 1,
pan -1 left to 1 right, ADR-0020), `tempo <bpm>`, `tempo-from <m> <bpm>|none`
(ADR-0042), `title <text>`,
`note <track> <m> <string> <fret> <duration>`, `rest <track> <m> <duration>`,
`fill <track> <m>`, `copy <m> <n> [track <t> [<count>]]`,
`paste <m> [track <t>] [insert]`,
`transpose <semitones> [measures <m> <n>] [tracks <t>...]`,
`tuning <track> [<preset>|<notes>] [capo <n>] [staff <s>] [frets]`,
`sign <m> <segno|segno-segno|coda|double-coda|fine|to-coda|to-double-coda>...`
or `sign <m> none`, `jump <m> <da-capo|dal-segno|dal-segno-segno|none>
[al-fine|al-coda|al-double-coda]`, `repeat <m> [start] [end <times>]` or
`repeat <m> none`, `ending <m> <time>...` or `ending <m> none`, `undo`, `redo`,
`save`. A malformed step exits with 2, a step the song rejects
(missing track, nothing to undo) with 1. `time` and `key` change the signature
from measure `m` until the next change of it, as the app does; a step that
changes nothing adds no undo step.

`sign` gives measure `m` exactly the signs and closing marks it lists (`none`
clears them) and `jump` its jump, with what ends the music after it (ADR-0049).
The measures of `edit` and `dump` list them as `"signs":["segno","to-coda"]`,
`"jump":"dal-segno"` and `"jumpThen":"al-coda"`; `sequence` plays them.
`repeat` gives measure `m` exactly a start repeat, an end repeat played
`times` times (2 to 255), both or neither, and `ending` the times through the
repeat that play it (1 to 8); each is one undo step, "Change repeats"
(ADR-0016). The measures list them as `"repeatStart":true`, `"repeatTimes":3`
and `"endings":[1,2]`.

`copy` keeps `n` measures from `m` of every track, or of `count` tracks from
`t` (one when `count` is left out), for later `paste` steps; it is not an
undoable edit. `paste` puts them over the measures from `m` of track `t` and
the tracks after it, appending measures past the end, or into new measures
before `m` with the copied time and key signatures (`insert`). Fretted notes
find strings again on another tuning or capo, and lose their frets on a staff
without one; drums paste only into drum tracks (ADR-0027).

`transpose` moves pitched notes up (or down, when negative) by that many
semitones, over every measure or `n` measures from `m`, of every track or the
listed ones (ADR-0026). Notes keep the degree of their spelling in the key the
interval leads to: C# up a semitone in C major is D. Key signatures move when
every pitched track does; drums never move. A fretted note keeps its string when
its new fret is on the neck, else takes the nearest free string; a note no
string can play rejects the step.

`tuning` gives a fretted staff (the track's first unless `staff`) another
tuning, a preset such as `guitar-drop-d` or open strings from the lowest
(`D2 A2 D3 G3 B3 E4`), and capo (ADR-0046); either left out stays as it is.
Notes keep their pitches and move to frets that play them, as `transpose`
places them; with `frets` they keep their frets and sound what those play
now, spelled in the key. A note the new tuning cannot play rejects the step.
The output gives each track's `tuning` (its first staff's; `null` without
strings) and `capo`.

Keyboard entry steps work at a caret, as the editor's keys do (ADR-0021):
`caret <track> <m> <beat> [string]` places it in the first voice, `voice <1-4>`
takes it to that voice of its measure (typing then goes there), `right`, `left`, `up`, `down`,
`next-measure` and `previous-measure` move it, `fret <n>` types a fret on its
string (adding a beat of the input duration on the free slot after the last
beat of a short measure), `pitch <C..B> [add]` types a note name in the
octave nearest the previous note (with `add`, above the beat's highest note;
on tablature on the nearest string that reaches it, ADR-0034), `shift
<semitones>` moves the caret's note, `duration <d>` sets the input duration and
the caret beat's, `delete` removes the note on the caret's string (on a staff
without strings, the beat's notes; on a rest, the rest), `delete-beat` the
whole beat, and `insert-rest` adds a rest before it. On a percussion staff the
caret's string is a drum lane, from the crash at the top to the hi-hat pedal
(ADR-0035): `drum` toggles the lane's piece on the caret's beat, `drum <key>`
that General MIDI key's lane (moving the caret to it), and `delete` removes the
lane's piece only. `effect <name>` toggles a note effect on the caret's notes
(ADR-0037): `tie`, `legato`, `slide`, `bend`, `vibrato`, `palm-mute`,
`dead-note`, `let-ring`, `harmonic`, `ghost`, `accent` or `staccato`.
`bend <form> [cents]` sets a bend on them (ADR-0055): `bend`, `release` (bend
and release), `pre-bend`, `pre-release` (pre-bend and release) or `none`, up by
`cents` (a full step, 200, by default).
`dynamic <ppp..fff>` sets a dynamic from the caret's beat until the next change
of dynamic, over every voice of the staff (ADR-0047); notes typed later take the
dynamic in force at the caret. The output includes `"caret"` and `"input"`.

`sequence`, `render-audio` and `play` play a song for practice (ADR-0024):
`--speed` from 25 to 200 percent of the written tempo, and `--metronome` clicks
every beat on a track after the song's last. Their JSON says `"speed"` (1 for
the written tempo) and `"metronome"`; `sequence` puts them after
`"duration"`, the others at the end. `sequence --count-in-at SECONDS` adds
`"countIn": {"clicks", "interval"}` after them: the count-in for playback
from there. `play --count-in` clicks it before playing, and `--loop FROM TO`
loops a passage; without `--seconds`, a looped `play` stops after going round
the loop twice.

Run it with `python scripts/elara.py cli -- <args>`. Conventions:

- `--json` prints exactly one JSON object on stdout; human text otherwise.
- Exit code 0 on success (and `--help`), 1 on a runtime error, 2 on bad arguments.
- Errors go to stderr as `error: <message>`.
- Arguments, file paths and output are UTF-8 on every platform. On Windows an
  application manifest (`elara-cli.manifest`) makes UTF-8 the process code page,
  so paths such as `canción-曲.gp` reach the commands intact.
- New features add a subcommand here *and* a CTest in `tests/cli/`.
- Subcommands are grouped in `src/*_commands.cpp` (audio, theory); each registers
  itself with a callback that stores its exit code in the shared `Context`.

- **May depend on:** every Elara module except `app`. **Must not include:** Qt.
