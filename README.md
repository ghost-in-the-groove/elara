# Elara

A free and open-source guitar tablature editor, score editor and (later) music
studio for Windows, macOS and Linux.

> **Status:** alpha, milestone 3. Elara opens Guitar Pro, MusicXML, MIDI and its
> own files and shows them as notation, tablature or both, page by page; playback
> and editing come next. See [docs/roadmap.md](docs/roadmap.md).

## Highlights (planned for 1.0)

- Tablature and standard notation, always in sync, for guitar, bass and other
  fretted instruments in any tuning; piano and single-staff instruments.
- Opens Guitar Pro 3-8 files; imports and exports MusicXML and MIDI.
- Realistic guitar articulations: bends, slides, legato, palm mute, harmonics.
- Practice tools: metronome, A-B loop, speed trainer, tuner.

## Building

Requirements: CMake 3.28+, Ninja, a C++23 compiler (MSVC 2022, GCC 13+, Clang 17+),
Qt 6.12, vcpkg, Python 3.11+. Full setup: [docs/development.md](docs/development.md).

```sh
python scripts/elara.py doctor   # checks the toolchain
python scripts/elara.py check    # builds and runs every test
python scripts/elara.py app      # launches the app (or: app -- song.gp)
```

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md): commands, module boundaries, conventions
and the definition of done.

## Licence

Elara is licensed under the [GNU AGPL v3 or later](LICENSE). Third-party
components and their licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
