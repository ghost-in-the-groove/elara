# 0109. The note effects palette

- Status: Accepted
- Date: 2026-11-26

## Context

The note effects (ties, hammer-ons, slides, bends, harmonics, vibrato, accents,
ornaments, fingerings, dynamics, hairpins, slurs, grace notes, fermatas, octave
lines, the pedal, breaths, strums, pick strokes, tremolo picking, tapping and
the whammy bar) lived in a panel that a button of the entry tools opened over
the score. It covered the staff being written, it had to be opened to learn
what the caret's note already had, and it mixed a list of names with rows of
unnamed signs.

Guitar Pro keeps all of this in a palette at the left of the score whose
buttons light up with the note under the cursor; it is what its users find
fastest, and what they fault is that it is one wall of more than a hundred
unlabelled icons. MuseScore's palettes are named, foldable groups. ADR-0108
left docking ours as the next task.

## Decision

**A palette docked at the left of the score** (`EffectsPalette.qml`, 236 px),
the score beside it, never under it. It shows by default while a score is open;
F2, the View menu and a button of the command bar hide and show it.

- **Named groups that fold**: Note effects, Bend, Slide, Harmonic, Vibrato,
  accent and ornaments, Dynamics, Phrasing and lines, Beat, Whammy bar. A
  group's heading is plain text; a click folds it. Groups that need strings
  show only on a fretted staff.
- **Tiles**: every effect is a tile of the size of a tool. A tile is drawn as
  the score draws the effect (a line icon from `Icons.qml`), lettered as the
  score letters it (P.M., N.H., tr), or written in the music font (the
  dynamics). It is lit in the accent while the caret's note or beat has what it
  sets, and a click sets it or takes it off: the palette always says what the
  caret has.
- **Keys in the corner**: the fourteen effects with a key (T, H, S, U, V, P, X,
  L, N, O, >, ', _, ") show it in the corner of their tile, and every tile's
  tooltip names the effect. The palette teaches the keys instead of replacing
  them.
- It never takes the keyboard from the score, and it is dimmed while there is
  no caret.

The entry tools lose their effects button. The page keeps its size when the
palette or the song panel opens or closes only if the zoom was chosen by hand:
a zoom that fits the paper or the width is kept fitting (`ScoreView.fit`).

`--effects` still opens the palette at start, and the smoke test still checks
that it did; `--menu <name>` opens a menu at start for screenshots, and
`app.smoke_menu` runs the smoke test with one open.

## Consequences

- What the caret's note has is always in sight, and the staff being written is
  never covered.
- A window narrower than about 1000 px shows the page smaller while the palette
  is open; F2 gives the room back.
- About a hundred tiles are made when the first score opens. They are plain
  rectangles, labels and static shapes with no animations of their own; the
  panel they replace made the same items when the window loaded.
- New effects get a tile in the group they belong to, with an icon in
  `Icons.qml`; a group that grows past three rows should be split.
