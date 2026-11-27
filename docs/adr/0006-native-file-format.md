# 0006. Native file format

- Status: Accepted
- Date: 2026-11-23

## Context

Elara needs its own file format before anything else can be saved: imported
Guitar Pro or MusicXML files are converted into the model (ADR-0004) and saved
as Elara songs. The format must store everything the model holds, survive
future versions of the model, be inspectable when debugging, stay small for
large scores and load quickly. Later it will also carry attachments (audio
tracks, images).

`elara_core` may only use the standard library, so encoding and compression
cannot live there.

## Decision

**Container.** A `.elara` file is a zip archive (like `.mscz` and `.gp`):

| Entry | Content |
|---|---|
| `mimetype` | `application/vnd.elara.song+zip`, stored uncompressed as the first entry so tools can identify the file from its first bytes |
| `song.json` | The song, deflate-compressed |

Further entries (attachments, a manifest) can be added without breaking
readers, which ignore entries they do not know.

**`song.json`** is one JSON object:

- `format: "elara"` and `formatVersion: 1`. A reader refuses a newer
  `formatVersion` with a clear message; within a version, unknown keys are
  ignored so newer writers can add optional data.
- Keys use the model's names (`timeSignature`, `tiedFromPrevious`); values use
  the model's text forms where they exist: durations `"8."` and `"16*3:2"`,
  pitches `"F#3"`, tunings `"D2 A2 D3 G3 B3 E4"`, meters `"6/8"`, keys
  `"Bb minor"`. Enumerations are kebab-case strings (`"treble-8vb"`,
  `"from-below"`), never numbers.
- Defaults are omitted: a rest is `{"duration":"4"}`, a plain note has no
  `effects`, empty trailing voices and measures are left out. Files stay small
  and a field added later has an obvious meaning when absent.
- Layout: song `info`, start `tempo`, one entry per measure in `measures`
  (time signature, key, tempo changes, `repeatStart`, `repeatTimes` and, in
  `alternateEndings`, the times through a repeat such as `[1,2]`; ADR-0016),
  then `tracks` with their `instrument` and `staves`; a staff's `measures` is
  an array of measures, a measure an array of voices, a voice an array of
  beats.

**Libraries**, both MIT-licensed C libraries from vcpkg: **yyjson** (JSON read
and write, reading only since ADR-0147; one of the fastest parsers, small, no
exceptions) and **miniz** (zip and deflate in one source file, no zlib
dependency).

**Module.** The format lives in `elara_io` with the importers. Errors are
returned as `std::expected<Song, FormatError>` whose message names the JSON
path (`tracks[0].staves[0].measures[3][0][2].notes[0].pitch: not a pitch`).

**Safety.** Saving writes a temporary file next to the target and renames it
over the target, so a crash never leaves a half-written song. Loading checks
the declared size of `song.json` before inflating it (zip bombs) and validates
every value through the model's own parsers and constructors.

## Consequences

- Unzipping a song gives readable JSON, useful for debugging and for tests.
- Every model change needs a matching change in the writer and reader, and the
  round-trip test (`Song ==` after save and load) must cover the new data.
- Breaking changes to the JSON require `formatVersion: 2` and keeping a reader
  for version 1.
- yyjson builds a DOM, so a song is held twice in memory while it is saved or
  loaded; acceptable for scores (tens of MB at most), revisit for audio. Since
  ADR-0147 saving builds none.
