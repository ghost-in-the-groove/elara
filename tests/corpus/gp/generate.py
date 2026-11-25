# SPDX-License-Identifier: CC0-1.0
"""Writes the Guitar Pro 6-8 files (.gpx, .gp) of the test corpus, the same on every run.

    python tests/corpus/gp/generate.py

A .gp file is a zip archive whose Content/score.gpif holds the score as XML; a
.gpx file keeps an older form of the same XML in a compressed file system.
Every element here is written out by hand, following the structure of files
saved by Guitar Pro; nothing is copied from Guitar Pro or its files.
"""

from __future__ import annotations

import pathlib
import zipfile
from xml.sax.saxutils import escape

HERE = pathlib.Path(__file__).resolve().parent
STEPS = {0: ("C", ""), 1: ("C", "#"), 2: ("D", ""), 3: ("D", "#"), 4: ("E", ""), 5: ("F", ""),
         6: ("F", "#"), 7: ("G", ""), 8: ("G", "#"), 9: ("A", ""), 10: ("A", "#"), 11: ("B", "")}


def cdata(text: str) -> str:
    return f"<![CDATA[{text}]]>"


def pitch_xml(midi: int, spelled: tuple[str, str] | None = None) -> str:
    step, accidental = spelled or STEPS[midi % 12]
    # GPIF octaves count from MIDI 0, so middle C (MIDI 60) is C5.
    return (f"<Pitch><Step>{step}</Step><Accidental>{accidental}</Accidental>"
            f"<Octave>{midi // 12}</Octave></Pitch>")


def prop(name: str, body: str = "<Enable />") -> str:
    return f'<Property name="{name}">{body}</Property>'


class Note:
    def __init__(self, *, string=None, fret=None, midi=None, concert=None, articulation=None,
                 props=(), elements=(), tie=None):
        self.string, self.fret, self.midi = string, fret, midi
        self.concert, self.articulation = concert, articulation
        self.props, self.elements, self.tie = list(props), list(elements), tie

    def xml(self, note_id: int) -> str:
        parts = [f'<Note id="{note_id}">']
        if self.tie:
            origin, destination = self.tie
            parts.append(f'<Tie origin="{str(origin).lower()}" '
                         f'destination="{str(destination).lower()}" />')
        parts += self.elements
        if self.articulation is not None:
            parts.append(f"<InstrumentArticulation>{self.articulation}</InstrumentArticulation>")
        props = list(self.props)
        if self.concert is not None:
            props.append(prop("ConcertPitch", pitch_xml(*self.concert)))
        if self.fret is not None:
            props.append(prop("Fret", f"<Fret>{self.fret}</Fret>"))
        if self.midi is not None:
            props.append(prop("Midi", f"<Number>{self.midi}</Number>"))
        if self.string is not None:
            props.append(prop("String", f"<String>{self.string}</String>"))
        if props:
            parts.append("<Properties>" + "".join(props) + "</Properties>")
        parts.append("</Note>")
        return "".join(parts)


class Beat:
    def __init__(self, value, notes=(), *, dots=0, tuplet=None, dynamic="MF", elements=(),
                 props=()):
        self.rhythm = (value, dots, tuplet)
        self.notes, self.dynamic = list(notes), dynamic
        self.elements, self.props = list(elements), list(props)


def channel_strip(volume: float, pan: float, automations=()) -> str:
    """The mixer of a track: parameter 11 is the pan and 12 the volume, both from
    0 to 1; automations are (type, bar, position, value)."""
    parameters = [0.5] * 16
    parameters[11], parameters[12] = pan, volume
    automation = "".join(
        f"<Automation><Type>{kind}</Type><Linear>false</Linear><Bar>{bar}</Bar>"
        f"<Position>{position}</Position><Visible>true</Visible><Value>{value}</Value>"
        "</Automation>" for kind, bar, position, value in automations)
    return (f'<RSE><ChannelStrip version="E56"><Parameters>'
            f'{" ".join(str(p) for p in parameters)}</Parameters>'
            f"<Automations>{automation}</Automations></ChannelStrip></RSE>")


