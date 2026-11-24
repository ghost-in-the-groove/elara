// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/io/native_format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
        TempoChange{.offset = Fraction(3, 8), .tempo = Tempo(90)}};
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
    slid.effects.ghost = true;
    Note muted = lead.frettedNote(6, 0);
    muted.effects.palmMute = true;
    muted.effects.deadNote = true;
    Note hammer = lead.frettedNote(6, 2);
    hammer.effects.legatoFromPrevious = true;
    Note tied = lead.frettedNote(6, 2);
    tied.effects.tiedFromPrevious = true;
    Note semi = lead.frettedNote(4, 3);
    semi.effects.harmonic = Harmonic{.kind = HarmonicKind::Semi, .fretTenths = 32};

    Beat strummed = beat("8", {bent, harmonic, slid});
    strummed.effects.strum = Strum::Down;
    strummed.effects.pickStroke = PickStroke::Up;
    strummed.effects.whammyBar = BendCurve({{0, 0}, {100, -1200}});
    strummed.effects.tremoloPicking = NoteValue::ThirtySecond;
    auto& first = lead.measure(0).voices[0].beats;
    first = {strummed,   beat("8*3:2", {muted}), beat("8*3:2", {hammer}), beat("8*3:2", {tied}),
             beat("4."), beat("16..", {semi})};
    lead.measure(0).voices[2].beats = {beat("2"), beat("2")}; // voice 1 empty, voice 2 not

    // Bass with techniques, piano on both staves, drums by key; a breve somewhere.
    Staff& bass = song.track(1).staff(0);
    Beat slap = beat("4", {bass.frettedNote(5, 0)});
    slap.effects.bassTechnique = BassTechnique::Slap;
    bass.measure(3).voices[0].beats = {slap, beat("4"), beat("2", {bass.frettedNote(1, 3)})};
    song.track(2).staff(0).measure(2).voices[0].beats = {
        beat("1", {Note{.pitch = Pitch(Step::C, 0, 5)}, Note{.pitch = Pitch(Step::E, -1, 5)}})};
    song.track(2).staff(1).measure(2).voices[0].beats = {
        beat("1", {Note{.pitch = Pitch(Step::C, 1, 3)}})};
    song.track(3).staff(0).measure(4).voices[0].beats = {
        beat("breve", {Note{.pitch = Pitch::fromMidi(36)}, Note{.pitch = Pitch::fromMidi(42)}})};
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

TEST_CASE("song.json uses text forms and omits defaults", "[io][native]") {
    const auto json = songToJson(richSong());
    CHECK(json.find(R"("timeSignature":"6/8","keySignature":"C minor")") != std::string::npos);
    CHECK(json.find(R"("tuning":"D2 A2 D3 G3 B3 E4","capo":2)") != std::string::npos);
    CHECK(json.find(R"({"duration":"4."})") != std::string::npos); // a rest
    CHECK(json.find(R"("clef":"treble-8vb")") != std::string::npos);
    CHECK(json.find(R"("slideIn":"from-below")") != std::string::npos);
    CHECK(json.find(R"("dynamic":"mf")") == std::string::npos); // the default
    CHECK(json.find(R"("tiedFromPrevious":false)") == std::string::npos);
    CHECK(json.find(R"("repeatStart":true)") != std::string::npos);
    CHECK(json.find(R"("repeatTimes":3,"alternateEndings":[1,2],"section":"Chorus"})") !=
          std::string::npos);
    CHECK(json.find(R"("section":"")") == std::string::npos);
    CHECK(json.find(R"({"alternateEndings":[3]})") != std::string::npos);
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

    // More measures in a staff than in the song.
    CHECK(error(R"({"format":"elara","formatVersion":1,"measures":[{}],"tracks":[{"name":"x",
        "instrument":{"kind":"pitched"},"staves":[{"clef":"treble","measures":[[],[]]}]}]})")
              .starts_with("tracks[0].staves[0].measures:"));
}

TEST_CASE("Unknown keys are ignored for forward compatibility", "[io][native]") {
    const auto loaded = songFromJson(
        R"({"format":"elara","formatVersion":1,"futureFeature":{"x":1},"info":{"title":"T","mood":"calm"}})");
    REQUIRE(loaded.has_value());
    CHECK(loaded->info().title == "T");
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

    CHECK_FALSE(loadNative(tempFile("elara-test-missing.elara")).has_value());
    CHECK_THROWS_AS(saveNative(other, tempFile("elara-no-such-dir") / "x" / "song.elara"),
                    std::runtime_error);
}
