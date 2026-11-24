# 0061. Whammy bar and bass techniques in the score

- Status: Accepted
- Date: 2026-11-24

## Context

Beats can carry a whammy-bar curve and a bass technique (tapping, slap, pop),
read from files and set at the caret (ADR-0060), and playback follows the
whammy bar, but the score drew neither: a dive or a slapped note looked like
any other note.

## Decision

**Two more technique lanes** (ADR-0012), drawn wherever the technique lanes
are: above the tablature, or above the notation when no tablature shows.

- **Whammy bar**, just above the bends' lane: the beat's curve drawn as lines
  from its column towards the next, from the written pitch down for a dive or
  up for a pull, with an upright stroke when the bar is pushed or pulled before
  the attack. The curve fills the lane's height whatever its depth, and the
  depth is written over it in steps from the written pitch ("-1 1/2", "+1/2").
  The first beat of a run of whammy-bar beats (a run ends at a beat without
  one or at a barline) starts its label with "w/bar", as tablature writes it.
- **Bass techniques**: "T", "S" or "P" over the beat, as harmonic labels are.

The curve and its label take the bends' role, the letters the techniques'.

## Consequences

- A whammy bar's exact shape and depth read at a glance; its lane takes room
  only on lines that use it.
- The label goes left-aligned from the beat, so whammy bars on many short
  beats in a row may crowd each other; they are rare enough to accept it.
