# 0100. Natural harmonics as diamonds

- Status: Accepted
- Date: 2026-11-25

## Context

A natural harmonic is played by touching a string over a node, at a fret
(`Harmonic`, `HarmonicKind::Natural`, ADR-0070); the note keeps the pitch of
that fret, which tablature writes as the fret in angle brackets ("<12>").
Standard notation drew it as a plain note, so a harmonic read as the stopped
note. The other kinds (artificial, pinched, tapped, semi) are stopped notes
whose harmonic sounds above them; standard notation names them over the
staff ("A.H.", "P.H.", "T.H.", "S.H.").

Guitar engraving writes a natural harmonic as a diamond notehead where the
string is touched, as Guitar Pro shows it.

## Decision

**A natural harmonic is a diamond.** On the standard staff, the notehead of a
note with a natural harmonic is the SMuFL diamond (whole, half or black, by
its value), at its written pitch, the fret where it is touched. Dead notes stay
crosses, and the other harmonics keep plain noteheads and their names over the
staff.

**Nothing else changes.** The model, the formats and playback are as they
were.

## Consequences

- Natural harmonics read as such in every layout of standard notation.
- Tests: `notation.Staff layout: dead notes are crosses, natural harmonics
  diamonds` and the render snapshots of the corpus files with harmonics.
