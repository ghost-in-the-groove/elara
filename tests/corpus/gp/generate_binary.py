# SPDX-License-Identifier: CC0-1.0
"""Writes the Guitar Pro 3, 4 and 5 files (.gp3, .gp4, .gp5) of the test corpus.

    python tests/corpus/gp/generate_binary.py

These versions keep the score as a little-endian binary stream: the song's
information, a header per measure, the tracks, then every track's content
measure by measure. Every field here is written out by hand following the
structure of the format; nothing is copied from Guitar Pro or its files.
Unused fields (page setup, sound settings) hold neutral values.
"""

from __future__ import annotations

import pathlib
import struct

HERE = pathlib.Path(__file__).resolve().parent
VERSIONS = {300: "FICHIER GUITAR PRO v3.00", 406: "FICHIER GUITAR PRO v4.06",
            510: "FICHIER GUITAR PRO v5.10"}
STANDARD = [64, 59, 55, 50, 45, 40]  # string 1 (highest) first


class Writer:
    def __init__(self, version: int):
        self.version = version
        self.out = bytearray()

    def u8(self, value: int) -> None:
        self.out += struct.pack("<B", value)

    def i8(self, value: int) -> None:
        self.out += struct.pack("<b", value)

    def i16(self, value: int) -> None:
        self.out += struct.pack("<h", value)

    def i32(self, value: int) -> None:
        self.out += struct.pack("<i", value)

    def zeros(self, count: int) -> None:
        self.out += bytes(count)

    def byte_string(self, text: str, size: int) -> None:
        """A length byte, then a field of `size` bytes."""
        data = text.encode("cp1252")
        self.u8(len(data))
        self.out += data.ljust(size, b"\0")

    def int_byte_string(self, text: str) -> None:
        """The field's size, then a length byte and the characters."""
        data = text.encode("cp1252")
        self.i32(len(data) + 1)
        self.u8(len(data))
        self.out += data

    def int_string(self, text: str) -> None:
        data = text.encode("cp1252")
        self.i32(len(data))
        self.out += data


# ---- Song description -------------------------------------------------------------
#
# A beat is a dict: dur (-2 whole ... 4 sixty-fourth), dotted, tuplet, rest,
# empty, notes, chord (name, new format?, first fret, frets from string 1), text,
# effects, mix.
# A note: string (1 = highest), fret, kind ("normal", "tie", "dead"), dynamic
# (1 ppp ... 8 fff), ghost, accent, heavy_accent, effects.


def beat(dur, notes=(), **extra):
    return {"dur": dur, "notes": list(notes), **extra}


def note(string, fret, **extra):
    return {"string": string, "fret": fret, **extra}


def rest(dur, **extra):
    return {"dur": dur, "rest": True, **extra}


EMPTY = {"dur": 0, "empty": True}


def write_bend(w: Writer, kind: int, value: int, points) -> None:
    w.i8(kind)
    w.i32(value)
    w.i32(len(points))
    for position, amount in points:
        w.i32(position)  # 0..60 over the note
        w.i32(amount)    # hundredths of a tone
        w.u8(0)


def write_chord(w: Writer, name: str, new_format: bool, first_fret: int = 0,
                frets=()) -> None:
    # frets: from string 1 (the highest), -1 for a string not played.
    played = list(frets) + [-1] * (7 - len(frets))
    if w.version >= 500:
        w.u8(1)
        w.zeros(16)
        w.byte_string(name, 21)
        w.zeros(4)
        w.i32(first_fret)
        for fret in played:
            w.i32(fret)
        w.zeros(32)
    elif not new_format:
        w.u8(0)
        w.int_byte_string(name)
        w.i32(first_fret)  # 0: no frets follow
        if first_fret:
            for fret in played[:6]:
                w.i32(fret)
    elif w.version >= 400:
        w.u8(1)
        w.zeros(16)
        w.byte_string(name, 22)
        w.zeros(3)
        w.i32(first_fret)
        for fret in played:
            w.i32(fret)
        w.zeros(32)
    else:
        w.u8(1)
        w.zeros(25)
        w.byte_string(name, 22)
        w.zeros(12)
        w.i32(first_fret)
        for fret in played[:6]:
            w.i32(fret)
        w.zeros(36)


