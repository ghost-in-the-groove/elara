// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>
#include <elara/io/native_format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = elara::core;
using elara::io::decodeNative;
using elara::io::encodeNative;
using elara::io::loadNative;
using elara::io::saveNative;
using elara::io::songFromJson;
using elara::io::songToJson;

namespace {

core::Beat beat(const char* duration, std::vector<core::Note> notes = {}) {
    return core::Beat{.duration = *core::Duration::parse(duration), .notes = std::move(notes)};
}

core::Track preset(const char* id) {
    return core::Track::fromPreset(*core::findInstrumentPreset(id));
}

// A song that uses every part of the model with non-default values.
core::Song richSong() {
    using namespace core;
    Song song;
    song.info() = SongInfo{.title = "Étude \"No. 1\"",
                           .subtitle = "for guitar",
                           .artist = "Ghost",
                           .album = "Tests",
                           .words = "",
                           .music = "Ghost",
                           .copyright = "CC0",
                           .transcriber = "Elara",
                           .notes = "line one\nline two"};
    song.setStartTempo(Tempo(72.5, Duration(NoteValue::Quarter, 1)));

    Track guitar("Lead",
                 Instrument{.name = "Electric Guitar (clean)",
                            .kind = InstrumentKind::Fretted,
                            .midiProgram = 27,
                            .writtenOffset = 12},
                 {Staff(Clef::Treble8vb, Tuning::preset("guitar-drop-d"), 2)});
    song.addTrack(std::move(guitar));
    song.addTrack(preset("five-string-bass"));
    song.addTrack(preset("piano"));
    song.addTrack(preset("drum-kit"));
    song.appendMeasures(5);
    song.track(0).setMix(TrackMix{.volume = 0.65, .pan = -0.5, .mute = false, .solo = true});
    song.track(3).setMix(TrackMix{.volume = 1.0, .pan = 0.0, .mute = true, .solo = false});

    song.measureHeader(1).timeSignature = TimeSignature(6, 8);
    song.measureHeader(1).keySignature = KeySignature(-3, KeyMode::Minor);
    song.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(0), .tempo = Tempo(60, Duration(NoteValue::Quarter, 1))},
        TempoChange{.offset = Fraction(3, 8), .tempo = Tempo(90), .gradual = true}};
    song.measureHeader(2).keySignature = KeySignature(4, KeyMode::Major);
    // |: 1 |1.2. 2 :|3. 3 |, played three times
    song.measureHeader(1).repeatStart = true;
    song.measureHeader(2).repeatTimes = 3;
    song.measureHeader(2).alternateEndings = 0b011;
    song.measureHeader(3).alternateEndings = 0b100;
    // Every sign on measure 0, what closes measure 1 on measure 1 (ADR-0049).
    song.measureHeader(0).segno = true;
    song.measureHeader(0).segnoSegno = true;
    song.measureHeader(0).coda = true;
    song.measureHeader(0).doubleCoda = true;
    song.measureHeader(1).fine = true;
    song.measureHeader(1).toCoda = true;
    song.measureHeader(1).toDoubleCoda = true;
    song.measureHeader(1).jump = JumpTo::SegnoSegno;
    song.measureHeader(1).jumpThen = JumpThen::DoubleCoda;
    song.measureHeader(2).section = "Chorus";
    song.measureHeader(4).doubleBar = true;
    song.measureHeader(0).tripletFeel = elara::core::TripletFeel::DottedSixteenth;

    // Guitar, measure 0: every note effect, a chord, a tuplet and a second voice.
    Staff& lead = song.track(0).staff(0);
    Note bent = lead.frettedNote(3, 7);
    bent.effects.bend = BendCurve::bendRelease(200);
    bent.effects.vibrato = Vibrato::Wide;
    bent.effects.accent = Accent::Marcato;
    bent.effects.dynamic = Dynamic::FF;
    Note harmonic = lead.frettedNote(2, 12);
    harmonic.effects.harmonic = Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 120};
    harmonic.effects.letRing = true;
    Note slid = lead.frettedNote(1, 5, SpellingPreference::Flats);
    slid.effects.slideIn = SlideIn::FromBelow;
    slid.effects.slideOut = SlideOut::Legato;
    slid.effects.staccato = true;
    slid.effects.tenuto = true; // portato (ADR-0088)
    slid.effects.ghost = true;
    Note muted = lead.frettedNote(6, 0);
    muted.effects.palmMute = true;
    muted.effects.staccatissimo = true; // ADR-0094
    muted.effects.deadNote = true;
    Note hammer = lead.frettedNote(6, 2);
    hammer.effects.legatoFromPrevious = true;
    hammer.effects.trill = Trill{.semitones = 2, .speed = NoteValue::Sixteenth}; // ADR-0090
    muted.effects.ornament = elara::core::Ornament::InvertedTurn;                // ADR-0091
    hammer.effects.fingering = elara::core::Finger::Index;                       // ADR-0093
    hammer.effects.pluck = elara::core::Finger::Ring;
    Note tied = lead.frettedNote(6, 2);
    tied.effects.tiedFromPrevious = true;
    Note semi = lead.frettedNote(4, 3);
    semi.effects.harmonic = Harmonic{.kind = HarmonicKind::Semi, .fretTenths = 32};

    Beat strummed = beat("8", {bent, harmonic, slid});
    strummed.effects.strum = Strum::Down;
    strummed.effects.pickStroke = PickStroke::Up;
    strummed.effects.whammyBar = BendCurve({{0, 0}, {100, -1200}});
    strummed.effects.tremoloPicking = NoteValue::ThirtySecond;
    strummed.effects.hairpin = Hairpin::Crescendo;
    auto& first = lead.measure(0).voices[0].beats;
    first = {strummed,   beat("8*3:2", {muted}), beat("8*3:2", {hammer}), beat("8*3:2", {tied}),
             beat("4."), beat("16..", {semi})};
    lead.measure(0).voices[2].beats = {beat("2"), beat("2")}; // voice 1 empty, voice 2 not
    // Grace notes (ADR-0079): a slide into the semi-harmonic beat's string,
    // then an on-beat dead note.
    Note graceSlide = lead.frettedNote(4, 1);
    graceSlide.effects.slideOut = SlideOut::Legato;
    Note graceDead = lead.frettedNote(4, 2);
    graceDead.effects.deadNote = true;
    first.back().graces = {
        Grace{.duration = Duration(NoteValue::Sixteenth), .notes = {graceSlide}},
        Grace{.duration = *Duration::parse("16*3:2"), .notes = {graceDead}, .onBeat = true}};
    // Chord symbols (ADR-0080): with a diagram, and a name alone on a rest.
    const ChordDiagram e7{.firstFret = 1, .frets = {0, 3, 1, 0, 2, 0}};
    first.front().chord = SharedOptional(ChordSymbol{.name = "E7", .diagram = e7});
    lead.measure(0).voices[2].beats[1].chord =
        SharedOptional(ChordSymbol{.name = "N.C.", .diagram = {}});
    // Beat text (ADR-0081).
    lead.measure(0).voices[2].beats[0].text = SharedOptional<std::string>("let ring");
    // Lyrics (ADR-0084): a syllable joined to the next, a verse that sings
    // nothing there, a third verse held over the notes after it.
    first[1].lyrics = SharedOptional(std::vector<Syllable>{
        {.text = "be", .hyphen = true}, {}, {.text = "low", .hyphen = false, .extend = true}});

    // Bass with techniques, piano on both staves, drums by key; a breve somewhere.
    Staff& bass = song.track(1).staff(0);
    Beat slap = beat("4", {bass.frettedNote(5, 0)});
    slap.effects.bassTechnique = BassTechnique::Slap;
    Beat softer = beat("2", {bass.frettedNote(1, 3)});
    softer.effects.hairpin = Hairpin::Diminuendo;
    softer.effects.fermata = Fermata::Long; // ADR-0085
    softer.effects.slur = true;             // ADR-0086
    bass.measure(3).voices[0].beats = {slap, beat("4"), softer};
    bass.measure(3).voices[0].beats[1].effects.fermata = Fermata::Short; // over a rest
    song.track(2).staff(0).measure(2).voices[0].beats = {
        beat("1", {Note{.pitch = Pitch(Step::C, 0, 5)}, Note{.pitch = Pitch(Step::E, -1, 5)}})};
    song.track(2).staff(1).measure(2).voices[0].beats = {
        beat("1", {Note{.pitch = Pitch(Step::C, 1, 3)}})};
    // Octave lines on the piano, 15ma over its right hand (ADR-0089).
    song.track(2).staff(0).measure(2).voices[0].beats[0].effects.ottava = Ottava::QuindicesimaAlta;
    song.track(2).staff(1).measure(2).voices[0].beats[0].effects.ottava = Ottava::Bassa;
    // The sustain pedal under the left hand (ADR-0092).
    song.track(2).staff(1).measure(2).voices[0].beats[0].effects.pedal = Pedal::Change;
    song.track(2).staff(1).measure(2).voices[0].beats[0].effects.breath =
        elara::core::Breath::Caesura; // ADR-0095
    // Clef changes, one on an empty measure at the end (ADR-0087).
    song.track(2).staff(1).measure(1).clef = Clef::Treble;
    song.track(2).staff(1).measure(4).clef = Clef::Tenor;
    // Measure repeats (ADR-0102): the bass repeats its last measure, the
    // piano's right hand the two before.
    using elara::core::MeasureRepeat;
    bass.measure(4).repeat = MeasureRepeat::One;
    song.track(2).staff(0).measure(3).repeat = MeasureRepeat::TwoFirst;
    song.track(2).staff(0).measure(4).repeat = MeasureRepeat::TwoSecond;
    song.track(3).staff(0).measure(4).voices[0].beats = {
        beat("breve", {Note{.pitch = Pitch::fromMidi(36)}, Note{.pitch = Pitch::fromMidi(42)}})};
    // Layout adjustments (ADR-0152): the song's, and the bass's own.
    std::vector<MeasureLayout> layouts(5);
    layouts[2].breakBefore = Break::System;
    layouts[4] = MeasureLayout{.breakBefore = Break::Page, .spacing = 120};
    song.setMeasureLayouts(LayoutScope::song(), MeasureLayouts(layouts));
    std::vector<MeasureLayout> bassLayouts(5);
    bassLayouts[3].breakBefore = Break::Keep;
    song.setMeasureLayouts(LayoutScope::ofTrack(1), MeasureLayouts(bassLayouts));
    return song;
}