class Track:
    def __init__(self, name, *, set_type, set_name, program, staves, transpose=(0, 0),
                 articulations=(), lyrics="", volume=0.795, pan=0.5, state="Default",
                 automations=()):
        self.name, self.set_type, self.set_name = name, set_type, set_name
        self.program, self.staves, self.transpose = program, staves, transpose
        self.articulations, self.lyrics = list(articulations), lyrics
        self.volume, self.pan, self.state = volume, pan, state
        self.automations = list(automations)

    def xml(self, track_id: int) -> str:
        elements = "".join(
            f"<Element><Name>{escape(name)}</Name><Type>{escape(name)}</Type><Articulations>"
            f"<Articulation><Name>{escape(name)}</Name><StaffLine>0</StaffLine>"
            f"<OutputMidiNumber>{key}</OutputMidiNumber></Articulation></Articulations></Element>"
            for name, key in (self.articulations or [("Pitched", 0)]))
        staves = []
        for staff in self.staves:
            props = []
            if staff.get("tuning"):
                props.append(prop("CapoFret", f"<Fret>{staff.get('capo', 0)}</Fret>"))
                props.append(prop("FretCount", "<Number>24</Number>"))
                pitches = " ".join(str(p) for p in staff["tuning"])
                props.append(prop("Tuning", f"<Pitches>{pitches}</Pitches><Flat></Flat>"
                                            f"<Instrument>Guitar</Instrument><Label>{cdata('')}"
                                            f"</Label><LabelVisible>true</LabelVisible>"))
            staves.append("<Staff><Properties>" + "".join(props) + "</Properties></Staff>")
        chromatic, octave = self.transpose
        lyrics = (f'<Lyrics dispatched="true"><Line><Text>{cdata(self.lyrics)}</Text>'
                  f"<Offset>0</Offset></Line></Lyrics>")
        return (f'<Track id="{track_id}"><Name>{cdata(self.name)}</Name>'
                f"<ShortName>{cdata(self.name[:5])}</ShortName><Color>200 100 100</Color>"
                f"<InstrumentSet><Name>{escape(self.set_name)}</Name>"
                f"<Type>{self.set_type}</Type><LineCount>5</LineCount>"
                f"<Elements>{elements}</Elements></InstrumentSet>"
                f"<Transpose><Chromatic>{chromatic}</Chromatic><Octave>{octave}</Octave>"
                f"</Transpose>{channel_strip(self.volume, self.pan, self.automations)}"
                f"<PlaybackState>{self.state}</PlaybackState>"
                f"<Sounds><Sound><Name>{cdata(self.set_name)}</Name>"
                f"<Label>{cdata(self.set_name)}</Label><Path>Corpus</Path><Role>Factory</Role>"
                f"<MIDI><LSB>0</LSB><MSB>0</MSB><Program>{self.program}</Program></MIDI>"
                f"</Sound></Sounds>{lyrics}<Staves>{''.join(staves)}</Staves></Track>")


GP8_HEADER = ("<GPVersion>8.0.2</GPVersion><GPRevision>13002</GPRevision>"
              "<Encoding><EncodingDescription>GP8</EncodingDescription></Encoding>")


