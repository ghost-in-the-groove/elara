# SPDX-License-Identifier: CC0-1.0
"""Writes the MIDI files of the test corpus, byte for byte the same on every run.

    python tests/corpus/midi/generate.py

Every event is written out here, from the Standard MIDI File 1.0 specification;
"played" timing comes from a fixed pseudo-random sequence, not a recording.
"""

from __future__ import annotations

import pathlib
import struct

HERE = pathlib.Path(__file__).resolve().parent


class Jitter:
    """A tiny linear congruential generator: the same numbers on every Python."""

    def __init__(self, seed: int) -> None:
        self.state = seed

    def next(self, low: int, high: int) -> int:
        self.state = (self.state * 1103515245 + 12345) % (1 << 31)
        return low + self.state % (high - low + 1)


def var_length(value: int) -> bytes:
    groups = [value & 0x7F]
    value >>= 7
    while value:
        groups.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(groups))


def meta(kind: int, data: bytes) -> bytes:
    return bytes([0xFF, kind]) + var_length(len(data)) + data


def text(kind: int, value: str) -> bytes:
    return meta(kind, value.encode("utf-8"))


def tempo(bpm: float) -> bytes:
    return meta(0x51, round(60_000_000 / bpm).to_bytes(3, "big"))


def time_signature(numerator: int, denominator: int, clocks: int) -> bytes:
    return meta(0x58, bytes([numerator, denominator.bit_length() - 1, clocks, 8]))


def key_signature(fifths: int, minor: bool = False) -> bytes:
    return meta(0x59, struct.pack("bB", fifths, 1 if minor else 0))


def on(channel: int, key: int, velocity: int) -> bytes:
    return bytes([0x90 | channel, key, velocity])


def off(channel: int, key: int) -> bytes:
    return bytes([0x80 | channel, key, 64])


def program(channel: int, number: int) -> bytes:
    return bytes([0xC0 | channel, number])


def controller(channel: int, number: int, value: int) -> bytes:
    return bytes([0xB0 | channel, number, value])


def track(events: list[tuple[int, bytes]]) -> bytes:
    """An MTrk chunk from (tick, event) pairs; stable-sorted by tick, note-offs
    first at equal ticks so a key struck again is not cut short."""
    order = sorted(
        range(len(events)),
        key=lambda i: (events[i][0], 0 if (events[i][1][0] & 0xF0) == 0x80 else 1, i),
    )
    body = bytearray()
    now = 0
    for i in order:
        tick, data = events[i]
        body += var_length(tick - now) + data
        now = tick
    body += var_length(0) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def smf(fmt: int, division: int, tracks: list[bytes]) -> bytes:
    return b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), division) + b"".join(tracks)


def note(events, channel, start, length, key, velocity=80):
    events.append((start, on(channel, key, velocity)))
    events.append((start + length, off(channel, key)))


def played_piano() -> bytes:
    """Two hands played without quantization: starts off by up to 24 ticks,
    releases early or late, uneven velocities and a sustain pedal."""
    q = 480
    jitter = Jitter(2026)
    conductor = [
        (0, text(0x03, "Corpus Played Piano")),
        (0, text(0x02, "CC0 1.0")),
        (0, time_signature(4, 4, 24)),
        (0, key_signature(2)),
        (0, tempo(92)),
    ]
    piano = [(0, text(0x03, "Piano")), (0, program(0, 0))]
    # Right hand: eighths, then a dotted quarter and an eighth, in D major.
    melody = [
        (74, 1), (73, 1), (71, 1), (69, 1), (71, 1), (73, 1), (74, 2),
        (76, 1), (78, 1), (79, 1), (78, 1), (76, 3), (74, 1),
        (73, 1), (74, 1), (76, 1), (73, 1), (69, 2), (66, 2),
        (74, 8),
    ]
    at = 0
    for key, eighths in melody:
        start = max(0, at + jitter.next(-24, 24))
        length = eighths * q // 2 + jitter.next(-60, 20)
        note(piano, 0, start, length, key, jitter.next(58, 96))
        at += eighths * q // 2
    # Left hand: half-note chords (root and fifth), a little behind the beat.
    for measure, (root, fifth) in enumerate([(50, 57), (43, 50), (45, 52), (50, 57)]):
        for half in range(2):
            start = measure * 4 * q + half * 2 * q + jitter.next(0, 30)
            length = 2 * q + jitter.next(-90, -10)
            note(piano, 0, start, length, root, jitter.next(50, 70))
            note(piano, 0, start + jitter.next(0, 8), length, fifth, jitter.next(50, 70))
    piano.append((0, controller(0, 64, 127)))
    piano.append((8 * q, controller(0, 64, 0)))
    return smf(1, q, [track(conductor), track(piano)])


