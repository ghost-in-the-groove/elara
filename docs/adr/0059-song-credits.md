# 0059. Song credits

- Status: Accepted
- Date: 2026-11-24

## Context

A song's information (`SongInfo`) has nine lines: title, subtitle, artist,
album, words, music, copyright, transcriber and notes. The importers read them
all, the native format and MusicXML write them, and the first page shows the
title, subtitle, artist, words, music and copyright. Only the title (the CLI's
`title` step, the song panel) and the artist (the song panel) could be edited,
so a song typed in Elara could not say who wrote it.

## Decision

**One line at a time.** `Credit` names the nine lines in that order, and
`creditOf()` reads or writes one of them. `changeCredit()` sets one line to a
text as one undo step, "Edit song info" (the name the title and artist edits
already use), and returns nothing when the line reads that text already. An
empty text clears the line. Editing one line at a time keeps each field of the
panel its own undo step and never overwrites a line another edit changed.

**CLI.** `elara-cli edit` gains `credit <line> [text]`, the line named as in the
native format (`title`, `subtitle`, `artist`, `album`, `words`, `music`,
`copyright`, `transcriber`, `notes`); without a text it clears the line. The
`title` step stays.

**App.** `ScoreDocument::credits` lists the nine lines in the order of `Credit`
(empty lines with no song, so that QML can index it at any time) and
`setCredit(line, text)` sets one. The song panel gains a Credits section,
folded at first, with a field for the subtitle, album, words, music, copyright
and transcriber, and a wrapping area for the notes. Title and artist stay at
the top of the Song section.

## Consequences

- The score shows a new subtitle, words, music or copyright as soon as it is
  typed; the album, the transcriber and the notes are kept and exported but not
  drawn.
- A note with line breaks can be typed in the panel; the CLI takes one line.