std::filesystem::path tempFile(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TEST_CASE("An empty song survives a JSON round trip", "[io][native]") {
    const auto json = songToJson(core::Song{});
    const auto loaded = songFromJson(json);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == core::Song{});
    CHECK(json.find(R"("format":"elara","formatVersion":1)") != std::string::npos);
}

TEST_CASE("Every part of the model survives a JSON round trip", "[io][native]") {
    const core::Song song = richSong();
    const auto loaded = songFromJson(songToJson(song));
    REQUIRE(loaded.has_value());
    CHECK(loaded->info() == song.info());
    CHECK(loaded->startTempo() == song.startTempo());
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        CHECK(loaded->measureHeader(m) == song.measureHeader(m));
    }
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        CHECK(loaded->track(t) == song.track(t));
    }
    CHECK(*loaded == song);
}

TEST_CASE("Beats naming the same chord share one symbol once read", "[io][native]") {
    core::Song song = richSong();
    auto& beats = song.track(0).staff(0).measure(0).voices[0].beats;
    beats[1].chord = core::SharedOptional(*beats[0].chord); // equal, not shared
    REQUIRE(&*beats[1].chord != &*beats[0].chord);
    const auto loaded = songFromJson(songToJson(song));
    REQUIRE(loaded.has_value());
    const auto& read = loaded->track(0).staff(0).measure(0).voices[0].beats;
    CHECK(&*read[1].chord == &*read[0].chord);
    CHECK_FALSE(read[4].chord);
    CHECK(*loaded == song);
}

