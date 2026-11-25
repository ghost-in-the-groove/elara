# 0068. Guitar techniques on a library's keyswitches and controllers

- Status: Accepted
- Date: 2026-11-25

## Context

The sampler (ADR-0066) imitates palm mutes, dead notes and harmonics the way
the built-in synthesizer does: it shortens, darkens or softens the note. A
guitar library often records these touches, and a recorded palm mute sounds
far better than a filtered open note. Libraries reach them in their own
ways:

- a keyswitch: a key below the guitar's range selects a group of regions
  (`sw_last`), named for the player by `sw_label` ("Palm Mute", "Harmonics",
  "Dead Notes");
- a controller: regions answer only to some values of it (`loccN`/`hiccN`)
  or change their sound with it, and `label_ccN` names it ("Muting").

There is no standard naming. Libraries do name these controls for their
users, though, in English, and the names are short and alike.

## Decision

**`SfzBank` finds the library's control for each touch by its name and
plays the touch on it, for that note alone.**

- `parseSfz()` keeps `sw_label` per region and `label_ccN` per program.
- When the bank is made it looks, in the order of the regions, for the first
  keyswitch whose label holds "harm" (harmonic), "dead" or "ghost" (dead
  note), or "palm" or "mute" (palm mute). A palm mute without a keyswitch
  takes a controller whose label holds "palm" or "mut" and that some region
  answers to or is modulated by. `SfzBank::techniques()` says what it found.
- A note with such a touch plays as if its keyswitch were pressed, or its
  controller at 127, only while picking and shaping that note's regions: the
  keyswitch the other notes hear and the controllers' values stay as they
  were. The sampler then plays the note as recorded, with none of its own
  imitation.
- When the library's control has no region for that note (a palm mute
  recorded only on the lower strings, a controller range that silences the
  note), the note is played as before: the current keyswitch and the
  sampler's imitation. A technique never makes a note silent.
- `elara-cli sfz` reports the techniques: `"techniques": {"palm-mute",
  "dead", "harmonic"}`, each `{"keyswitch" or "cc": N, "label"}` or null.

## Consequences

- Libraries that name their controls in English play their recorded touches
  with no setup; others keep the imitation they had. A mapping chosen by
  hand per library (a file next to the SFZ, or in the app) can come later
  if a library needs it.
- Legato is not mapped: a hammer-on or pull-off is a glide of the sounding
  note (ADR-0023), not a new note, so there is nothing to strike a legato
  region with. Playing a library's legato samples needs a note to start
  them, a later task.
- Tests build small libraries with each kind of control: a keyswitch, a
  controller, a technique recorded on part of the range, a label that names
  nothing, and the CLI reports the controller of the test guitar.