def drums() -> bytes:
    """A two-measure rock beat at a low division: kick, snare, closed and open
    hi-hat, crash, pedal hi-hat, with short drum-machine note lengths."""
    q = 96
    conductor = [(0, text(0x03, "Corpus Drums")), (0, time_signature(4, 4, 24)), (0, tempo(120))]
    kit = [(0, text(0x03, "Drums"))]
    hit = 12
    for measure in range(2):
        base = measure * 4 * q
        for eighth in range(8):
            at = base + eighth * q // 2
            last = measure == 1 and eighth == 7
            note(kit, 9, at, hit, 46 if last else 42, 70 if eighth % 2 else 90)  # hi-hat
        for beat in (0, 2):
            note(kit, 9, base + beat * q, hit, 36, 110)  # kick
        note(kit, 9, base + 2 * q + q // 2, hit, 36, 90)  # kick on the "and" of 3
        for beat in (1, 3):
            note(kit, 9, base + beat * q, hit, 38, 105)  # snare
            note(kit, 9, base + beat * q, hit, 44, 60)  # pedal hi-hat
    note(kit, 9, 0, hit, 49, 120)  # crash
    return smf(1, q, [track(conductor), track(kit)])


def band_format0() -> bytes:
    """One format 0 track with three channels: fingered bass, steel guitar and a
    flute with a triplet; 3/4 then 6/8, B-flat major, a tempo change and a lyric."""
    q = 120  # triplet eighths are 40 ticks
    events = [
        (0, text(0x03, "Corpus Band")),
        (0, time_signature(3, 4, 24)),
        (0, key_signature(-2)),
        (0, tempo(100)),
        (0, program(0, 33)),
        (0, program(1, 25)),
        (0, program(2, 73)),
    ]
    # Two measures of 3/4 (360 ticks each), then two of 6/8 (360 ticks each).
    events.append((2 * 3 * q, time_signature(6, 8, 36)))
    events.append((2 * 3 * q, tempo(80)))
    bass = [(46, 0, 2 * q), (53, 2 * q, q), (51, 3 * q, 3 * q), (46, 6 * q, 3 * q),
            (41, 9 * q, 3 * q)]
    for key, start, length in bass:
        note(events, 0, start, length, key, 90)
    # Guitar: three-note chords on each beat of the 3/4 measures, then dotted quarters.
    for beat in range(6):
        for key in (58, 62, 65):
            note(events, 1, beat * q, q, key, 64)
    for dotted in range(4):
        for key in (58, 63, 67):
            note(events, 1, 6 * q + dotted * 3 * q // 2, 3 * q // 2, key, 64)
    # Flute: quarter, triplet eighths, quarter; then eighths in 6/8.
    flute = [(70, 0, q), (72, q, 40), (74, q + 40, 40), (75, q + 80, 40), (77, 2 * q, q),
             (74, 3 * q, 3 * q)]
    for i in range(6):
        flute.append((72 + (i % 3) * 2, 6 * q + i * q // 2, q // 2))
    for i in range(6):
        flute.append((77 - i, 9 * q + i * q // 2, q // 2))
    for key, start, length in flute:
        note(events, 2, start, length, key, 76)
    events.append((0, text(0x05, "la")))
    return smf(0, q, [track(events)])


def karaoke() -> bytes:
    """A karaoke file: the "@K" mark, a track of words in text events after a
    header of "@" lines (title and author), words parted by spaces with "\\" and
    "/" starting a paragraph and a line, a sung melody and a piano under it."""
    q = 480
    marker = [(0, text(0x01, "@KMIDI KARAOKE FILE")), (0, time_signature(4, 4, 24)),
              (0, tempo(96))]
    words = [(0, text(0x03, "Words")), (0, text(0x01, "@V0100")), (0, text(0x01, "@LENGL")),
             (0, text(0x01, "@TCorpus Song")), (0, text(0x01, "@TGhost in the Groove"))]
    syllables = ["\\Hel", "lo ", "lit", "tle ", "/song ", "of ", "mine"]
    melody = [(67, 1), (64, 1), (65, 1), (62, 1), (64, 1), (62, 1), (60, 2)]
    voice = [(0, text(0x03, "Melody")), (0, program(0, 53))]
    at = 0
    for syllable, (key, beats) in zip(syllables, melody):
        words.append((at, text(0x01, syllable)))
        note(voice, 0, at, beats * q, key, 90)
        at += beats * q
    piano = [(0, text(0x03, "Piano")), (0, program(1, 0))]
    for measure, chord in enumerate([(48, 55, 64), (43, 53, 59)]):
        for key in chord:
            note(piano, 1, measure * 4 * q, 4 * q, key, 60)
    return smf(1, q, [track(marker), track(words), track(voice), track(piano)])


FILES = {
    "played-piano.mid": played_piano,
    "drums.mid": drums,
    "band-format0.mid": band_format0,
    "karaoke.kar": karaoke,
}


def main() -> None:
    for name, make in FILES.items():
        (HERE / name).write_bytes(make())
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
