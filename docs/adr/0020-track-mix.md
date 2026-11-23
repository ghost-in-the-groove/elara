# 0020. The track mix

- Status: Accepted
- Date: 2026-11-23

## Context

The mixer (milestone 4) sets each track's volume and pan and mutes or solos
tracks. Every format Elara reads already carries these settings: Guitar Pro
keeps a channel volume and balance per track (and, since version 5, mute and
solo), Guitar Pro 6 and 7 a channel strip, MIDI files the channel volume and
pan controllers, MusicXML a volume and pan per MIDI instrument. Until now the
importers dropped them, so a song played back flat: every track at the same
level, in the centre.

Where they live decides who sees them. Settings kept by the app would be lost
on save and invisible to the command line and the tests; settings kept by the
audio engine would make it a second place that holds song data, which golden
rule 1 forbids.

## Decision

**In the model.** Each `core::Track` has a `core::TrackMix`:

| Field | Range | Default | Meaning |
|---|---|---|---|
| `volume` | 0 to 1 | 0.8 | The fader position: 0 is silent, 1 the top of the fader |
| `pan` | -1 to 1 | 0 | -1 hard left, 0 the centre, 1 hard right |
| `mute` | | false | The track is silent |
| `solo` | | false | Only soloed tracks sound |

`Song::audible(i)` says whether track `i` sounds: it is not muted, and either
it is soloed or no track is. The mix is part of the song: it is saved in the
native format (only when it is not the default, so older files and songs
without a mix are unchanged), it counts for `Song` equality, and the app
changes it through the undoable `core::SetTrackMix` command ("Change mix",
ADR-0005). `setMix` rejects values outside the ranges.

**A fader position, not a gain.** The volume is where the fader stands, as in
the formats, which all store a position rather than decibels. The default 0.8
is close to the General MIDI default channel volume (100 of 127), so imported
songs keep their balance. The mixer turns positions into gains: the plan is a
gain of (volume / 0.8)², which is unity at the default and about +4 dB at the
top, and a constant-power pan that is unity in the centre.

**Imports.**

- Guitar Pro 3 to 5: the track's channel volume and balance count sixteenths
  (volume / 16, (balance - 8) / 8); Guitar Pro 5's track flags give mute and
  solo.
- Guitar Pro 6 and 7: the channel strip's parameter 12 is the volume and
  parameter 11 the pan, both from 0 to 1; an automation at the very start
  replaces them; the playback state gives mute and solo.
- MIDI: channel volume (controller 7, / 127) and pan (controller 10, 64 the
  centre) sent before the channel's first note.
- MusicXML: `<volume>` (a percentage) and `<pan>` (degrees, -90 to 90; a pan
  behind the listener counts as its mirror in front) of the part's first MIDI
  instrument that has them.

Changes later in the song (automations, controllers after the first note) are
not modelled yet and are dropped with a warning ("volume and pan changes
dropped").

**Exports.** MIDI writes the volume and pan controllers at the start of each
channel and MusicXML the volume and pan of each MIDI instrument, both only
when they differ from the default. Neither format has mute or solo: they are
dropped with a warning.

**Headless.** `elara-cli dump` and `import` show each track's mix (in
`song.json` when it is not the default, and in the text report), `edit` prints
it for every track, and the `edit` step `mix <track> <volume> <pan> [mute]
[solo]` changes it (ADR-0003).

## Consequences

- Playback and the mixer panel read the mix from one place, and a saved song
  plays back as it was mixed, in Elara and in the files it exports.
- Muting or soloing a track is an edit: it marks the document modified and can
  be undone. That matches Guitar Pro, which saves both with the song; a mixer
  panel should merge one fader drag into one undo step.
- Mix automation (volume and pan changing during the song) needs its own model
  later; until then imports keep only the starting values and say so.
- Volumes from the formats are not exact fractions of the fader (sixteenths,
  127ths, percentages), so a song that goes through MIDI or Guitar Pro and back
  may come back with a slightly different volume. MusicXML writes up to four
  decimals and keeps them.
