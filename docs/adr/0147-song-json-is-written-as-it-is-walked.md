# 0147. song.json is written as it is walked

- Status: Accepted
- Date: 2026-11-27

## Context

ADR-0006 chose yyjson to read and write `song.json`, and noted that yyjson
builds a tree of values, so a song is held twice in memory while it is saved;
acceptable for scores, to revisit later. Saving is not rare: the app writes a
recovery copy of the open song in the background a few seconds after each
change (ADR-0106), and every save, export to `.elara` and `elara-cli` command
that prints a song goes through the same writer.

The tree is larger than the text it becomes. On a 20-track, 1500-measure song
(360,000 notes) `song.json` is 24.8 MB with 4.7 million values; yyjson's
mutable values take 24 bytes each (113 MB), and before writing yyjson reserves
the output at 18 bytes a value (85 MB), so saving that song took about 200 MB
on top of the song itself.

## Decision

`song.json` is written by a small writer in `native_format.cpp` that appends
each value to the text as the song is walked, with no tree before it. It
writes yyjson's compact form byte for byte:

- keys in the order the tree had them, no spaces;
- quotes, backslashes and control characters escaped (`\b \t \n \f \r`, the
  rest as `\u00XX` with upper-case hex); `/`, DEL and multi-byte UTF-8 copied;
- integers as they are, other numbers through `yyjson_write_number()`, the
  shortest text that reads back the same;
- text that is not UTF-8, and numbers that are not finite, cannot be written,
  as with yyjson.

yyjson still reads `song.json`; it no longer writes it.

## Consequences

- Saving holds the song and the text, about one and a half times the text
  while the string grows, and nothing else.
- Files are unchanged: the writer's output matched yyjson's on every song of
  the test corpus and on the large song above, and a test checks the
  escaping and number forms against strings yyjson itself writes.
- A new field is written by the writer's functions as before; the writer
  checks nothing about the order of keys, so the round-trip test stays the
  guard that what is written can be read.