TEST_CASE("Chord diagrams outside the neck are rejected", "[io][native]") {
    const std::string bad = songToJson(richSong());
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {R"("firstFret":1)", R"("firstFret":0)"},
             {R"("frets":[0,3,1,0,2,0])", R"("frets":[0,3,1,0,2,-2])"},
             {R"("frets":[0,3,1,0,2,0])", R"("frets":[])"}}) {
        std::string json = bad;
        json.replace(json.find(from), from.size(), to);
        CHECK_FALSE(songFromJson(json).has_value());
    }
}

TEST_CASE("song.json uses text forms and omits defaults", "[io][native]") {
    const auto json = songToJson(richSong());
    CHECK(json.find(R"("timeSignature":"6/8","keySignature":"C minor")") != std::string::npos);
    CHECK(json.find(R"("tuning":"D2 A2 D3 G3 B3 E4","capo":2)") != std::string::npos);
    CHECK(json.find(R"({"duration":"4."})") != std::string::npos); // a rest
    CHECK(json.find(R"("clef":"treble-8vb")") != std::string::npos);
    CHECK(
        json.find(
            R"("clef":"bass","clefs":[{"measure":1,"clef":"treble"},{"measure":4,"clef":"tenor"}],)") !=
        std::string::npos);
    CHECK(json.find(R"("clefs":[])") == std::string::npos);
    CHECK(json.find(R"("repeats":[{"measure":4,"repeat":"one"}])") != std::string::npos);
    CHECK(
        json.find(
            R"("repeats":[{"measure":3,"repeat":"two-first"},{"measure":4,"repeat":"two-second"}])") !=
        std::string::npos);
    CHECK(json.find(R"("repeats":[])") == std::string::npos);
    CHECK(json.find(R"("slideIn":"from-below")") != std::string::npos);
    CHECK(json.find(R"("trill":{"semitones":2,"speed":"16"})") != std::string::npos);
    CHECK(json.find(R"("ornament":"inverted-turn")") != std::string::npos);
    CHECK(json.find(R"("fingering":"index","pluck":"ring")") != std::string::npos);
    CHECK(json.find(R"("staccatissimo":true)") != std::string::npos);
    CHECK(json.find(R"("hairpin":"crescendo")") != std::string::npos);
    CHECK(json.find(R"("hairpin":"diminuendo")") != std::string::npos);
    CHECK(json.find(R"("hairpin":"none")") == std::string::npos);
    CHECK(json.find(R"("fermata":"long","slur":true)") != std::string::npos);
    CHECK(json.find(R"("slur":false)") == std::string::npos);
    CHECK(json.find(R"({"duration":"4","effects":{"fermata":"short"}})") != std::string::npos);
    CHECK(json.find(R"("fermata":"none")") == std::string::npos);
    CHECK(json.find(R"("effects":{"ottava":"15ma"})") != std::string::npos);
    CHECK(json.find(R"("effects":{"ottava":"8vb","pedal":"change","breath":"caesura"})") !=
          std::string::npos);
    CHECK(json.find(R"("pedal":"none")") == std::string::npos);
    CHECK(json.find(R"("ottava":"none")") == std::string::npos);
    CHECK(json.find(R"("dynamic":"mf")") == std::string::npos); // the default
    CHECK(json.find(R"("tiedFromPrevious":false)") == std::string::npos);
    CHECK(
        json.find(
            R"({"duration":"16..","graces":[{"duration":"16","notes":[{"pitch":"F3","string":4,"fret":1,"effects":{"slideOut":"legato"}}]},{"duration":"16*3:2","notes":[{"pitch":"F#3","string":4,"fret":2,"effects":{"deadNote":true}}],"onBeat":true}],"notes":)") !=
        std::string::npos);
    CHECK(json.find(R"("graces":[])") == std::string::npos);
    CHECK(
        json.find(
            R"({"duration":"8","chord":{"name":"E7","diagram":{"firstFret":1,"frets":[0,3,1,0,2,0]}},)") !=
        std::string::npos);
    CHECK(json.find(R"({"duration":"2","chord":{"name":"N.C."}})") != std::string::npos);
    CHECK(json.find(R"({"duration":"2","text":"let ring"})") != std::string::npos);
    CHECK(json.find(R"("lyrics":["be-","","low_"])") != std::string::npos);
    CHECK(json.find(R"("onBeat":false)") == std::string::npos);
    CHECK(json.find(R"("repeatStart":true)") != std::string::npos);
    CHECK(json.find(R"("repeatTimes":3,"alternateEndings":[1,2],"section":"Chorus"})") !=
          std::string::npos);
    CHECK(json.find(R"("section":"")") == std::string::npos);
    CHECK(json.find(R"({"alternateEndings":[3]})") != std::string::npos);
    CHECK(json.find(R"({"doubleBar":true})") != std::string::npos);
    CHECK(json.find(R"("doubleBar":false)") == std::string::npos);
    CHECK(json.find(R"("tripletFeel":"dotted-sixteenth","segno":true)") != std::string::npos);
    CHECK(json.find(R"("tripletFeel":"none")") == std::string::npos);
    CHECK(json.find(R"("repeatStart":false)") == std::string::npos);
    CHECK(json.find(R"("segno":true,"segnoSegno":true,"coda":true,"doubleCoda":true})") !=
          std::string::npos);
    CHECK(
        json.find(
            R"("repeatStart":true,"fine":true,"toCoda":true,"toDoubleCoda":true,"jump":"dal-segno-segno","jumpThen":"al-double-coda"})") !=
        std::string::npos);
    CHECK(json.find(R"("segno":false)") == std::string::npos);
    CHECK(json.find(R"("mix":{"volume":0.65,"pan":-0.5,"solo":true})") != std::string::npos);
    CHECK(json.find(R"("mix":{"volume":1,"mute":true})") != std::string::npos);
    CHECK(json.find(R"("mute":false)") == std::string::npos);
    // The bass and the piano keep the default mix: no "mix" at all.
    std::size_t mixes = 0;
    for (auto at = json.find(R"("mix":)"); at != std::string::npos;
         at = json.find(R"("mix":)", at + 1)) {
        ++mixes;
    }
    CHECK(mixes == 2);
}

TEST_CASE("Reading song.json reports what is wrong and where", "[io][native]") {
    const auto error = [](std::string_view json) {
        const auto result = songFromJson(json);
        REQUIRE_FALSE(result.has_value());
        return result.error().message;
    };
    CHECK_FALSE(error("{not json").empty());
    CHECK(error(R"({"format":"elara","formatVersion":2})").find("newer") != std::string::npos);
    CHECK(error(R"({"format":"midi","formatVersion":1})").find("not an Elara song") !=
          std::string::npos);
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{"timeSignature":"5/3"}]})")
              .starts_with("measures[0].timeSignature:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{},{"repeatTimes":1}]})")
              .starts_with("measures[1].repeatTimes:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{"alternateEndings":[9]}]})")
              .starts_with("measures[0].alternateEndings[0]:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{"alternateEndings":[2,1]}]})")
              .starts_with("measures[0].alternateEndings[1]:"));

    // A pitch that does not exist, deep in a staff.
    std::string json = songToJson(richSong());
    const auto at = json.find(R"("pitch":"C5")");
    REQUIRE(at != std::string::npos);
    json.replace(at, 12, R"("pitch":"H5")");
    CHECK(error(json).starts_with("tracks[2].staves[0].measures[2][0][0].notes[0].pitch:"));

    // A volume past full.
    CHECK(error(R"({"format":"elara","formatVersion":1,"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"mix":{"volume":1.5},"staves":[{"clef":"treble"}]}]})")
              .starts_with("tracks[0].mix.volume:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"mix":{"pan":"left"},"staves":[{"clef":"treble"}]}]})")
              .starts_with("tracks[0].mix.pan:"));

    // A clef change past the song's end, or to no clef Elara knows.
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","clefs":[{"measure":1,"clef":"bass"}]}]}]})")
              .starts_with("tracks[0].staves[0].clefs[0].measure:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","clefs":[{"measure":0,"clef":"soprano"}]}]}]})")
              .starts_with("tracks[0].staves[0].clefs[0].clef:"));

    // A measure repeat past the song's end, or of no kind Elara knows.
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","repeats":[{"measure":1,"repeat":"one"}]}]}]})")
              .starts_with("tracks[0].staves[0].repeats[0].measure:"));
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","repeats":[{"measure":0,"repeat":"three"}]}]}]})")
              .starts_with("tracks[0].staves[0].repeats[0].repeat:"));

    // More measures in a staff than in the song.
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","measures":[[],[]]}]}]})")
              .starts_with("tracks[0].staves[0].measures:"));
}