def write_beat_effects(w: Writer, fx: dict) -> None:
    first = second = 0
    if fx.get("vibrato"):
        first |= 0x01
    if fx.get("wide_vibrato"):
        first |= 0x02
    if fx.get("natural_harmonic"):
        first |= 0x04  # Guitar Pro 3
    if fx.get("artificial_harmonic"):
        first |= 0x08  # Guitar Pro 3
    if "slap" in fx or "tremolo_bar" in fx:
        first |= 0x20
    if "strum" in fx:
        first |= 0x40
    if w.version >= 400:
        if "pick" in fx:
            second |= 0x02
        if "whammy" in fx:
            second |= 0x04
    w.u8(first)
    if w.version >= 400:
        w.u8(second)
    if first & 0x20:
        if w.version >= 400:
            w.i8(fx["slap"])
        else:
            w.u8(fx.get("slap", 0))
            w.i32(fx.get("tremolo_bar", 0))
    if second & 0x04:
        write_bend(w, *fx["whammy"])
    if first & 0x40:
        up, down = fx["strum"]
        w.i8(up)
        w.i8(down)
    if second & 0x02:
        w.i8(fx["pick"])


def write_mix(w: Writer, mix: dict) -> None:
    values = [mix.get(name, -1) for name in
              ("volume", "balance", "chorus", "reverb", "phaser", "tremolo")]
    tempo = mix.get("tempo", -1)
    w.i8(mix.get("instrument", -1))
    if w.version >= 500:
        for _ in range(4):
            w.i32(-1)  # RSE instrument
    for value in values:
        w.i8(value)
    if w.version >= 500:
        w.int_byte_string("")
    w.i32(tempo)
    for value in values:
        if value >= 0:
            w.i8(0)
    if tempo >= 0:
        w.i8(mix.get("transition", 0))
        if w.version >= 510:
            w.u8(0)
    if w.version >= 400:
        w.i8(0)  # applies to this track only
    if w.version >= 510:
        w.i8(-1)
        w.int_byte_string("")
        w.int_byte_string("")


def write_note_effects(w: Writer, fx: dict) -> None:
    first = second = 0
    if "bend" in fx:
        first |= 0x01
    if fx.get("hammer"):
        first |= 0x02
    if w.version < 400 and fx.get("slide"):
        first |= 0x04
    if fx.get("let_ring"):
        first |= 0x08
    if fx.get("grace"):
        first |= 0x10
    if w.version >= 400:
        if fx.get("staccato"):
            second |= 0x01
        if fx.get("palm_mute"):
            second |= 0x02
        if "tremolo" in fx:
            second |= 0x04
        if "slide" in fx:
            second |= 0x08
        if "harmonic" in fx:
            second |= 0x10
        if "trill" in fx:
            second |= 0x20
        if fx.get("vibrato"):
            second |= 0x40
    w.u8(first)
    if w.version >= 400:
        w.u8(second)
    if first & 0x01:
        write_bend(w, *fx["bend"])
    if first & 0x10:
        # A grace note: (fret, dynamic, transition, duration code, flags); Guitar
        # Pro 3-4 write the duration before the transition and have no flags.
        fret, dynamic, transition, length, flags = fx["grace"]
        if w.version >= 500:
            for value in (fret, dynamic, transition, length, flags):
                w.u8(value)
        else:
            w.i8(fret)
            w.u8(dynamic)
            w.u8(length)
            w.i8(transition)
    if second & 0x04:
        w.i8(fx["tremolo"])
    if second & 0x08:
        if w.version >= 500:
            w.u8(fx["slide"])  # flags
        else:
            w.i8(fx["slide"])  # one kind
    if second & 0x10:
        kind, extra = fx["harmonic"][0], fx["harmonic"][1:]
        w.i8(kind)
        for value in extra:
            w.u8(value)
    if second & 0x20:
        fret, period = fx["trill"]  # the other fret; 1 sixteenths, 2 thirty-seconds
        w.i8(fret)
        w.i8(period)


