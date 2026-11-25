# 0071. Recent scores

- Status: Accepted
- Date: 2026-11-25

## Context

Opening a score meant going through the file dialog every time, even for the
song worked on yesterday. The welcome pane, which is what the window shows at
start, offered only Open and New.

## Decision

**The app keeps a list of the last eight score files opened or saved, the
latest first, and shows it on the welcome pane and in an Open recent menu.**

- `RecentScores` (`elara_app_view`, registered in QML as `RecentScores` with
  `QML_FOREIGN` so that the app tests reach it without a QML engine) holds the
  paths. A file goes to the top when the document's `source` changes: when it
  opens, and when the song is saved under a name. A file already listed moves
  instead of showing twice; on Windows paths compare without case.
- Only local files are listed, and only those that still exist are shown; the
  list keeps the others in case their drive comes back.
- The list is kept in the app's settings under `recentScores` while
  `remember` is on. The window turns it on from `rememberSettings`, so checks
  and screenshots, like the SoundFont choice, keep a list of their own in
  memory and leave the person's alone.
- The welcome pane shows up to six, each with its format as a badge (the
  extension, MusicXML's as XML), its name and its folder; a click opens it
  (after asking to save changes), a cross on hover takes it off. The More menu
  has Open recent with all eight and Clear the list.

## Consequences

- The work of yesterday is one click away at start.
- The settings hold file paths; nothing else about the files is stored.
- The smoke test that opens a file checks that it heads the list; unit tests
  cover the order, the limit, files that are gone, removing and clearing, and
  that only a list that remembers reads or writes the settings.
