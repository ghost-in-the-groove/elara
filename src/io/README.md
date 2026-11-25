# elara_io

File formats: reading and writing songs. Everything here converts between bytes
and the `elara_core` model; nothing is stored in this module.

| Header | What it gives you |
|---|---|
| `result.hpp` | `FormatError` (why a file could not be read), `ImportResult` (a song plus the warnings of what a foreign format had that the model cannot hold) and `ExportResult` (a file's content plus the warnings of what the format cannot hold). |
| `guitar_pro.hpp` | Guitar Pro files (ADR-0010): `importGuitarPro()`/`importGuitarProFile()` read Guitar Pro 7-8 (`.gp`), 6 (`.gpx`) and 3-5 (`.gp5`, `.gp4`, `.gp3`) with tunings (not the default one Guitar Pro gives flutes, pianos and other instruments it shows without tablature), capo, fret positions, staves, voices and techniques; `importGpif()` reads the GPIF score inside Guitar Pro 6-8 files. A Guitar Pro 3-5 keyboard whose notes reach into both clefs gets a grand staff split at middle C (ADR-0075). A Guitar Pro 6-8 arpeggio becomes a strum: rolled upwards, it sounds from the lowest note as a downstroke does (ADR-0025), and keyboards draw it as an arpeggio line (ADR-0076). Guitar Pro 6-8 hairpins are kept on their beats (ADR-0078). Grace notes, as Guitar Pro 6-8 beats or as Guitar Pro 3-5 notes' effects, become the graces of the beat they lead into (ADR-0079). Chord names and diagrams, per beat in Guitar Pro 3-5 and from a staff's (or a Guitar Pro 6 track's) diagram collection in 6-8, become the beats' chord symbols, the beats that name one diagram sharing one symbol (ADR-0080). Beat texts become the beats' text (ADR-0081). Tenuto flags become the notes' tenuto (ADR-0088). The fingers of both hands of a Guitar Pro 3-5 note become its fingering and plucking finger (ADR-0093). A beat's `Ottavia` becomes its octave line (ADR-0089). Trills keep their other note, and their speed from Guitar Pro 3-5 (ADR-0090). A Guitar Pro 6-8 note's `Ornament` becomes its mordent or turn (ADR-0091). Lyrics become the beats' syllables: those Guitar Pro 7-8 dispatched to the beats, or the lines of text placed on the track's first staff (ADR-0084). A Guitar Pro 6-8 bar whose clef differs from the bar before changes the staff's clef, in the staff's octave (ADR-0087). A Guitar Pro 6-8 simile mark makes the bar a measure repeat of the bar or two bars before, dropping the rest it holds (ADR-0102). Large GPIF scores are read one staff per thread. |
| `midi.hpp` | Standard MIDI Files (ADR-0009): `importMidi()`/`importMidiFile()` infer notation (rhythm grids per beat, exact for files from notation programs and fitted for played music; voices, ties, tuplets, spelling in the key; velocities as dynamics, played ones as a dynamic per measure with accents and ghost notes, ADR-0045; Lyric events on the notes they start with, and the words, title and artist of karaoke .kar files, ADR-0084); `exportMidi()`/`exportMidiFile()` write a format 1 file from the song's sounding notes, with repeats and jumps written as played (ADR-0083) and a Lyric event per syllable, each pass of a repeat singing its verse (ADR-0084). The sustain pedal is controller 64 both ways: imported, it goes on the beats it holds of the lowest staff with notes, a pedal lifted and pressed again around a beat becoming a change there (ADR-0092). |
| `musicxml.hpp` | MusicXML import (ADR-0007): `importMusicXml()` for text, `importMusicXmlBytes()` for text or compressed `.mxl`, `importMusicXmlFile()`. MusicXML export (ADR-0008): `exportMusicXml()` gives the text or, with `compressed`, an `.mxl` archive; `exportMusicXmlFile()` picks by extension and writes atomically. Wedges are hairpins both ways (ADR-0078). Grace notes go both ways: slashed ones before the beat, the others on it (ADR-0079). Chord symbols are `<harmony>` both ways, their names spelt as root, kind and bass and their diagrams as frames (ADR-0080). A direction's words are the text of the beat they start on, and a beat's text is written as words above its staff (ADR-0081). A chord symbol or words between beats split the beat sounding across them into tied beats so that they have one to go on, and over a measure rest written as one they write it out as a rest (ADR-0096). Lyrics are `<lyric>`s both ways: a verse per number, the syllabic worked out from the hyphens, extenders kept (ADR-0084). Fermatas on notes and rests are the beats' both ways, short, normal or long by their shape (ADR-0085). Tenuto marks go both ways, `<detached-legato>` read as tenuto and staccato (ADR-0088). Staccatissimo marks go both ways (ADR-0094). Breath marks and caesuras go both ways as the break after their beat (ADR-0095). Octave shifts of one and two octaves become the octave lines of the beats under them on every voice of their staff, and are written back once per staff (ADR-0089). Trill marks go both ways, to the next step of the key unless an accidental mark says otherwise, the wavy line after one being the trill's (ADR-0090). Mordents, inverted (upper) mordents, turns and inverted turns go both ways (ADR-0091). Fingerings and plucking fingers go both ways, a fingering read by the part's kind of instrument; a second fingering of a note warns (ADR-0093). Pedal marks become the sustain pedal of the beats under them on every voice of their staff, and are written back once per staff as "Ped." and a line; sostenuto marks warn (ADR-0092). Slurs go both ways: the beats from a slur's start to the one before its stop are slurred to the next, and each run of them is written as a slur numbered by staff and voice (ADR-0086). Clef changes go both ways as `<clef>` in a measure's `<attributes>`; one in the middle of a measure moves to the start of the next (ADR-0087). Large documents are read and written one part per thread. |
| `native_format.hpp` | The native `.elara` format (ADR-0006): `saveNative()`/`loadNative()` for files (atomic save), `encodeNative()`/`decodeNative()` for the zip bytes, `songToJson()`/`songFromJson()` for the `song.json` inside. Beats that name equal chord symbols share one once read (ADR-0080). A staff's `"clefs"` lists the measures that change its clef (ADR-0087), its `"repeats"` the measure repeats (ADR-0102). A note's `"trill"` keeps its semitones and speed (ADR-0090), its `"ornament"` a mordent or a turn (ADR-0091), its `"fingering"` and `"pluck"` the fingers that play and pluck it (ADR-0093). A beat's `"pedal"` keeps the sustain pedal down or changed under it (ADR-0092), its `"breath"` a breath mark or a caesura after it (ADR-0095). Errors come back as `FormatError` with the JSON path of the problem. |
| `compression.hpp` | `zlibCompress()` and `zlibDecompress()` (with a size limit): zlib streams through miniz, for the PDF writer's streams (ADR-0074). |
| `open.hpp` | `openFile()`: any file Elara reads, chosen by its extension in any case (`.elara`, Guitar Pro, MusicXML, MIDI); `openExtensions()` lists them. The CLI and the app open files through it. |

The `.elara` file is a zip archive with a `mimetype` entry and a `song.json`
that uses the model's names and text forms (`"8."`, `"F#3"`, `"6/8"`) and omits
defaults. Unzip a song to read or diff it. Any model change needs a matching
change here and in the round-trip test (`tests/io/test_native_format.cpp`).

Every importer reads each track's starting volume, pan and, where the format
has them, mute and solo into its `TrackMix`; MIDI and MusicXML export write the
volume and pan back (ADR-0020).

Signs and jumps (ADR-0049) come from Guitar Pro's directions (`src/gp_directions.hpp`
maps all nineteen for both Guitar Pro readers) and from MusicXML's `<sound>`
attributes, `<segno/>` and `<coda/>` glyphs and navigation texts; MusicXML export
writes them back. Sections (ADR-0062) come from Guitar Pro's markers and
section marks, MusicXML's rehearsal marks and MIDI markers; both exports write
them back.

Importers are implemented from public documentation only (CONTRIBUTING.md,
golden rule 6) and tested against the self-made corpus in `tests/corpus/`
(`SOURCES.md` lists every file).
`src/archive.hpp` holds the zip and file helpers (including atomic writes)
the formats share, `src/warnings.hpp` the warning list, and
`src/musicxml_shared.hpp` the MusicXML vocabulary both directions use; `src/smf.hpp`
reads and writes Standard MIDI File chunks and events, and `src/midi_shared.hpp`
holds what the MIDI reader and writer agree on. Every exporter is tested by
importing its output back (`tests/io/test_musicxml_export.cpp`, `tests/io/test_midi.cpp`).

- **May depend on:** `elara_core`, the standard library, yyjson, miniz and
  pugixml (private: they never appear in public headers).
- **Must not include:** Qt, JUCE.

Public headers live in `include/elara/io/`, sources in `src/`, tests in
`tests/io/`.