TEST_CASE("Measure layouts are written as the measures that differ, in each scope",
          "[io][native]") {
    // A11.
    const auto json = songToJson(richSong());
    CHECK(
        json.find(
            R"("layout":[{"measure":2,"break":"system"},{"measure":4,"break":"page","spacing":120}],"tracks":[)") !=
        std::string::npos);
    CHECK(json.find(R"("layout":[{"measure":3,"break":"keep"}],"staves":[)") != std::string::npos);
    std::size_t scopes = 0; // the other tracks hold none: no "layout" at all
    for (auto at = json.find(R"("layout":)"); at != std::string::npos;
         at = json.find(R"("layout":)", at + 1)) {
        ++scopes;
    }
    CHECK(scopes == 2);

    const auto loaded = songFromJson(json);
    REQUIRE(loaded.has_value());
    const core::LayoutScope bass = core::LayoutScope::ofTrack(1);
    CHECK(loaded->measureLayout(core::LayoutScope::song(), 4) ==
          core::MeasureLayout{.breakBefore = core::Break::Page, .spacing = 120});
    CHECK(loaded->measureLayout(bass, 3).breakBefore == core::Break::Keep);
    CHECK_FALSE(loaded->measureLayouts(core::LayoutScope::ofTrack(0)));
    CHECK(*loaded == richSong());
}

