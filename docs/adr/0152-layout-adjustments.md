# 0152. Layout adjustments

- Status: Accepted
- Date: 2026-11-27

## Context

Elara lays out every page by rules (ADR-0012 to ADR-0014) and lays it out again
after every edit, only where the edit reached (ADR-0052). A writer edits a
measure far before the end, or inserts music, and the score stays laid out.
Two things are still missing:

- **Stability.** Line breaks are chosen for the whole song at once, for the
  most even spacing (ADR-0014). An edit can move the breaks of systems far from
  it. Nothing tells the engine "keep these systems as they are".
- **Exceptions.** Where the rules do not give what the writer wants (this
  measure starts a system, this beat needs a little more room), there is
  nothing to say so with.

Other editors answer both in ways that age badly: positions stored as
coordinates of a page, which a reflow leaves behind, or adjustments kept by
system, which is not music. The model here is made of values addressed by
index (ADR-0004, ADR-0129); nothing in it has an identity to hang an
adjustment on.

## Decision

- **Layout stays a pure function** of the song, the staves shown, the font and
  the options. No rule prefers what the layout was before: the incremental
  layout stays equal to a full one, the CLI can check everything, undo is
  exact.
- **Stability comes from stored breaks, not from memory.** A writer who wants
  systems to stay says so with system and page breaks; "lock" turns the breaks
  of the current layout into stored ones. Between two stored breaks the engine
  still chooses.
- **An adjustment is stored on what it adjusts.** A beat's on the `Beat`; a
  measure's in a per-measure vector inserted and removed with the measures. It
  follows its music through insertion, removal, undo, copy and paste with no
  identity and no measure numbers kept anywhere; removing the music removes
  it.
- **Spacing adjustments and offsets are different kinds.** A spacing
  adjustment changes what the spacing and the line breaker work with and
  cannot make ink overlap, since a spring never goes under its ink. Offsets
  (moving one mark after layout) are not part of this decision.
- **Two scopes for what belongs to a measure**, since breaks depend on how wide
  the music is, and that on what is shown: a track's own measure layouts when
  it is shown alone, the song's when staves of more than one track are.
  `LayoutScope` names one.
- **Nothing vertical per system.** Distances between staves and systems come
  from the ink and from the song's settings, never from an adjustment of one
  system, which would not survive a reflow.
- **Foreign coordinates are not imported**: imports are laid out by Elara.
- **What is not used costs nothing.** `MeasureLayout` is two bytes (a `Break`
  and a spacing in percent). `Track` and `Song` each hold a
  `SharedOptional<std::vector<MeasureLayout>>`, a null pointer until the first
  adjustment of the scope and again once every entry is back to the default:
  a song without adjustments allocates, copies, compares and saves as before.
  Once used, a scope holds one entry per measure (1 KB for 500 measures),
  shared by the song's copies and replaced as a whole by a change.
- **Breaks**: `Break::System` and `Break::Page` start a system or a page at the
  measure; `Break::Keep` keeps the measure on the system of the one before
  while it fits; `Break::Auto` leaves it to the line breaker. The first
  measure's means nothing.
- **Commands**: `changeBreaks(song, scope, {measure, break}...)` sets several
  at once as one undo step, "Change breaks"; it changes what is drawn, never
  what sounds (ADR-0111), and its vectors count in the history's budget
  (ADR-0121).
- **Under edits**: measures inserted get the default; measures removed take
  their layouts with them, in every scope, and undo brings them back; measures
  pasted over keep their layouts (they belong to the place, not to the music
  pasted); a track removed takes its own.
- **Files**: `.elara` writes `"layout"` on the song and on each track, a sparse
  array of the measures that differ (`{"measure": 12, "break": "system"}`),
  and nothing when a scope is empty, so a song without adjustments is written
  byte for byte as before. `formatVersion` stays 1: an older reader ignores
  it, and ADR-0139 warns before it is lost. MusicXML and MIDI export write
  none of it, and their warnings say so: "system and page breaks dropped" and
  "measure spacing dropped", for the song and for each track.
- `elara-cli edit` has the step `break <measure> <system|page|keep|auto>
  [track <n>]`; `dump --json` shows each scope's breaks.

## Consequences

- Breaks are the first adjustment; room before a beat, the spacing of a
  measure and the song's layout settings follow under the same rules.
- The layout reads the breaks of the scope its staves call for (ADR-0014's
  line breaker gains them); until then they are stored and saved only.
- Two scopes mean a writer who shows one track alone and then the whole band
  sees two sets of breaks. That is what the widths call for; a later command
  may copy one scope's into the other.
- A song with breaks on every measure of every track holds 1 KB per track per
  500 measures, and inserting a measure copies each scope's vector once.
