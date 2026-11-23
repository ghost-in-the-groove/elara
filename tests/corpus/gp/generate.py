# SPDX-License-Identifier: CC0-1.0
"""Writes the Guitar Pro 7-8 files (.gp) of the test corpus, the same on every run.

    python tests/corpus/gp/generate.py

A .gp file is a zip archive whose Content/score.gpif holds the score as XML.
Every element here is written out by hand, following the structure of files
saved by Guitar Pro 8; nothing is copied from Guitar Pro or its files.
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


class Track:
    def __init__(self, name, *, set_type, set_name, program, staves, transpose=(0, 0),
                 articulations=(), lyrics=""):
        self.name, self.set_type, self.set_name = name, set_type, set_name
        self.program, self.staves, self.transpose = program, staves, transpose
        self.articulations, self.lyrics = list(articulations), lyrics

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
                f"</Transpose><Sounds><Sound><Name>{cdata(self.set_name)}</Name>"
                f"<Label>{cdata(self.set_name)}</Label><Path>Corpus</Path><Role>Factory</Role>"
                f"<MIDI><LSB>0</LSB><MSB>0</MSB><Program>{self.program}</Program></MIDI>"
                f"</Sound></Sounds>{lyrics}<Staves>{''.join(staves)}</Staves></Track>")


def score(*, info, tempos, tracks, master_bars) -> str:
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
    return ('<?xml version="1.0" encoding="utf-8"?>\n<GPIF>'
            "<GPVersion>8.0.2</GPVersion><GPRevision>13002</GPRevision>"
            "<Encoding><EncodingDescription>GP8</EncodingDescription></Encoding>"
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


def gp_file(gpif: str) -> bytes:
    import io
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in (("VERSION", "7.0"), ("meta.json", "{\n}\n"),
                           ("Content/score.gpif", gpif)):
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
        Beat("Quarter", [fretted(3, 2, props=[bend], elements=["<Vibrato>Slight</Vibrato>"])]),
        Beat("Quarter", [fretted(4, 12, props=[prop("HarmonicType", "<HType>Natural</HType>"),
                                               prop("HarmonicFret", "<HFret>12</HFret>")],
                                 elements=["<LetRing />"])]),
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
             props=[prop("PickStroke", "<Direction>Down</Direction>")]),
        Beat("Eighth", [fretted(2, 7)]),
        Beat("Quarter", [], dynamic="MF"),
    ]]
    return score(
        info={"Title": "Corpus Guitar Techniques", "Artist": "Ghost in the Groove",
              "Copyright": "CC0 1.0", "Tabber": "Elara contributors"},
        tempos=[(0, 0, "100 2", False), (1, 0.5, "80 2", False)],
        tracks=[guitar],
        master_bars=[
            {"time": "4/4", "key": (1, "Major"), "bars": [("G2", bar1)]},
            {"time": "4/4", "key": (1, "Major"), "bars": [("G2", bar2)],
             "extra": '<Repeat start="true" end="false" count="0" />'},
            {"time": "3/4", "key": (1, "Major"), "bars": [("G2", bar3)]},
        ])


# ---- band.gp ---------------------------------------------------------------------

def band() -> str:
    bass = Track("Bass", set_type="electricBass", set_name="Electric Bass", program=33,
                 transpose=(0, -1), staves=[{"tuning": [28, 33, 38, 43]}])
    piano = Track("Piano", set_type="grandPiano", set_name="Grand Piano", program=0,
                  staves=[{}, {}])
    drums = Track("Drums", set_type="drumKit", set_name="Drums", program=0,
                  staves=[{}], articulations=[("snare", 38), ("hiHat", 42), ("kickDrum", 36)])
    clarinet = Track("Clarinet", set_type="clarinet", set_name="Clarinet in Bb", program=71,
                     transpose=(-2, 0), staves=[{}], lyrics="la la")

    def bass_note(string, fret):
        return Note(string=string, fret=fret, midi=[28, 33, 38, 43][string] + fret)

    def flat(midi):  # spelled with a flat in F major
        names = {10: ("B", "b"), 3: ("E", "b")}
        return (midi, names.get(midi % 12))

    bars = []
    for measure in range(2):
        bass_bar = [[Beat("Half", [bass_note(0, 1)]), Beat("Half", [bass_note(1, 3)])]]
        right = [
            [Beat("Quarter", [Note(midi=65), Note(midi=69), Note(midi=72)]),
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


FILES = {
    "guitar-techniques.gp": guitar_techniques,
    "band.gp": band,
}


def main() -> None:
    for name, make in FILES.items():
        (HERE / name).write_bytes(gp_file(make()))
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
