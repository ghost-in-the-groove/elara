# 0039. The mixer in the song panel

- Status: Accepted
- Date: 2026-11-23

## Context

Every track has a mix (ADR-0020): a volume fader, a pan, mute and solo, which
playback follows and the importers fill in. The CLI edits it (`edit "mix ..."`),
but the app had no mixer, so a song opened in it played as the file said. The
song panel (ADR-0031) already lists the tracks. Two things made a naive mixer
unpleasant:

- Each edit made a new sequence and loaded it into the player, which pauses
  and goes back to the start: moving a fader would stop the song.
- A fader sends a value for every step of a drag. One undo step per value
  would flood the history, and an edit per value would copy the song each time.

## Decision

**The mixer lives in the song panel**, under each track: a mute and a solo
toggle, a volume fader and a pan fader, with the value in a tip while it moves.
A track that mute or solo keeps quiet fades, so a solo shows at once which
tracks it silences.

**A fader previews while it moves and commits when let go.**
`ScoreDocument::previewTrackMix()` hands the player the tracks' mixes with the
moving fader's value (`Player::updateMix(std::span<const TrackMix>)`), without
touching the song or copying it. Letting go calls `setTrackMix()`: one undo step,
"Change mix". Mute and solo commit on click.

**Playback carries on when the music does not change.** After an edit the
document builds the sequence again and keeps the previous one when the two are
equal (`Sequence::operator==`): a mix, a title or an artist does not change what
plays. The audio controller only loads a song when the sequence changed;
otherwise it passes the new mix to the player, which ramps to it over a block.
Undoing a mix change carries on the same way.

## Consequences

- The sequence is still built after every edit to find out whether it changed;
  for the 20-track benchmark song that is about 30 ms, the same as before.
- Mixes have no automation over time; the fader is one value per track.
- The smoke test previews a fader, checks the song did not change, sets the
  first track's mix with a solo, checks it is one undo step that keeps the
  sequence and silences the other tracks, and undoes it.