def write_note(w: Writer, n: dict) -> None:
    flags = 0x20  # every note here has a kind and a fret
    if n.get("heavy_accent"):
        flags |= 0x02
    if n.get("ghost"):
        flags |= 0x04
    if n.get("effects"):
        flags |= 0x08
    if "dynamic" in n:
        flags |= 0x10
    if n.get("accent"):
        flags |= 0x40
    if "fingers" in n:
        flags |= 0x80
    w.u8(flags)
    w.u8({"normal": 1, "tie": 2, "dead": 3}[n.get("kind", "normal")])
    if flags & 0x10:
        w.i8(n["dynamic"])
    w.i8(n["fret"])
    if flags & 0x80:
        # The fretting and the plucking hand: -1 none, 0 the thumb to 4 the
        # little finger.
        left, right = n["fingers"]
        w.i8(left)
        w.i8(right)
    if w.version >= 500:
        w.u8(0)
    if flags & 0x08:
        write_note_effects(w, n["effects"])


def write_beat(w: Writer, b: dict) -> None:
    flags = 0
    if b.get("dotted"):
        flags |= 0x01
    if "chord" in b:
        flags |= 0x02
    if "text" in b:
        flags |= 0x04
    if "effects" in b:
        flags |= 0x08
    if "mix" in b:
        flags |= 0x10
    if "tuplet" in b:
        flags |= 0x20
    if b.get("rest") or b.get("empty"):
        flags |= 0x40
    w.u8(flags)
    if flags & 0x40:
        w.u8(0 if b.get("empty") else 2)
    w.i8(b["dur"])
    if flags & 0x20:
        w.i32(b["tuplet"])
    if flags & 0x02:
        write_chord(w, *b["chord"])
    if flags & 0x04:
        w.int_byte_string(b["text"])
    if flags & 0x08:
        write_beat_effects(w, b["effects"])
    if flags & 0x10:
        write_mix(w, b["mix"])
    by_string = {n["string"]: n for n in b.get("notes", [])}
    mask = 0
    for string in by_string:
        mask |= 1 << (7 - string)
    w.u8(mask)
    for string in sorted(by_string):
        write_note(w, by_string[string])
    if w.version >= 500:
        w.i16(0)


def write_header(w: Writer, index: int, h: dict) -> None:
    flags = 0
    if "time" in h:
        flags |= 0x03
    if h.get("repeat_open"):
        flags |= 0x04
    if "repeat_close" in h:
        flags |= 0x08
    if "alternate" in h:
        flags |= 0x10
    if "marker" in h:
        flags |= 0x20
    if "key" in h:
        flags |= 0x40
    if w.version >= 500 and index > 0:
        w.u8(0)
    w.u8(flags)
    if flags & 0x03:
        w.i8(h["time"][0])
        w.i8(h["time"][1])

    def marker():
        w.int_byte_string(h["marker"])
        w.out += bytes([255, 0, 0, 0])

    def key():
        w.i8(h["key"][0])
        w.i8(h["key"][1])

    if w.version < 500:
        if flags & 0x08:
            w.i8(h["repeat_close"])
        if flags & 0x10:
            w.u8(h["alternate"])
        if flags & 0x20:
            marker()
        if flags & 0x40:
            key()
    else:
        if flags & 0x08:
            w.i8(h["repeat_close"])
        if flags & 0x20:
            marker()
        if flags & 0x40:
            key()
        if flags & 0x10:
            w.u8(h["alternate"])
        if flags & 0x03:
            w.out += bytes([2, 2, 2, 2])  # beaming
        if not flags & 0x10:
            w.u8(0)
        w.u8(h.get("triplet_feel", 0))


