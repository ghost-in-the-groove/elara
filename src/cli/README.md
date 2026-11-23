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
```

`pitch` accepts text (`F#3`, `Bb2`, `Cx4`) or a MIDI number and prints the
spelling, MIDI number and equal-tempered frequency. `--tuning` takes a preset
name from `tunings` or open strings from the lowest tab line, e.g.
`"D2 A2 D3 G3 B3 E4"`. Durations are written as in `rests`: `4` quarter, `8.`
dotted eighth, `8*3:2` triplet eighth, `breve` double whole note.

Run it with `python scripts/elara.py cli -- <args>`. Conventions:

- `--json` prints exactly one JSON object on stdout; human text otherwise.
- Exit code 0 on success (and `--help`), 1 on a runtime error, 2 on bad arguments.
- Errors go to stderr as `error: <message>`.
- New features add a subcommand here *and* a CTest in `tests/cli/`.
- Subcommands are grouped in `src/*_commands.cpp` (audio, theory); each registers
  itself with a callback that stores its exit code in the shared `Context`.

- **May depend on:** every Elara module except `app`. **Must not include:** Qt.
