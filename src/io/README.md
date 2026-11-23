# elara_io

File formats: reading and writing songs. Everything here converts between bytes
and the `elara_core` model; nothing is stored in this module.

| Header | What it gives you |
|---|---|
| `result.hpp` | `FormatError` (why a file could not be read), `ImportResult` (a song plus the warnings of what a foreign format had that the model cannot hold) and `ExportResult` (a file's content plus the warnings of what the format cannot hold). |
| `musicxml.hpp` | MusicXML import (ADR-0007): `importMusicXml()` for text, `importMusicXmlBytes()` for text or compressed `.mxl`, `importMusicXmlFile()`. MusicXML export (ADR-0008): `exportMusicXml()` gives the text or, with `compressed`, an `.mxl` archive; `exportMusicXmlFile()` picks by extension and writes atomically. Large documents are read and written one part per thread. |
| `native_format.hpp` | The native `.elara` format (ADR-0006): `saveNative()`/`loadNative()` for files (atomic save), `encodeNative()`/`decodeNative()` for the zip bytes, `songToJson()`/`songFromJson()` for the `song.json` inside. Errors come back as `FormatError` with the JSON path of the problem. |

The `.elara` file is a zip archive with a `mimetype` entry and a `song.json`
that uses the model's names and text forms (`"8."`, `"F#3"`, `"6/8"`) and omits
defaults. Unzip a song to read or diff it. Any model change needs a matching
change here and in the round-trip test (`tests/io/test_native_format.cpp`).

Importers are implemented from public documentation only (CONTRIBUTING.md,
golden rule 6) and tested against the self-made corpus in `tests/corpus/`
(`SOURCES.md` lists every file). Guitar Pro and MIDI come next in milestone 2.
`src/archive.hpp` holds the zip and file helpers (including atomic writes)
the formats share, `src/warnings.hpp` the warning list, and
`src/musicxml_shared.hpp` the MusicXML vocabulary both directions use. Every
exporter is tested by importing its output back (`tests/io/test_musicxml_export.cpp`).

- **May depend on:** `elara_core`, the standard library, yyjson, miniz and
  pugixml (private: they never appear in public headers).
- **Must not include:** Qt, JUCE.

Public headers live in `include/elara/io/`, sources in `src/`, tests in
`tests/io/`.