def write_track(w: Writer, index: int, t: dict) -> None:
    if w.version >= 500 and (index == 0 or w.version == 500):
        w.u8(0)
    flags = 0x01 if t.get("drums") else 0
    if w.version >= 500:
        flags |= 0x08  # visible
        if t.get("solo"):
            flags |= 0x10
        if t.get("mute"):
            flags |= 0x20
    w.u8(flags)
    w.byte_string(t["name"], 40)
    strings = t["strings"]
    w.i32(len(strings))
    for i in range(7):
        w.i32(strings[i] if i < len(strings) else 0)
    w.i32(1)  # port
    w.i32(t["channel"])
    w.i32(t["channel"])  # effect channel
    w.i32(24)  # frets
    w.i32(t.get("capo", 0))
    w.out += bytes([255, 0, 0, 0])  # colour
    if w.version >= 500:
        w.i16(0)
        w.u8(0)  # auto accentuation
        w.u8(0)  # MIDI bank
        w.u8(0)  # humanize
        w.zeros(12 + 12)
        w.i32(-1)  # RSE instrument
        w.i32(0)
        w.i32(0)
        if w.version == 500:
            w.i16(0)
            w.u8(0)
        else:
            w.i32(0)
            w.zeros(4)  # equalizer
            w.int_byte_string("")
            w.int_byte_string("")


def song_file(song: dict) -> bytes:
    v = song["version"]
    w = Writer(v)
    w.byte_string(VERSIONS[v], 30)
    info = song["info"]
    fields = ["title", "subtitle", "artist", "album", "words"]
    if v >= 500:
        fields.append("music")
    fields += ["copyright", "tab", "instructions"]
    for field in fields:
        w.int_byte_string(info.get(field, ""))
    notice = info.get("notice", [])
    w.i32(len(notice))
    for line in notice:
        w.int_byte_string(line)
    if v < 500:
        w.u8(1 if song.get("triplet_feel") else 0)
    if v >= 400:
        track, lines = song.get("lyrics", (0, []))
        w.i32(track)
        for i in range(5):
            measure, text = lines[i] if i < len(lines) else (1, "")
            w.i32(measure)
            w.int_string(text)
    if v >= 510:
        w.i32(100)  # master volume
        w.i32(0)
        w.zeros(11)  # equalizer
    if v >= 500:
        for value in (210, 297, 10, 10, 15, 10, 100):  # page size, margins, scale
            w.i32(value)
        w.i16(0x01FF)
        for template in ("%TITLE%", "%SUBTITLE%", "%ARTIST%", "%ALBUM%", "Words by %WORDS%",
                         "Music by %MUSIC%", "Words & Music by %WORDSMUSIC%", "Copyright %COPYRIGHT%",
                         "All Rights Reserved", "Page %N%/%P%"):
            w.int_byte_string(template)
        w.int_byte_string("Moderate")
    w.i32(song["tempo"])
    if v >= 510:
        w.u8(0)
    if v >= 400:
        w.i8(song.get("key", 0))
        w.i32(0)  # octave
    else:
        w.i32(song.get("key", 0))
    channels = {t["channel"]: t for t in song["tracks"]}
    for slot in range(64):
        track = channels.get(slot + 1, {})
        w.i32(track.get("program", 0))
        # Volume and balance count sixteenths (8 is the centre); then chorus,
        # reverb, phaser, tremolo and padding.
        w.out += bytes([track.get("volume", 13), track.get("balance", 8), 0, 0, 0, 0, 0, 0])
    if v >= 500:
        # The measure (from 1) of each sign and jump, or -1: coda, double coda,
        # segno, segno segno, fine, the twelve D.C./D.S./D.S.S. jumps (plain, al
        # coda, al double coda, al fine), da coda, da double coda.
        directions = song.get("directions", {})
        for index in range(19):
            w.i16(directions.get(index, -1))
        w.i32(0)  # master reverb
    headers = song["measures"]
    w.i32(len(headers))
    w.i32(len(song["tracks"]))
    for index, header in enumerate(headers):
        write_header(w, index, header)
    for index, track in enumerate(song["tracks"]):
        write_track(w, index, track)
    if v >= 500:
        w.zeros(2 if v == 500 else 1)
    for m in range(len(headers)):
        for track in song["tracks"]:
            voices = track["content"][m]
            for voice in range(2 if v >= 500 else 1):
                beats = voices[voice] if voice < len(voices) else [EMPTY]
                w.i32(len(beats))
                for b in beats:
                    write_beat(w, b)
            if v >= 500:
                w.u8(0)
    return bytes(w.out)