TEST_CASE("A song without measure layouts is written and read as before", "[io][native]") {
    core::Song song = richSong();
    song.setMeasureLayouts(core::LayoutScope::song(), {});
    song.setMeasureLayouts(core::LayoutScope::ofTrack(1), {});
    const auto json = songToJson(song);
    CHECK(json.find(R"("layout")") == std::string::npos);
    const auto loaded = songFromJson(json);
    REQUIRE(loaded.has_value());
    CHECK_FALSE(loaded->measureLayouts(core::LayoutScope::song()));
    CHECK(*loaded == song);

    // An empty "layout", or one of default entries only, holds none.
    const auto empty = songFromJson(
        R"({"format":"elara","formatVersion":1,"measures":[{},{}],"layout":[{"measure":1}]})");
    REQUIRE(empty.has_value());
    CHECK_FALSE(empty->measureLayouts(core::LayoutScope::song()));
}

TEST_CASE("Measure layouts are checked as they are read", "[io][native]") {
    const auto error = [](std::string_view layout) {
        const auto result = songFromJson(std::format(
            R"({{"format":"elara","formatVersion":1,"measures":[{{}},{{}}],"layout":{}}})",
            layout));
        REQUIRE_FALSE(result.has_value());
        return result.error().message;
    };
    CHECK(error(R"([{"measure":2,"break":"system"}])").starts_with("layout[0].measure:"));
    CHECK(error(R"([{"break":"system"}])") == R"(layout[0]: missing "measure")");
    CHECK(error(R"([{"measure":1,"break":"line"}])").starts_with("layout[0].break:"));
    CHECK(error(R"([{"measure":1,"spacing":20}])").starts_with("layout[0].spacing:"));
    CHECK(error(R"([3])").starts_with("layout[0]:"));
    CHECK(error(R"({"measure":1})").starts_with("layout:"));

    const auto track = songFromJson(R"({"format":"elara","formatVersion":1,"measures":[{}],
        "tracks":[{"name":"x","instrument":{"kind":"pitched"},"layout":[{"measure":1}],
        "staves":[{"clef":"treble"}]}]})");
    REQUIRE_FALSE(track.has_value());
    CHECK(track.error().message.starts_with("tracks[0].layout[0].measure:"));
}