def score(*, info, tempos, tracks, master_bars, header=GP8_HEADER) -> str:
    """master_bars: dicts with time, key (fifths, mode), extra XML and one bar per
    staff of every track: (clef, [voice beats...])."""
    rhythms: dict = {}
    bars, voices, beats, notes = [], [], [], []

    def rhythm_id(rhythm) -> int:
        if rhythm not in rhythms:
            rhythms[rhythm] = len(rhythms)
        return rhythms[rhythm]

    master_xml = []
    for master in master_bars:
        bar_ids = []
        for clef, bar_voices in master["bars"]:
            voice_ids = []
            for voice in bar_voices:
                beat_ids = []
                for beat in voice:
                    note_ids = []
                    for note in beat.notes:
                        notes.append(note.xml(len(notes)))
                        note_ids.append(len(notes) - 1)
                    parts = [f'<Beat id="{len(beats)}">', f"<Dynamic>{beat.dynamic}</Dynamic>"]
                    parts += beat.elements
                    parts.append(f'<Rhythm ref="{rhythm_id(beat.rhythm)}" />')
                    if note_ids:
                        parts.append("<Notes>" + " ".join(map(str, note_ids)) + "</Notes>")
                    if beat.props:
                        parts.append("<Properties>" + "".join(beat.props) + "</Properties>")
                    parts.append("</Beat>")
                    beats.append("".join(parts))
                    beat_ids.append(len(beats) - 1)
                voices.append(f'<Voice id="{len(voices)}"><Beats>'
                              + " ".join(map(str, beat_ids)) + "</Beats></Voice>")
                voice_ids.append(len(voices) - 1)
            voice_ids += [-1] * (4 - len(voice_ids))
            bars.append(f'<Bar id="{len(bars)}"><Clef>{clef}</Clef><Voices>'
                        + " ".join(map(str, voice_ids)) + "</Voices></Bar>")
            bar_ids.append(len(bars) - 1)
        fifths, mode = master.get("key", (0, "Major"))
        master_xml.append(
            f"<MasterBar><Key><AccidentalCount>{fifths}</AccidentalCount><Mode>{mode}</Mode>"
            f"<TransposeAs>Sharps</TransposeAs></Key><Time>{master.get('time', '4/4')}</Time>"
            + master.get("extra", "") + "<Bars>" + " ".join(map(str, bar_ids)) + "</Bars>"
            + "</MasterBar>")

    rhythm_xml = []
    for (value, dots, tuplet), index in rhythms.items():
        parts = [f'<Rhythm id="{index}"><NoteValue>{value}</NoteValue>']
        if dots:
            parts.append(f'<AugmentationDot count="{dots}" />')
        if tuplet:
            parts.append(f'<PrimaryTuplet num="{tuplet[0]}" den="{tuplet[1]}" />')
        parts.append("</Rhythm>")
        rhythm_xml.append("".join(parts))

    info_xml = "".join(f"<{tag}>{cdata(info.get(tag, ''))}</{tag}>"
                       for tag in ("Title", "SubTitle", "Artist", "Album", "Words", "Music",
                                   "WordsAndMusic", "Copyright", "Tabber", "Instructions",
                                   "Notices"))
    tempo_xml = "".join(
        f"<Automation><Type>Tempo</Type><Linear>{str(linear).lower()}</Linear><Bar>{bar}</Bar>"
        f"<Position>{position}</Position><Visible>true</Visible><Value>{value}</Value>"
        f"</Automation>" for bar, position, value, linear in tempos)
    track_ids = " ".join(str(i) for i in range(len(tracks)))
    return ('<?xml version="1.0" encoding="utf-8"?>\n<GPIF>' + header +
            f"<Score>{info_xml}<MultiVoice>0</MultiVoice></Score>"
            f"<MasterTrack><Tracks>{track_ids}</Tracks><Automations>{tempo_xml}</Automations>"
            "</MasterTrack>"
            "<Tracks>" + "".join(t.xml(i) for i, t in enumerate(tracks)) + "</Tracks>"
            "<MasterBars>" + "".join(master_xml) + "</MasterBars>"
            "<Bars>" + "".join(bars) + "</Bars>"
            "<Voices>" + "".join(voices) + "</Voices>"
            "<Beats>" + "".join(beats) + "</Beats>"
            "<Notes>" + "".join(notes) + "</Notes>"
            "<Rhythms>" + "".join(rhythm_xml) + "</Rhythms>"
            "</GPIF>\n")


def part_configuration(flags: list[int]) -> bytes:
    """Guitar Pro's PartConfiguration with one score view, the full score: the
    number of views, then for each view a byte (multi-measure rests), its number
    of tracks and a byte per track (1 standard notation, 2 tablature), then the
    view on screen; numbers are big-endian."""
    def number(value: int) -> bytes:
        return value.to_bytes(4, "big")
    return number(1) + bytes([0]) + number(len(flags)) + bytes(flags) + number(0)