# ---- The files ----------------------------------------------------------------------

def techniques_gp5() -> dict:
    lead = {
        "name": "Lead Guitar", "channel": 1, "program": 29, "strings": STANDARD, "capo": 2,
        "volume": 14, "balance": 10, "solo": True,
        "content": [
            # Measure 1: a palm-muted open string, a triplet with a hammer-on, a
            # full bend with vibrato (and a tempo change), a natural harmonic.
            [[beat(0, [note(6, 0, effects={"palm_mute": True})]),
              beat(1, [note(5, 2, effects={"hammer": True})], tuplet=3),
              beat(1, [note(5, 4)], tuplet=3),
              beat(1, [note(4, 2)], tuplet=3),
              beat(0, [note(3, 4, effects={"bend": (1, 100, [(0, 0), (30, 100), (60, 100)]),
                                           "vibrato": True})], mix={"tempo": 80}),
              beat(0, [note(1, 12, effects={"harmonic": (1,), "let_ring": True})])],
             [EMPTY]],
            # Measure 2: a tie that does not repeat its fret (fretted with the
            # ring finger), a loud dead note with a heavy accent; a second voice.
            [[beat(-1, [note(3, 5, dynamic=5, fingers=(3, -1))]),
              beat(0, [note(3, 0, kind="tie", dynamic=5)]),
              beat(0, [note(4, 0, kind="dead", heavy_accent=True, dynamic=7)])],
             [beat(-1, [note(6, 0)]), rest(-1)]],
            # Measure 3 (3/4): an on-beat sixteenth grace note sliding from
            # fret 1, a shift slide, staccato, a down pick stroke and a chord
            # diagram; text.
            [[beat(0, [note(2, 3, effects={"grace": (1, 6, 1, 3, 0x02), "slide": 0x01,
                                           "staccato": True})],
                   dotted=True, chord=("G", True, 1, [3, 0, 0, 0, 2, 3]), effects={"pick": 2}),
              beat(1, [note(2, 5)], text="let it ring"),
              rest(0)],
             [EMPTY]],
            # Measure 4: an upward strum, tremolo picking sliding out downwards,
            # a tapped harmonic with a whammy-bar dip.
            [[beat(-1, [note(3, 2), note(2, 3), note(1, 2)], effects={"strum": (3, 0)}),
              beat(0, [note(1, 5, effects={"tremolo": 2, "slide": 0x04})]),
              beat(0, [note(1, 7, effects={"harmonic": (3, 19)})],
                   effects={"whammy": (6, -50, [(0, 0), (30, -50), (60, 0)])})],
             [EMPTY]],
        ],
    }
    return {
        "version": 510,
        "info": {"title": "Corpus Techniques", "subtitle": "Guitar Pro 5",
                 "artist": "Ghost in the Groove", "music": "Ghost in the Groove",
                 "copyright": "CC0 1.0", "tab": "Elara contributors",
                 "instructions": "Play with a pick", "notice": ["Self-made test file"]},
        "tempo": 100, "key": 1,
                # Intro |: 2 |1. 3 :|2. 4 |: Guitar Pro 5 counts the times played and
        # keeps the alternate endings as bits.
        "measures": [{"time": (4, 4), "marker": "Intro"},
                     {"repeat_open": True},
                     {"time": (3, 4), "key": (2, 0), "repeat_close": 2, "alternate": 0b01},
                     {"time": (4, 4), "alternate": 0b10}],
        "tracks": [lead],
    }


