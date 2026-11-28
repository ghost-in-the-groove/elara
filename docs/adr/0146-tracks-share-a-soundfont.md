# 0146. Tracks share a SoundFont

- Status: Accepted
- Date: 2026-11-27

## Context

Each track plays on a FluidSynth synthesizer of its own (ADR-0043), so that
tracks render in parallel and are mixed apart. Each synthesizer loaded the
SoundFont file again: its presets, instruments and zones, about 12 MB for
MuseScore General (3861 zones, each with its generators). The samples were
shared already, through FluidSynth's sample cache. A 20-track song held 20
copies of the same zones: MuseScore General cost the app about 325 MB for
20 tracks, 105 MB for one. Every synthesizer also loaded and dropped the
font's first preset, a piano, on its way to the track's presets.

FluidSynth lets one loaded SoundFont be added to several synthesizers
(`fluid_synth_add_sfont`), but two things in it assume one thread:

- A sample counts the voices that play it with a plain integer, changed when
  a voice starts and ends. Synthesizers rendering on different threads would
  lose counts, and a SoundFont whose counts are not zero is never freed.
- Finding the zones of a note writes a flag in each zone it looks at, even
  when the flag does not change.

It also counts, under no lock, the channels that select each preset and the
selected presets that need each sample, and loads and drops samples by those
counts.

## Decision

- `SoundFont::open()` loads the file once into a synthesizer that never plays,
  with no preset selected, so it reads no samples. The SoundFont and every
  sound made from it hold that copy; it goes with the last of them.
- A track's sound is a synthesizer of its own, as before, to which the copy is
  added. Its channels are given their presets before, so adding the font
  selects them directly. A mutex of the font lets one synthesizer at a time
  select presets or let them go, when a sound is made or deleted; playing
  takes no lock.
- The overlay port patches FluidSynth 2.6.2 (`share-soundfonts.patch`):
  the voices of a sample are counted atomically, the zone flag is written only
  when it is set (legato, which Elara never uses), and when the file can no
  longer be opened every sample of a preset is still counted, and disabled,
  instead of only the first, as dropping the preset later expects.
- A sound whose samples cannot be read, the file gone, plays on the built-in
  synthesizer, as when the font could not be loaded.

## Consequences

- MuseScore General costs one copy of its zones however many tracks play it.
- Making a sound no longer parses the file, and no longer loads and drops a
  piano first.
- The patch is ours to carry to the next FluidSynth: three small hunks in
  `src/sfloader`, each marked "patched for Elara".
- A SoundFont now holds its zones while it is open, even with no sound made:
  the app keeps the chosen SoundFont open, so this costs one copy while no
  song is loaded.
