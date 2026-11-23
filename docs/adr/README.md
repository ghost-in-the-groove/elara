# Architecture Decision Records

One file per decision that constrains future work: `NNNN-short-title.md`,
numbered in order, never renumbered. Superseded ADRs stay, with a link to the
one that replaces them.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-technology-stack.md) | Technology stack | Accepted |
| [0002](0002-juce-as-headless-audio-library.md) | JUCE as a headless audio library | Accepted |
| [0003](0003-headless-verification.md) | Headless verification | Accepted |
| [0004](0004-song-model.md) | Song model | Accepted |
| [0005](0005-undo-with-reversible-commands.md) | Undo with reversible commands | Accepted |
| [0006](0006-native-file-format.md) | Native file format | Accepted |
| [0007](0007-musicxml-import.md) | MusicXML import | Accepted |
| [0008](0008-musicxml-export.md) | MusicXML export | Accepted |
| [0009](0009-midi-import-and-export.md) | MIDI import and export | Accepted |
| [0010](0010-guitar-pro-import.md) | Guitar Pro import | Accepted |

## Template

```markdown
# NNNN. Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
What forces are at play.

## Decision
What we do.

## Consequences
What becomes easier, harder, or must be watched.
```