def quartet_gp4() -> dict:
    bass = {
        "name": "Bass", "channel": 2, "program": 33, "strings": [43, 38, 33, 28],
        "volume": 12, "balance": 4,
        "content": [
            [[beat(0, [note(3, 3)], effects={"slap": 2}),
              beat(0, [note(3, 3)], effects={"slap": 3}),
              beat(0, [note(2, 3, effects={"slide": -1})]),
              beat(0, [note(1, 12, effects={"harmonic": (1,)})])]],
            [[beat(-2, [note(4, 1, effects={"let_ring": True})])]],
        ],
    }
    piano = {
        "name": "Piano", "channel": 1, "program": 0, "strings": STANDARD,
        "content": [
            [[beat(-1, [note(3, 5), note(2, 5), note(1, 3)]), rest(-1)]],
            [[beat(-2, [note(1, 8, accent=True)])]],
        ],
    }
    drums = {
        "name": "Drums", "channel": 10, "drums": True, "strings": [0, 0, 0, 0, 0, 0],
        "volume": 16,
        "content": [
            [[beat(0, [note(5, 36), note(1, 42)]), beat(0, [note(3, 38), note(1, 42)]),
              beat(0, [note(5, 36), note(1, 42)]), beat(0, [note(3, 38), note(1, 42)])]],
            [[beat(-2, [note(1, 49)], mix={"tempo": 120, "volume": 10})]],
        ],
    }
    melody = {
        "name": "Melody", "channel": 3, "program": 73, "strings": STANDARD,
        "content": [
            # A chord diagram in the newer format, with a string not played.
            [[beat(0, [note(1, 5)], chord=("D/F#", True, 1, [2, 3, 2, 0, -1, 2])),
              beat(0, [note(1, 8, effects={"vibrato": True})]),
              beat(-1, [note(1, 10, effects={"bend": (1, 50, [(0, 0), (60, 50)])})])]],
            [[beat(-1, [note(1, 8)]),
              beat(0, [note(1, 8, kind="tie")]),
              beat(0, [note(1, 5, effects={"trill": (7, 2)})])]],
        ],
    }
    return {
        "version": 406,
        "info": {"title": "Corpus Band", "subtitle": "Guitar Pro 4", "artist": "Ghost in the Groove",
                 "words": "Ghost in the Groove", "copyright": "CC0 1.0", "tab": "Elara contributors"},
        "triplet_feel": True,
        "lyrics": (4, [(1, "[Verse] Sing a-long  hey there"), (2, "oh")]),
        "tempo": 90, "key": -1,
                # |:1. 1 :|2. 2 |: Guitar Pro 4 counts the times played again and names
        # each alternate ending by its number.
        "measures": [{"time": (4, 4), "repeat_open": True, "repeat_close": 1, "alternate": 1},
                     {"alternate": 2}],
        "tracks": [bass, piano, drums, melody],
    }


def melody_gp3() -> dict:
    nylon = {
        "name": "Nylon Guitar", "channel": 1, "program": 24, "strings": STANDARD,
        "content": [
            # Measure 1: a natural harmonic (kept per beat in Guitar Pro 3), a
            # hammer-on fretted with the index and plucked with m, a slide with
            # a chord diagram, then a tapped quintuplet.
            [[beat(0, [note(1, 12)], effects={"natural_harmonic": True}),
              beat(0, [note(2, 1, effects={"hammer": True}, fingers=(1, 2))]),
              beat(0, [note(2, 3, effects={"slide": True})],
                   chord=("Am", True, 1, [0, 1, 2, 2, 0, -1])),
              beat(2, [note(3, 2)], tuplet=5, effects={"slap": 1}),
              beat(2, [note(3, 2)], tuplet=5),
              beat(2, [note(3, 4)], tuplet=5),
              beat(2, [note(3, 5)], tuplet=5),
              beat(2, [note(3, 4)], tuplet=5)]],
            # Measure 2 (6/8): wide vibrato and let ring with an old-style chord
            # diagram and text, a whammy-bar dip, a downward strum pulled off
            # from a thirty-second grace note at fret 3.
            [[beat(0, [note(4, 2, effects={"let_ring": True})], dotted=True,
                   chord=("E", False, 1, [0, 0, 1, 2, 2, 0]), text="ad lib.",
                   effects={"wide_vibrato": True}),
              beat(1, [note(5, 0)], effects={"tremolo_bar": 50}),
              beat(0, [note(6, 0, effects={"grace": (3, 6, 3, 1, 0)})],
                   effects={"strum": (0, 2)})]],
        ],
    }
    return {
        "version": 300,
        "info": {"title": "Corpus Melodía", "artist": "Ghost in the Groove",
                 "words": "Ghost in the Groove", "copyright": "CC0 1.0", "tab": "Elara contributors",
                 "notice": ["Self-made test file", "Second line"]},
        "tempo": 75, "key": 0,
        "measures": [{"time": (4, 4), "key": (0, 1)}, {"time": (6, 8)}],
        "tracks": [nylon],
    }