TEST_CASE("Unknown keys are ignored for forward compatibility", "[io][native]") {
    const auto loaded = songFromJson(
        R"({"format":"elara","formatVersion":1,"futureFeature":{"x":1},"info":{"title":"T","mood":"calm"}})");
    REQUIRE(loaded.has_value());
    CHECK(loaded->info().title == "T");
}

TEST_CASE("A song saved by a newer Elara opens with a warning", "[io][native]") {
    const auto read = [](std::string_view generator) {
        return elara::io::readSongJson(std::format(
            R"({{"format":"elara","formatVersion":1,"generator":"{}","info":{{"title":"T"}}}})",
            generator));
    };
    const auto newer = read("Elara 99.0.0");
    REQUIRE(newer.has_value());
    CHECK(newer->song.info().title == "T");
    REQUIRE(newer->warnings.size() == 1);
    CHECK(newer->warnings[0] ==
          std::format("saved by Elara 99.0.0, newer than this one ({}): what it added is not "
                      "shown, and saving here leaves it out",
                      core::version()));
    const auto later = read("Elara 99.0.0-beta.2");
    REQUIRE(later.has_value());
    CHECK(later->warnings.size() == 1);

    // This version, an older one and a generator without a version say nothing.
    const std::string self = std::format("Elara {}", core::version());
    for (const std::string_view generator :
         {std::string_view(self), std::string_view("Elara 0.0.0"), std::string_view("Elara"),
          std::string_view("Elara 99.1"), std::string_view("Elara x.y.z"),
          std::string_view("Another 99.0.0"), std::string_view("")}) {
        INFO(generator);
        const auto quiet = read(generator);
        REQUIRE(quiet.has_value());
        CHECK(quiet->warnings.empty());
    }
    const auto written = elara::io::readSongJson(songToJson(richSong()));
    REQUIRE(written.has_value());
    CHECK(written->song == richSong());
    CHECK(written->warnings.empty());
    CHECK(elara::io::readNative(encodeNative(richSong()))->warnings.empty());
}