def gp_file(gpif: str, parts: bytes | None = None) -> bytes:
    import io
    buffer = io.BytesIO()
    entries: list[tuple[str, str | bytes]] = [("VERSION", "7.0"), ("meta.json", "{\n}\n"),
                                              ("Content/score.gpif", gpif)]
    if parts is not None:
        entries.append(("Content/PartConfiguration", parts))
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries:
            entry = zipfile.ZipInfo(name, date_time=(2026, 11, 23, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    return buffer.getvalue()


# ---- guitar-techniques.gp --------------------------------------------------------

DROP_D = [38, 45, 50, 55, 59, 64]  # lowest string first
CAPO = 2


def fretted(string: int, fret: int, **kwargs) -> Note:
    """string counts from 0, the lowest; the MIDI number includes the capo."""
    return Note(string=string, fret=fret, midi=DROP_D[string] + CAPO + fret, **kwargs)


CRESCENDO, DIMINUENDO = "<Hairpin>Crescendo</Hairpin>", "<Hairpin>Decrescendo</Hairpin>"


def guitar_techniques() -> str:
    guitar = Track("Lead Guitar", set_type="electricGuitar", set_name="Electric Guitar",
                   program=27, transpose=(0, -1), staves=[{"tuning": DROP_D, "capo": CAPO}])
    bend = (prop("Bended") + prop("BendOriginValue", "<Float>0</Float>")
            + prop("BendOriginOffset", "<Float>0</Float>")
            + prop("BendDestinationValue", "<Float>100</Float>")
            + prop("BendDestinationOffset", "<Float>60</Float>"))
    bar1 = [[
        Beat("Quarter", [fretted(0, 0, props=[prop("PalmMuted")])]),
        Beat("Eighth", [fretted(1, 0, props=[prop("HopoOrigin")])], tuplet=(3, 2)),
        Beat("Eighth", [fretted(1, 2, props=[prop("HopoDestination")])], tuplet=(3, 2)),
        Beat("Eighth", [fretted(2, 0)], tuplet=(3, 2)),
        # A crescendo over the last two beats leads to the forte of bar 2.
        Beat("Quarter", [fretted(3, 2, props=[bend], elements=["<Vibrato>Slight</Vibrato>"])],
             elements=[CRESCENDO]),
        Beat("Quarter", [fretted(4, 12, props=[prop("HarmonicType", "<HType>Natural</HType>"),
                                               prop("HarmonicFret", "<HFret>12</HFret>")],
                                 elements=["<LetRing />"])], elements=[CRESCENDO]),
    ]]
    bar2 = [[
        Beat("Half", [fretted(5, 3, tie=(True, False))], dynamic="F"),
        Beat("Quarter", [fretted(5, 3, tie=(False, True))], dynamic="F"),
        Beat("Quarter", [fretted(0, 0, props=[prop("Muted")], elements=["<Accent>8</Accent>"])],
             dynamic="FF"),
    ]]
    bar3 = [[
        Beat("Eighth", [fretted(2, 7)], elements=["<GraceNotes>BeforeBeat</GraceNotes>"]),
        Beat("Quarter", [fretted(2, 5, props=[prop("Slide", "<Flags>1</Flags>")],
                                 elements=["<Accent>1</Accent>"])], dots=1,
             props=[prop("PickStroke", "<Direction>Down</Direction>")], elements=[DIMINUENDO]),
        Beat("Eighth", [fretted(2, 7)], elements=[DIMINUENDO]),
        Beat("Quarter", [], dynamic="MF"),
    ]]
    return score(
        info={"Title": "Corpus Guitar Techniques", "Artist": "Ghost in the Groove",
              "Copyright": "CC0 1.0", "Tabber": "Elara contributors"},
        tempos=[(0, 0, "100 2", False), (1, 0.5, "80 2", False)],
        tracks=[guitar],
                # |: 1 |1.2. 2 :|3. 3 |, played three times
        master_bars=[
            {"time": "4/4", "key": (1, "Major"), "bars": [("G2", bar1)],
             "extra": '<Repeat start="true" end="false" count="0" />'},
            {"time": "4/4", "key": (1, "Major"), "bars": [("G2", bar2)],
             "extra": '<Repeat start="false" end="true" count="3" />'
                      "<AlternateEndings>1 2</AlternateEndings>"},
            {"time": "3/4", "key": (1, "Major"), "bars": [("G2", bar3)],
             "extra": "<AlternateEndings>3</AlternateEndings>"},
        ])


# ---- band.gp ---------------------------------------------------------------------

# Guitar Pro gives every staff a tuning, a piano's or a clarinet's too; its part
# configuration shows those tracks without tablature.
GUITAR = [40, 45, 50, 55, 59, 64]
FIVE_STRING_BASS = [23, 28, 33, 38, 43]
BAND_PARTS = [3, 1, 1, 1]  # bass: notation and tablature; the others: notation


def band() -> str:
    # The mixer: the bass a little left, the drums louder, and the clarinet
    # starting quieter (an automation at the start) and getting louder later.
    bass = Track("Bass", set_type="electricBass", set_name="Electric Bass", program=33,
                 transpose=(0, -1), staves=[{"tuning": [28, 33, 38, 43]}], volume=0.7, pan=0.3)
    piano = Track("Piano", set_type="grandPiano", set_name="Grand Piano", program=0,
                  staves=[{"tuning": GUITAR}, {"tuning": FIVE_STRING_BASS}])
    drums = Track("Drums", set_type="drumKit", set_name="Drums", program=0,
                  staves=[{}], articulations=[("snare", 38), ("hiHat", 42), ("kickDrum", 36)],
                  volume=0.9)
    clarinet = Track("Clarinet", set_type="clarinet", set_name="Clarinet in Bb", program=71,
                     transpose=(-2, 0), staves=[{"tuning": GUITAR}], lyrics="la la", pan=0.75,
                     automations=[("DSPParam_12", 0, 0, 0.6), ("DSPParam_12", 1, 0.5, 0.8)])

    def bass_note(string, fret):
        return Note(string=string, fret=fret, midi=[28, 33, 38, 43][string] + fret)

    def flat(midi):  # spelled with a flat in F major
        names = {10: ("B", "b"), 3: ("E", "b")}
        return (midi, names.get(midi % 12))

    bars = []
    for measure in range(2):
        bass_bar = [[Beat("Half", [bass_note(0, 1)]), Beat("Half", [bass_note(1, 3)])]]
        # The first chord is rolled: upwards in measure 1, downwards in measure 2.
        roll = "Up" if measure == 0 else "Down"
        right = [
            [Beat("Quarter", [Note(midi=65), Note(midi=69), Note(midi=72)],
                  elements=[f"<Arpeggio>{roll}</Arpeggio>"]),
             Beat("Quarter", [Note(concert=flat(70))]),
             Beat("Half", [Note(midi=69)])],
            [Beat("Whole", [Note(midi=77)])],  # a second voice
        ]
        left = [[Beat("Whole", [Note(midi=41), Note(midi=48)])]]
        kit = [[Beat("Eighth", [Note(articulation=1)] + ([Note(articulation=2)] if i % 4 == 0
                                                          else [Note(articulation=0)] if i % 4 == 2
                                                          else []))
                for i in range(8)]]
        if measure == 0:
            horn = [[Beat("Quarter", [Note(concert=flat(70))]),
                     Beat("Quarter", [Note(concert=(72, None))]),
                     Beat("Half", [Note(concert=flat(75))])]]
        else:
            horn = [[Beat("Half", [Note(concert=(77, None))])]]  # filled with rests on import
        bars.append({"time": "4/4", "key": (-1, "Major"),
                     "bars": [("F4", bass_bar), ("G2", right), ("F4", left), ("Neutral", kit),
                              ("G2", horn)]})
    return score(
        info={"Title": "Corpus Band", "WordsAndMusic": "Ghost in the Groove",
              "Copyright": "CC0 1.0"},
        tempos=[(0, 0, "90 2", False), (1, 0, "60 3", True)],
        tracks=[bass, piano, drums, clarinet],
        master_bars=bars)


# ---- duet.gpx (Guitar Pro 6) -----------------------------------------------------

SECTOR = 0x1000


class Gp6Track:
    """A Guitar Pro 6 track: no instrument set and no staves; the General MIDI
    table says whether it is a drum kit, and the tuning and capo sit on the track."""

    def __init__(self, name, *, program, channel, percussion=False, tuning=None, capo=0,
                 flat=False, volume=0.795, pan=0.5, state="Default"):
        self.name, self.program, self.channel = name, program, channel
        self.percussion, self.tuning, self.capo, self.flat = percussion, tuning, capo, flat
        self.volume, self.pan, self.state = volume, pan, state

    def xml(self, track_id: int) -> str:
        table = "Percussion" if self.percussion else "Instrument"
        props = []
        if self.tuning:
            props.append(prop("CapoFret", f"<Fret>{self.capo}</Fret>"))
            props.append(prop("FretCount", "<Number>21</Number>"))
            props.append(prop("Tuning", "<Pitches>" + " ".join(map(str, self.tuning))
                              + "</Pitches>"))
            if self.flat:
                props.append(prop("TuningFlat"))
        properties = "<Properties>" + "".join(props) + "</Properties>" if props else ""
        return (f'<Track id="{track_id}"><Name>{cdata(self.name)}</Name>'
                f"<ShortName>{cdata(self.name[:5])}</ShortName><Color>80 160 220</Color>"
                f'<GeneralMidi table="{table}"><Program>{self.program}</Program><Port>0</Port>'
                f"<PrimaryChannel>{self.channel}</PrimaryChannel>"
                f"<SecondaryChannel>{self.channel}</SecondaryChannel>"
                "<ForeOneChannelPerString>false</ForeOneChannelPerString></GeneralMidi>"
                f"{channel_strip(self.volume, self.pan)}"
                f"<PlaybackState>{self.state}</PlaybackState>{properties}</Track>")


class BitWriter:
    """Bits from the most significant down, byte after byte."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.used = 8

    def bit(self, value: int) -> None:
        if self.used == 8:
            self.data.append(0)
            self.used = 0
        if value:
            self.data[-1] |= 0x80 >> self.used
        self.used += 1

    def bits(self, value: int, count: int) -> None:  # the most significant bit first
        for i in reversed(range(count)):
            self.bit((value >> i) & 1)

    def bits_reversed(self, value: int, count: int) -> None:  # the least significant first
        for i in range(count):
            self.bit((value >> i) & 1)


def bcfs(files: list[tuple[str, bytes]]) -> bytes:
    """A Guitar Pro 6 file system: 4096-byte sectors, the first one unused. Each
    file is an entry sector (type 2, the name, the size, the sectors that hold
    it) followed by its data sectors."""
    sectors = [bytes(SECTOR)]
    for name, data in files:
        count = max(1, -(-len(data) // SECTOR))
        entry = bytearray(SECTOR)
        entry[0:4] = (2).to_bytes(4, "little")
        encoded = name.encode("ascii")
        entry[0x04:0x04 + len(encoded)] = encoded
        entry[0x8C:0x90] = len(data).to_bytes(4, "little")
        first = len(sectors) + 1
        for i in range(count):
            entry[0x94 + 4 * i:0x98 + 4 * i] = (first + i).to_bytes(4, "little")
        sectors.append(bytes(entry))
        for i in range(count):
            chunk = data[i * SECTOR:(i + 1) * SECTOR]
            sectors.append(chunk + bytes(SECTOR - len(chunk)))
    return b"BCFS" + b"".join(sectors)


def bcfz(data: bytes) -> bytes:
    """Compresses `data` into the chunks Guitar Pro 6 unpacks: a 0 bit and up to
    three literal bytes, or a 1 bit, a word size and an offset and length of that
    many bits that copy earlier bytes (never more than the offset)."""
    window = (1 << 12) - 1
    out = BitWriter()
    starts: dict[bytes, list[int]] = {}
    literals = bytearray()

    def flush() -> None:
        for at in range(0, len(literals), 3):
            chunk = literals[at:at + 3]
            out.bit(0)
            out.bits_reversed(len(chunk), 2)
            for byte in chunk:
                out.bits(byte, 8)
        literals.clear()

    i = 0
    while i < len(data):
        best_length = best_offset = 0
        key = data[i:i + 3]
        for start in reversed(starts.get(key, [])[-32:]):
            offset = i - start
            if offset > window:
                break
            limit = min(offset, window, len(data) - i)
            length = 0
            while length < limit and data[start + length] == data[i + length]:
                length += 1
            if length > best_length:
                best_length, best_offset = length, offset
        starts.setdefault(key, []).append(i)
        if best_length >= 4:
            flush()
            word = max(best_offset.bit_length(), best_length.bit_length())
            out.bit(1)
            out.bits(word, 4)
            out.bits_reversed(best_offset, word)
            out.bits_reversed(best_length, word)
            i += best_length
        else:
            literals.append(data[i])
            i += 1
    flush()
    return b"BCFZ" + len(data).to_bytes(4, "little") + bytes(out.data)


EB_STANDARD = [39, 44, 49, 54, 58, 63]  # lowest string first
DUET_CAPO = 2


def duet() -> bytes:
    # The mixer: the guitar quieter and to the left, the drums muted.
    guitar = Gp6Track("Rhythm Guitar", program=25, channel=0, tuning=EB_STANDARD,
                      capo=DUET_CAPO, flat=True, volume=0.7, pan=0.25)
    drums = Gp6Track("Drums", program=0, channel=9, percussion=True, state="Mute")

    def note(string, fret, **kwargs):
        return Note(string=string, fret=fret, midi=EB_STANDARD[string] + DUET_CAPO + fret,
                    **kwargs)

    kick, snare, hat, crash = (Note(midi=key) for key in (36, 38, 42, 49))
    element_only = Note(props=[prop("Element", "<Element>1</Element>"),
                               prop("Variation", "<Variation>0</Variation>")])
    bar1 = [[Beat("Quarter", [note(0, 0), note(1, 2), note(2, 2)]),
             Beat("Quarter", [note(3, 0)]),
             Beat("Half", [note(4, 1, elements=["<LetRing />"])])]]
    bar2 = [[Beat("Whole", [note(5, 0)])]]
    kit1 = [[Beat("Quarter", [kick, hat]), Beat("Quarter", [hat]),
             Beat("Quarter", [snare, hat]), Beat("Quarter", [hat])]]
    kit2 = [[Beat("Half", [crash, kick]), Beat("Half", [element_only])]]
    gpif = score(
        info={"Title": "Corpus Duet", "Artist": "Ghost in the Groove", "Copyright": "CC0 1.0"},
        tempos=[(0, 0, "132", False)],
        tracks=[guitar, drums],
        master_bars=[
            {"time": "4/4", "key": (-3, "Major"), "bars": [("G2", bar1), ("Neutral", kit1)]},
            {"time": "4/4", "key": (-3, "Major"), "bars": [("G2", bar2), ("Neutral", kit2)]},
        ],
        header="<GPRevision>0</GPRevision>")
    misc = b'<?xml version="1.0" encoding="utf-8"?>\n<Misc></Misc>\n'
    parts = bytes([0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0])  # neutral part settings
    return bcfz(bcfs([("misc.xml", misc), ("score.gpif", gpif.encode("utf-8")),
                      ("PartConfiguration", parts)]))


# ---- double-coda.gp ---------------------------------------------------------------

def double_coda() -> str:
    """1 | 2 segno | 3 segno segno | 4 da coda, da double coda | 5 D.S.S. al Double
    Coda | 6 coda | 7 double coda: played 1 2 3 4 5 3 4 7."""
    guitar = Track("Guitar", set_type="electricGuitar", set_name="Electric Guitar", program=27,
                   staves=[{"tuning": GUITAR}])
    directions = ["", "<Target>Segno</Target>", "<Target>SegnoSegno</Target>",
                  "<Jump>DaCoda</Jump><Jump>DaDoubleCoda</Jump>",
                  "<Jump>DaSegnoSegnoAlDoubleCoda</Jump>", "<Target>Coda</Target>",
                  "<Target>DoubleCoda</Target>"]
    # Sections: one with a letter and a text, one with a letter alone.
    sections = {0: ("A", "Intro"), 5: ("B", "")}
    master_bars = []
    for measure, direction in enumerate(directions):
        bar = [[Beat("Whole", [Note(string=5, fret=measure, midi=GUITAR[5] + measure)])]]
        extra = f"<Directions>{direction}</Directions>" if direction else ""
        if measure in sections:
            letter, text = sections[measure]
            extra += (f"<Section><Letter>{cdata(letter)}</Letter>"
                      f"<Text>{cdata(text)}</Text></Section>")
        master_bars.append({"time": "4/4", "bars": [("G2", bar)], "extra": extra})
    return score(
        info={"Title": "Corpus Navigation", "Artist": "Ghost in the Groove",
              "Copyright": "CC0 1.0", "Tabber": "Elara contributors"},
        tempos=[(0, 0, "120 2", False)], tracks=[guitar], master_bars=master_bars)


FILES = {
    "guitar-techniques.gp": lambda: gp_file(guitar_techniques()),
    "double-coda.gp": lambda: gp_file(double_coda()),
        "band.gp": lambda: gp_file(band(), part_configuration(BAND_PARTS)),
    "duet.gpx": duet,
}


def main() -> None:
    for name, make in FILES.items():
        (HERE / name).write_bytes(make())
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