def dal_segno_gp5() -> dict:
    # 1 | 2 segno | 3 da coda | 4 D.S. al Coda | 5 coda | 6: played
    # 1 2 3 4 2 3 5 6.
    guitar = {
        "name": "Guitar", "channel": 1, "program": 27, "strings": STANDARD,
        "content": [[[beat(-2, [note(1, fret)])], [EMPTY]] for fret in (0, 1, 3, 5, 7, 8)],
    }
    return {
        "version": 510,
        "info": {"title": "Corpus Navigation", "subtitle": "Guitar Pro 5",
                 "artist": "Ghost in the Groove", "copyright": "CC0 1.0",
                 "tab": "Elara contributors"},
        "tempo": 120, "key": 0,
        "directions": {2: 2, 17: 3, 10: 4, 0: 5},
        "measures": [{"time": (4, 4)}, {}, {}, {}, {}, {}],
        "tracks": [guitar],
    }


def piano_gp5() -> dict:
    # Guitar Pro 3-5 keep a piano on one staff, its notes on guitar strings.
    piano = {
        "name": "Piano", "channel": 1, "program": 0, "strings": STANDARD,
        "content": [
            # Measure 1: a strummed chord across both hands (C5 accented, E4,
            # F2), A4 alone, then G4 over C3.
            [[beat(0, [note(1, 8, accent=True), note(2, 5), note(6, 1)],
                   effects={"strum": (3, 0)}),
              beat(0, [note(1, 5)]),
              beat(-1, [note(1, 3), note(5, 3)])],
             [EMPTY]],
            # Measure 2: a melody in eighths ending on a tied C5, over a whole
            # F2 and C3 in the second voice.
            [[beat(1, [note(1, 0)]), beat(1, [note(1, 1)]), beat(1, [note(1, 3)]),
              beat(1, [note(1, 5)]), beat(1, [note(1, 7)]), beat(1, [note(1, 8)]),
              beat(0, [note(1, 8, kind="tie")])],
             [beat(-2, [note(6, 1), note(5, 3)])]],
        ],
    }
    # The right hand in the first voice, the left hand in the second, always.
    electric = {
        "name": "Electric Piano", "channel": 2, "program": 4, "strings": STANDARD,
        "content": [
            [[beat(-1, [note(1, 8)]), beat(-1, [note(1, 5)])],
             [beat(-2, [note(6, 1)])]],
            [[beat(-2, [note(1, 3)])],
             [beat(-1, [note(5, 3)]), beat(-1, [note(6, 3)])]],
        ],
    }
    return {
        "version": 510,
        "info": {"title": "Corpus Piano", "subtitle": "Guitar Pro 5",
                 "artist": "Ghost in the Groove", "copyright": "CC0 1.0",
                 "tab": "Elara contributors"},
        "tempo": 80, "key": 0,
        "measures": [{"time": (4, 4)}, {}],
        "tracks": [piano, electric],
    }


FILES = {
    "techniques.gp5": techniques_gp5,
    "piano.gp5": piano_gp5,
    "dal-segno.gp5": dal_segno_gp5,
    "quartet.gp4": quartet_gp4,
    "melody.gp3": melody_gp3,
}


def main() -> None:
    for name, make in FILES.items():
        (HERE / name).write_bytes(song_file(make()))
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