TEST_CASE("A native file is a zip with a mimetype and song.json", "[io][native]") {
    const core::Song song = richSong();
    const std::vector<std::uint8_t> bytes = encodeNative(song);
    REQUIRE(bytes.size() > 60);
    CHECK(bytes[0] == 'P');
    CHECK(bytes[1] == 'K');
    // The first entry is the uncompressed mimetype, readable at a fixed offset.
    const std::string head(bytes.begin() + 30, bytes.begin() + 38);
    CHECK(head == "mimetype");
    const std::string mime(bytes.begin() + 38, bytes.begin() + 38 + 30);
    CHECK(mime == "application/vnd.elara.song+zip");

    const auto loaded = decodeNative(bytes);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == song);

    std::vector<std::uint8_t> broken = bytes;
    broken.resize(broken.size() / 2);
    CHECK_FALSE(decodeNative(broken).has_value());
    CHECK_FALSE(decodeNative(std::vector<std::uint8_t>{1, 2, 3}).has_value());
}

TEST_CASE("Native files are saved atomically and loaded back", "[io][native]") {
    const auto path = tempFile("elara-test-native.elara");
    std::filesystem::remove(path);

    saveNative(richSong(), path);
    auto loaded = loadNative(path);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == richSong());

    // Saving again replaces the file and leaves no temporary file behind.
    core::Song other;
    other.info().title = "Replaced";
    saveNative(other, path);
    loaded = loadNative(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->info().title == "Replaced");
    auto temporary = path;
    temporary += ".saving";
    CHECK_FALSE(std::filesystem::exists(temporary));
    std::filesystem::remove(path);

    // A temporary file a crash left behind, longer than the new one, is
    // replaced whole.
    {
        std::ofstream stale(temporary, std::ios::binary);
        stale << std::string(std::size_t{1} << 20U, 'x');
    }
    saveNative(other, path);
    loaded = loadNative(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->info().title == "Replaced");
    CHECK(std::filesystem::file_size(path) < (1U << 16U));
    CHECK_FALSE(std::filesystem::exists(temporary));
    std::filesystem::remove(path);

    CHECK_FALSE(loadNative(tempFile("elara-test-missing.elara")).has_value());
    CHECK_THROWS_AS(saveNative(other, tempFile("elara-no-such-dir") / "x" / "song.elara"),
                    std::runtime_error);
}
