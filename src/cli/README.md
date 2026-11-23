# elara-cli

Headless command-line front end (ADR-0003). Tests and CI cannot hear audio or
look at a score, so every capability must be observable from here:

```text
elara-cli [--json] version
elara-cli [--json] render-tone --out tone.wav [--frequency 440] [--seconds 1] [--sample-rate 48000]
elara-cli [--json] devices
elara-cli [--json] tone [--frequency 440] [--seconds 1]
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
```

`pitch` accepts text (`F#3`, `Bb2`, `Cx4`) or a MIDI number and prints the
spelling, MIDI number and equal-tempered frequency. `--tuning` takes a preset
name from `tunings` or open strings from the lowest tab line, e.g.
`"D2 A2 D3 G3 B3 E4"`. Durations are written as in `rests`: `4` quarter, `8.`
dotted eighth, `8*3:2` triplet eighth, `breve` double whole note.

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

`edit` applies quoted steps to a new song, or to `--in FILE`, through the undo
history, saves the result to `--out FILE` if given, and prints the song, its
validation issues and the undo/redo state:

```text
elara-cli --json edit "add-track electric-guitar" "append-measures 4" \n    "time 2 6/8" "note 0 0 6 3 4" "fill 0 0" undo redo save
```

Steps: `add-track <preset> [at]`, `remove-track <i>`, `insert-measures <at> [n]`,
`append-measures <n>`, `remove-measures <at> [n]`, `time <m> <N/D>`,
`key <m> <key>`, `tempo <bpm>`, `title <text>`,
`note <track> <m> <string> <fret> <duration>`, `rest <track> <m> <duration>`,
`fill <track> <m>`, `undo`, `redo`, `save`. A malformed step exits with 2, a
step the song rejects (missing track, nothing to undo) with 1.

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
