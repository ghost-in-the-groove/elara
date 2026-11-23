// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/validation.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = elara::core;
using elara::io::exportMusicXml;
using elara::io::exportMusicXmlFile;
using elara::io::ExportResult;
using elara::io::importMusicXml;
using elara::io::importMusicXmlBytes;
using elara::io::importMusicXmlFile;
using elara::io::ImportResult;
using elara::io::MusicXmlExportOptions;

namespace {

std::filesystem::path corpusDir() {
    return std::filesystem::path(ELARA_CORPUS_DIR) / "musicxml";
}

std::span<const std::uint8_t> bytesOf(const std::string& data) {
    return {reinterpret_cast<const std::uint8_t*>(data.data()), data.size()};
}

/// Where two songs first differ, shown as their song.json texts around that point.
std::string difference(const core::Song& expected, const core::Song& actual) {
    const std::string a = elara::io::songToJson(expected);
    const std::string b = elara::io::songToJson(actual);
    const auto mismatch = std::ranges::mismatch(a, b);
    const auto at = static_cast<std::size_t>(mismatch.in1 - a.begin());
    const std::size_t from = at > 160 ? at - 160 : 0;
    return std::format("songs differ at byte {}\nexpected ...{}...\nactual   ...{}...", at,
                       a.substr(from, 320), b.substr(from, 320));
}

ImportResult reimport(const ExportResult& exported) {
    auto back = importMusicXmlBytes(bytesOf(exported.data));
    if (!back) {
        FAIL(back.error().message);
    }
    return std::move(*back);
}

/// Exports, imports back and checks that nothing was reported on the way.
core::Song roundTrip(const core::Song& song, const MusicXmlExportOptions& options = {}) {
    const ExportResult exported = exportMusicXml(song, options);
    for (const std::string& warning : exported.warnings) {
        UNSCOPED_INFO("export warning: " << warning);
    }
    CHECK(exported.warnings.empty());
    ImportResult back = reimport(exported);
    for (const std::string& warning : back.warnings) {
        UNSCOPED_INFO("import warning: " << warning);
    }
    CHECK(back.warnings.empty());
    return std::move(back.song);
}

void requireSame(const core::Song& expected, const core::Song& actual) {
    if (expected != actual) {
        FAIL(difference(expected, actual));
    }
}

bool hasWarning(const std::vector<std::string>& warnings, std::string_view feature) {
    return std::ranges::any_of(warnings, [&](const std::string& warning) {
        return warning.find(feature) != std::string::npos;
    });
}

void fillVoices(core::Song& song) {
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                for (core::Voice& voice : song.track(t).staff(s).measure(m).voices) {
                    REQUIRE(core::fillWithRests(voice, song.measureHeader(m).timeSignature));
                }
            }
        }
    }
}

core::Beat beatOf(core::Duration duration, std::vector<core::Note> notes = {}) {
    return core::Beat{.duration = duration, .notes = std::move(notes)};
}

core::Note pitched(core::Pitch pitch, core::Dynamic dynamic = core::Dynamic::MF) {
    core::Note note{.pitch = pitch};
    note.effects.dynamic = dynamic;
    return note;
}

/// A song with every feature MusicXML can carry: song info, tempo changes,
/// meters and keys, a fretted guitar with its techniques, transposing
/// instruments, a two-staff piano with several voices and dynamics, drums, bass
/// and ukulele.
core::Song everythingSong() {
    using namespace elara::core;
    Song song;
    song.info() = SongInfo{.title = R"(Rock & <Roll> "Live")",
                           .subtitle = "Take 2",
                           .artist = "The Grooves",
                           .album = "Ghost Notes",
                           .words = "A Lyricist",
                           .music = "A Composer",
                           .copyright = "CC0 1.0",
                           .transcriber = "A Transcriber",
                           .notes = "Played with 'feel'"};
    song.setStartTempo(Tempo(90.0, Duration(NoteValue::Quarter, 1)));
    song.appendMeasures(4);
    song.measureHeader(1).tempoChanges = {
        TempoChange{.offset = Fraction(1, 4), .tempo = Tempo(140.0)},
        TempoChange{.offset = Fraction(5, 8), .tempo = Tempo(70.5, Duration(NoteValue::Eighth))}};
    song.measureHeader(2).timeSignature = TimeSignature(3, 4);
    song.measureHeader(2).keySignature = KeySignature(-1, KeyMode::Minor);
    song.measureHeader(3).timeSignature = TimeSignature(6, 8);
    song.measureHeader(3).keySignature = KeySignature(4, KeyMode::Major);
    // |: 0 |1.2. 1 :|3. 2 |: 3 :|, the first repeat played three times
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).repeatTimes = 3;
    song.measureHeader(1).alternateEndings = 0b011;
    song.measureHeader(2).alternateEndings = 0b100;
    song.measureHeader(3).repeatStart = true;
    song.measureHeader(3).repeatTimes = 2;

    const Duration quarter(NoteValue::Quarter);
    const Duration eighth(NoteValue::Eighth);
    const Duration half(NoteValue::Half);
    const Duration triplet8(NoteValue::Eighth, 0, Tuplet(3, 2));
    const Duration triplet4(NoteValue::Quarter, 0, Tuplet(3, 2));

    // ---- Guitar: drop D, capo 2, every technique MusicXML has.
    const std::size_t g =
        song.addTrack(Track("Lead",
                            Instrument{.name = "Electric Guitar",
                                       .kind = InstrumentKind::Fretted,
                                       .midiProgram = 29,
                                       .writtenOffset = 12},
                            {Staff(Clef::Treble8vb, Tuning::preset("guitar-drop-d"), 2)}));
    Staff& guitar = song.track(g).staff(0);
    const auto fretted = [&guitar](int string, int fret, Dynamic dynamic = Dynamic::MF) {
        Note note = guitar.frettedNote(string, fret);
        note.effects.dynamic = dynamic;
        return note;
    };
    {
        auto& beats = guitar.measure(0).voices[0].beats;
        Note first = fretted(3, 5, Dynamic::F);
        first.effects.palmMute = true;
        Note hammer = fretted(3, 7, Dynamic::F);
        hammer.effects.legatoFromPrevious = true;
        Note pull = fretted(3, 5, Dynamic::F);
        pull.effects.legatoFromPrevious = true;
        pull.effects.slideOut = SlideOut::Legato;
        Note slid = fretted(3, 9, Dynamic::F);
        slid.effects.vibrato = Vibrato::Wide;
        beats = {beatOf(quarter, {first}), beatOf(quarter, {hammer}), beatOf(quarter, {pull}),
                 beatOf(quarter, {slid})};
    }
    {
        auto& beats = guitar.measure(1).voices[0].beats;
        Note tied = fretted(3, 9, Dynamic::F);
        tied.effects.tiedFromPrevious = true;
        Note bend = fretted(2, 5, Dynamic::F);
        bend.effects.bend = BendCurve::bend(200);
        Note release = fretted(2, 7, Dynamic::F);
        release.effects.bend = BendCurve::bendRelease(100);
        Note preBend = fretted(2, 8, Dynamic::F);
        preBend.effects.bend = BendCurve::preBend(150);
        Note preRelease = fretted(1, 5, Dynamic::P);
        preRelease.effects.bend = BendCurve::preBendRelease(200);
        Note marked = fretted(1, 7, Dynamic::P);
        marked.effects.staccato = true;
        marked.effects.accent = Accent::Accent;
        beats = {beatOf(quarter, {tied}),
                 beatOf(triplet8, {bend}),
                 beatOf(triplet8, {release}),
                 beatOf(triplet8, {preBend}),
                 beatOf(triplet4, {preRelease}),
                 beatOf(triplet8, {marked}),
                 beatOf(quarter, {fretted(6, 0, Dynamic::P), fretted(5, 2, Dynamic::P),
                                  fretted(4, 2, Dynamic::P)})};
        beats[1].effects.pickStroke = PickStroke::Down;
        beats[6].effects.strum = Strum::Down;
    }
    {
        auto& beats = guitar.measure(2).voices[0].beats;
        Note natural = fretted(1, 12, Dynamic::P);
        natural.effects.harmonic = Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 120};
        natural.effects.accent = Accent::Marcato;
        Note dead = fretted(2, 5, Dynamic::P);
        dead.effects.deadNote = true;
        Note ghost = fretted(3, 7, Dynamic::P);
        ghost.effects.ghost = true;
        ghost.effects.slideIn = SlideIn::FromBelow;
        Note fall = fretted(4, 5, Dynamic::P);
        fall.effects.slideOut = SlideOut::Downwards;
        beats = {beatOf(quarter, {natural}), beatOf(eighth, {dead}), beatOf(eighth, {ghost}),
                 beatOf(quarter, {fall})};
        beats[3].effects.tremoloPicking = NoteValue::Sixteenth;
        beats[3].effects.pickStroke = PickStroke::Up;
    }
    {
        auto& beats = guitar.measure(3).voices[0].beats;
        Note scoop = fretted(5, 3, Dynamic::P);
        scoop.effects.slideIn = SlideIn::FromAbove;
        scoop.effects.slideOut = SlideOut::Upwards;
        Note artificial = fretted(2, 8, Dynamic::P);
        artificial.effects.harmonic = Harmonic{.kind = HarmonicKind::Artificial, .fretTenths = 120};
        Note vibrato = fretted(1, 1, Dynamic::P);
        vibrato.effects.vibrato = Vibrato::Slight;
        beats = {beatOf(Duration(NoteValue::Quarter, 1), {scoop, fretted(4, 5, Dynamic::P)}),
                 beatOf(eighth, {artificial}), beatOf(Duration(NoteValue::Sixteenth), {vibrato})};
        beats[0].effects.strum = Strum::Up;
    }

    // ---- B-flat clarinet: transposed spelling, double accidentals.
    const std::size_t c = song.addTrack(Track("Clarinet",
                                              Instrument{.name = "Clarinet in Bb",
                                                         .kind = InstrumentKind::Pitched,
                                                         .midiProgram = 71,
                                                         .writtenOffset = 2},
                                              {Staff(Clef::Treble)}));
    {
        Staff& staff = song.track(c).staff(0);
        staff.measure(0).voices[0].beats = {beatOf(half, {pitched(Pitch(Step::B, -1, 4))}),
                                            beatOf(quarter, {pitched(Pitch(Step::F, 2, 4))})};
        staff.measure(1).voices[0].beats = {
            beatOf(Duration(NoteValue::Half, 2), {pitched(Pitch(Step::C, -1, 5))}),
            beatOf(eighth, {pitched(Pitch(Step::E, -2, 4))})};
        staff.measure(3).voices[0].beats = {
            beatOf(Duration(NoteValue::Quarter, 1), {pitched(Pitch(Step::B, 0, 4))}),
            beatOf(Duration(NoteValue::Quarter, 1), {pitched(Pitch(Step::C, 1, 5))})};
    }

    // ---- Tenor saxophone: written a ninth above.
    const std::size_t sax = song.addTrack(Track::fromPreset(*findInstrumentPreset("tenor-sax")));
    song.track(sax).staff(0).measure(0).voices[0].beats = {
        beatOf(Duration(NoteValue::Whole), {pitched(Pitch(Step::B, -1, 3))})};

    // ---- Piano: two staves, voices 1 and 3 with an empty voice 2, dynamics.
    const std::size_t p = song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    {
        Staff& upper = song.track(p).staff(0);
        Staff& lower = song.track(p).staff(1);
        upper.measure(0).voices[0].beats = {
            beatOf(half, {pitched(Pitch(Step::C, 0, 5)), pitched(Pitch(Step::E, 0, 5)),
                          pitched(Pitch(Step::G, 0, 5))})};
        upper.measure(0).voices[2].beats = {
            beatOf(Duration(NoteValue::Whole), {pitched(Pitch(Step::G, 0, 4))})};
        lower.measure(0).voices[0].beats = {
            beatOf(Duration(NoteValue::Whole), {pitched(Pitch(Step::C, 0, 3), Dynamic::P),
                                                pitched(Pitch(Step::G, 0, 3), Dynamic::P)})};
        upper.measure(1).voices[0].beats = {
            beatOf(quarter, {pitched(Pitch(Step::C, 0, 5))}),
            beatOf(quarter, {pitched(Pitch(Step::D, 0, 5))}),
            beatOf(quarter, {pitched(Pitch(Step::E, 0, 5), Dynamic::F)}),
            beatOf(quarter, {pitched(Pitch(Step::F, 0, 5), Dynamic::F)})};
        upper.measure(1).voices[1].beats = {
            beatOf(half, {pitched(Pitch(Step::A, 0, 4))}),
            beatOf(half, {pitched(Pitch(Step::B, 0, 4), Dynamic::F)})};
        upper.measure(2).voices[0].beats = {
            beatOf(Duration(NoteValue::Half, 1), {pitched(Pitch(Step::E, 0, 5), Dynamic::F)})};
        lower.measure(2).voices[0].beats = {
            beatOf(Duration(NoteValue::Half, 1), {pitched(Pitch(Step::C, 0, 3), Dynamic::P)})};
    }

    // ---- Drums: hands in voice 1, feet and snare in voice 2.
    const std::size_t d = song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    {
        Staff& kit = song.track(d).staff(0);
        for (int i = 0; i < 8; ++i) {
            kit.measure(0).voices[0].beats.push_back(
                beatOf(eighth, {pitched(Pitch::fromMidi(42))}));
        }
        Note ghost = pitched(Pitch::fromMidi(38));
        ghost.effects.ghost = true;
        Note accent = pitched(Pitch::fromMidi(38));
        accent.effects.accent = Accent::Accent;
        kit.measure(0).voices[1].beats = {
            beatOf(quarter, {pitched(Pitch::fromMidi(36))}), beatOf(quarter, {ghost}),
            beatOf(quarter, {pitched(Pitch::fromMidi(36))}), beatOf(quarter, {accent})};
    }

    // ---- Bass guitar and ukulele (a fretted staff without an octave clef).
    const std::size_t b = song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-bass")));
    song.track(b).staff(0).measure(0).voices[0].beats = {
        beatOf(half, {song.track(b).staff(0).frettedNote(4, 3)}),
        beatOf(half, {song.track(b).staff(0).frettedNote(3, 5)})};
    const std::size_t u = song.addTrack(Track::fromPreset(*findInstrumentPreset("ukulele")));
    {
        Staff& uke = song.track(u).staff(0);
        uke.measure(2).voices[0].beats = {
            beatOf(half, {uke.frettedNote(4, 0), uke.frettedNote(3, 0), uke.frettedNote(2, 0),
                          uke.frettedNote(1, 3)})};
    }

    fillVoices(song);
    REQUIRE(core::validate(song).empty());
    return song;
}

/// A random but valid song: several instruments, meters, tuplets, chords,
/// ties and techniques, from a fixed seed.
core::Song randomSong(std::uint32_t seed) {
    using namespace elara::core;
    std::mt19937 random(seed);
    const auto pick = [&random](int low, int high) {
        return std::uniform_int_distribution<int>(low, high)(random);
    };
    Song song;
    song.info().title = std::format("Random {}", seed);
    constexpr std::array<TimeSignature, 5> kMeters{TimeSignature(4, 4), TimeSignature(3, 4),
                                                   TimeSignature(6, 8), TimeSignature(2, 2),
                                                   TimeSignature(7, 8)};
    song.appendMeasures(24);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        if (m % 6 == 0) {
            song.measureHeader(m).timeSignature = kMeters[static_cast<std::size_t>(pick(0, 4))];
            song.measureHeader(m).keySignature =
                KeySignature(pick(-4, 4), pick(0, 1) == 0 ? KeyMode::Major : KeyMode::Minor);
        } else {
            song.measureHeader(m) = song.measureHeader(m - 1).continuation();
        }
        song.measureHeader(m).repeatStart = pick(0, 7) == 0;
        song.measureHeader(m).repeatTimes =
            pick(0, 7) == 0 ? static_cast<std::uint8_t>(pick(2, 4)) : std::uint8_t{0};
        song.measureHeader(m).alternateEndings =
            pick(0, 9) == 0 ? static_cast<std::uint8_t>(pick(1, 255)) : std::uint8_t{0};
        if (pick(0, 4) == 0) {
            song.measureHeader(m).tempoChanges.push_back(
                TempoChange{.offset = Fraction(pick(0, 3), 8),
                            .tempo = Tempo(static_cast<double>(pick(40, 220)))});
        }
    }
    for (const char* preset :
         {"electric-guitar", "piano", "drum-kit", "alto-sax", "five-string-bass"}) {
        song.addTrack(Track::fromPreset(*findInstrumentPreset(preset)));
    }

    const std::array<Duration, 7> kPlain{
        Duration(NoteValue::Half),        Duration(NoteValue::Quarter),
        Duration(NoteValue::Quarter, 1),  Duration(NoteValue::Eighth),
        Duration(NoteValue::Eighth, 1),   Duration(NoteValue::Sixteenth),
        Duration(NoteValue::ThirtySecond)};
    const std::array<Dynamic, 4> kDynamics{Dynamic::P, Dynamic::MF, Dynamic::F, Dynamic::FF};
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        Track& track = song.track(t);
        const bool drums = track.instrument().kind == InstrumentKind::Percussion;
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            Staff& staff = track.staff(s);
            Dynamic dynamic = Dynamic::MF;
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                const Fraction length = song.measureHeader(m).timeSignature.measureLength();
                const std::size_t voices = drums || s == 0 ? 2 : 1;
                for (std::size_t v = 0; v < voices; ++v) {
                    if (pick(0, 5) == 0) {
                        continue; // a measure rest
                    }
                    Voice& voice = staff.measure(m).voices[v];
                    Fraction used;
                    while (used < length) {
                        std::vector<Duration> durations;
                        if (pick(0, 5) == 0 && used + Fraction(1, 4) <= length) {
                            durations.assign(3, Duration(NoteValue::Eighth, 0, Tuplet(3, 2)));
                        } else {
                            const Duration plain = kPlain[static_cast<std::size_t>(pick(0, 6))];
                            if (used + plain.length() > length) {
                                break;
                            }
                            durations.push_back(plain);
                        }
                        if (v == 0 && pick(0, 7) == 0) {
                            dynamic = kDynamics[static_cast<std::size_t>(pick(0, 3))];
                        }
                        for (const Duration& duration : durations) {
                            Beat beat{.duration = duration};
                            if (pick(0, 6) != 0) {
                                const int notes =
                                    drums || staff.isFretted() ? pick(1, 3) : pick(1, 2);
                                std::vector<int> strings;
                                for (int n = 0; n < notes; ++n) {
                                    Note note;
                                    if (drums) {
                                        constexpr std::array kKeys{35, 36, 38, 42, 46,
                                                                   49, 51, 41, 45};
                                        note.pitch = Pitch::fromMidi(
                                            kKeys[static_cast<std::size_t>(pick(0, 8))]);
                                    } else if (staff.isFretted()) {
                                        int string = pick(1, staff.tuning()->stringCount());
                                        if (std::ranges::find(strings, string) != strings.end()) {
                                            continue;
                                        }
                                        strings.push_back(string);
                                        note = staff.frettedNote(string, pick(0, 15),
                                                                 pick(0, 1) == 0
                                                                     ? SpellingPreference::Sharps
                                                                     : SpellingPreference::Flats);
                                        note.effects.palmMute = pick(0, 9) == 0;
                                        note.effects.vibrato =
                                            pick(0, 11) == 0 ? Vibrato::Slight : Vibrato::None;
                                        if (pick(0, 13) == 0) {
                                            note.effects.bend = BendCurve::bend(100 * pick(1, 3));
                                        }
                                    } else {
                                        note.pitch = song.measureHeader(m).keySignature.spell(
                                            s == 1 ? pick(36, 59) : pick(55, 84));
                                    }
                                    note.effects.staccato = pick(0, 9) == 0;
                                    note.effects.ghost = pick(0, 15) == 0;
                                    beat.notes.push_back(note);
                                }
                            }
                            for (Note& note : beat.notes) {
                                note.effects.dynamic = dynamic;
                            }
                            used += duration.length();
                            voice.beats.push_back(std::move(beat));
                        }
                    }
                }
            }
        }
    }
    // Every voice of a staff starts with the staff's dynamic, so notes that sound
    // together share one (MusicXML marks dynamics per staff).
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            Staff& staff = song.track(t).staff(s);
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                const Voice& first = staff.measure(m).voices[0];
                Dynamic dynamic = Dynamic::MF;
                for (const Beat& beat : first.beats) {
                    if (!beat.notes.empty()) {
                        dynamic = beat.notes.front().effects.dynamic;
                        break;
                    }
                }
                for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                    for (Beat& beat : staff.measure(m).voices[v].beats) {
                        for (Note& note : beat.notes) {
                            note.effects.dynamic = dynamic;
                        }
                    }
                }
            }
        }
    }
    fillVoices(song);
    REQUIRE(core::validate(song).empty());
    return song;
}

} // namespace

TEST_CASE("MusicXML export: every corpus file survives export and import unchanged",
          "[musicxml][export]") {
    for (const auto& entry : std::filesystem::directory_iterator(corpusDir())) {
        INFO(entry.path().filename().string());
        const auto imported = importMusicXmlFile(entry.path());
        REQUIRE(imported.has_value());
        requireSame(imported->song, roundTrip(imported->song));
    }
}

TEST_CASE("MusicXML export: a song with every feature MusicXML holds comes back unchanged",
          "[musicxml][export]") {
    const core::Song song = everythingSong();
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: random songs come back unchanged", "[musicxml][export]") {
    for (const std::uint32_t seed : {1U, 2U, 3U, 20261123U}) {
        INFO("seed " << seed);
        const core::Song song = randomSong(seed);
        requireSame(song, roundTrip(song));
    }
}

TEST_CASE("MusicXML export: the document follows the MusicXML mapping", "[musicxml][export]") {
    using namespace elara::core;
    Song song;
    song.appendMeasures(1);
    const std::size_t clarinet = song.addTrack(Track("Clarinet",
                                                     Instrument{.name = "Clarinet in Bb",
                                                                .kind = InstrumentKind::Pitched,
                                                                .midiProgram = 71,
                                                                .writtenOffset = 2},
                                                     {Staff(Clef::Treble)}));
    song.track(clarinet).staff(0).measure(0).voices[0].beats = {
        beatOf(Duration(NoteValue::Whole), {pitched(Pitch(Step::B, -1, 4))})};
    const std::size_t guitar =
        song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    song.track(guitar).staff(0).measure(0).voices[0].beats = {
        beatOf(Duration(NoteValue::Whole), {song.track(guitar).staff(0).frettedNote(6, 3)})};
    const std::size_t drums = song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    song.track(drums).staff(0).measure(0).voices[0].beats = {
        beatOf(Duration(NoteValue::Whole), {pitched(Pitch::fromMidi(38))})};

    const ExportResult exported = exportMusicXml(song);
    const std::string& xml = exported.data;
    CHECK(exported.warnings.empty());
    CHECK(xml.starts_with(R"(<?xml version="1.0" encoding="UTF-8")"));
    CHECK(xml.contains(R"(<score-partwise version="4.0">)"));
    // B-flat clarinet: a sounding B-flat is written as C, a major second higher.
    CHECK(xml.contains("<diatonic>-1</diatonic>"));
    CHECK(xml.contains("<chromatic>-2</chromatic>"));
    CHECK(xml.contains("<step>C</step>"));
    // Guitar: treble 8vb staff plus a six-line TAB staff with the tuning.
    CHECK(xml.contains("<clef-octave-change>-1</clef-octave-change>"));
    CHECK(xml.contains("<sign>TAB</sign>"));
    CHECK(xml.contains("<staff-lines>6</staff-lines>"));
    CHECK(xml.contains("<string>6</string>"));
    CHECK(xml.contains("<fret>3</fret>"));
    // Drums: General MIDI key 38 is midi-unpitched 39 (1-based) on channel 10,
    // drawn on the snare line of the drum map.
    CHECK(xml.contains("<midi-unpitched>39</midi-unpitched>"));
    CHECK(xml.contains("<midi-channel>10</midi-channel>"));
    CHECK(xml.contains("<display-step>C</display-step>"));
    CHECK(xml.contains("<display-octave>5</display-octave>"));
    CHECK(xml.contains("<sign>percussion</sign>"));
    // Readers compute accidentals, beams and stems themselves.
    CHECK(xml.contains(R"(<supports element="accidental" type="no"/>)"));
    CHECK(xml.contains(R"(<supports element="beam" type="no"/>)"));
}

TEST_CASE("MusicXML export: what MusicXML cannot hold is reported and changed predictably",
          "[musicxml][export]") {
    using namespace elara::core;
    Song song;
    song.setStartTempo(Tempo(100.0, Duration(NoteValue::Quarter, 0, Tuplet(3, 2))));
    song.appendMeasures(1);
    const std::size_t g =
        song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    Staff& staff = song.track(g).staff(0);
    Note ringing = staff.frettedNote(1, 5);
    ringing.effects.letRing = true;
    Note pinch = staff.frettedNote(2, 5);
    pinch.effects.harmonic = Harmonic{.kind = HarmonicKind::Pinch, .fretTenths = 120};
    pinch.effects.slideOut = SlideOut::Shift;
    Note node = staff.frettedNote(3, 3);
    node.effects.harmonic = Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 32};
    node.effects.bend = BendCurve({{.position = 0, .cents = 0},
                                   {.position = 25, .cents = 100},
                                   {.position = 50, .cents = 200},
                                   {.position = 100, .cents = 100}});
    Beat rest{.duration = Duration(NoteValue::Quarter)};
    rest.effects.pickStroke = PickStroke::Down;
    auto& beats = staff.measure(0).voices[0].beats;
    beats = {beatOf(Duration(NoteValue::Quarter), {ringing}),
             beatOf(Duration(NoteValue::Quarter), {pinch}),
             beatOf(Duration(NoteValue::Quarter), {node}), rest};
    beats[0].effects.whammyBar =
        BendCurve({{.position = 0, .cents = 0}, {.position = 100, .cents = -200}});
    beats[0].effects.bassTechnique = BassTechnique::Slap;
    Note louder = staff.frettedNote(6, 0);
    louder.effects.dynamic = Dynamic::F;
    staff.measure(0).voices[1].beats = {beatOf(Duration(NoteValue::Whole), {louder})};

    const ExportResult exported = exportMusicXml(song);
    const auto& warnings = exported.warnings;
    CHECK(hasWarning(warnings, "Electric Guitar: let ring dropped (measure 1)"));
    CHECK(hasWarning(warnings, "whammy bar dropped"));
    CHECK(hasWarning(warnings, "bass techniques (tap, slap, pop) dropped"));
    CHECK(hasWarning(warnings, "pinch, tapped and semi harmonics written as artificial harmonics"));
    CHECK(hasWarning(warnings, "shift slides written as legato slides"));
    CHECK(hasWarning(warnings, "natural harmonics written at their fret"));
    CHECK(hasWarning(warnings, "bend curves written as the nearest bend, release or pre-bend"));
    CHECK(hasWarning(warnings, "effects on rests dropped"));
    CHECK(hasWarning(warnings, "notes of different dynamics at the same time written with one"));
    CHECK(hasWarning(warnings, "tempos with a tuplet beat unit written in quarter notes"));

    const ImportResult back = reimport(exported);
    CHECK(back.warnings.empty());
    const auto& voice = back.song.track(0).staff(0).measure(0).voices[0];
    CHECK_FALSE(voice.beats[0].notes[0].effects.letRing);
    CHECK(voice.beats[0].effects.whammyBar.isEmpty());
    CHECK(voice.beats[0].effects.bassTechnique == BassTechnique::None);
    CHECK(voice.beats[1].notes[0].effects.harmonic ==
          Harmonic{.kind = HarmonicKind::Artificial, .fretTenths = 120});
    CHECK(voice.beats[1].notes[0].effects.slideOut == SlideOut::Legato);
    CHECK(voice.beats[2].notes[0].effects.harmonic.fretTenths == 30);
    CHECK(voice.beats[2].notes[0].effects.bend == BendCurve::bendRelease(200));
    CHECK(voice.beats[3].effects == BeatEffects{});
    CHECK(voice.beats[0].notes[0].effects.dynamic == Dynamic::MF);
    CHECK(back.song.startTempo().beatUnit() == Duration(NoteValue::Quarter));
    CHECK(back.song.startTempo().bpm() == Catch::Approx(100.0 * 2.0 / 3.0));
}

TEST_CASE("MusicXML export: compressed .mxl archives import back", "[musicxml][export]") {
    const core::Song song = everythingSong();
    const ExportResult compressed = exportMusicXml(song, {.compressed = true});
    CHECK(compressed.warnings.empty());
    REQUIRE(compressed.data.size() > 4);
    CHECK(compressed.data.starts_with("PK"));
    CHECK(compressed.data.size() < exportMusicXml(song).data.size() / 4);
    requireSame(song, reimport(compressed).song);
}

TEST_CASE("MusicXML export: files are written by extension and atomically", "[musicxml][export]") {
    const core::Song song = everythingSong();
    const auto directory = std::filesystem::temp_directory_path() / "elara-musicxml-export-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    for (const char* name : {"song.musicxml", "song.xml", "song.mxl"}) {
        INFO(name);
        const auto path = directory / name;
        CHECK(exportMusicXmlFile(song, path).empty());
        const auto back = importMusicXmlFile(path);
        REQUIRE(back.has_value());
        requireSame(song, back->song);
        CHECK_FALSE(std::filesystem::exists(path.string() + ".saving"));
    }
    CHECK(importMusicXmlFile(directory / "song.mxl").has_value());
    const auto missing = directory / "no-such-directory" / "song.musicxml";
    CHECK_THROWS_AS(exportMusicXmlFile(song, missing), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(missing));
    std::filesystem::remove_all(directory);
}

TEST_CASE("MusicXML export: parts written on several threads give the same document",
          "[musicxml][export]") {
    const core::Song song = everythingSong();
    const ExportResult serial = exportMusicXml(song, {.threads = 1});
    for (const unsigned threads : {2U, 3U, 8U}) {
        INFO(threads << " threads");
        const ExportResult parallel = exportMusicXml(song, {.threads = threads});
        CHECK(parallel.data == serial.data);
        CHECK(parallel.warnings == serial.warnings);
    }
}

TEST_CASE("MusicXML export: songs without tracks or measures", "[musicxml][export]") {
    core::Song empty;
    const ExportResult nothing = exportMusicXml(empty);
    CHECK(reimport(nothing).song == empty);

    core::Song measuresOnly;
    measuresOnly.appendMeasures(3);
    const ExportResult noTracks = exportMusicXml(measuresOnly);
    CHECK(hasWarning(noTracks.warnings, "songs without tracks keep no measures"));

    // Staves, clefs and tunings live in the measures' attributes.
    core::Song tracksOnly;
    tracksOnly.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const ExportResult noMeasures = exportMusicXml(tracksOnly);
    CHECK(hasWarning(noMeasures.warnings, "songs without measures keep only track names"));
    const ImportResult back = reimport(noMeasures);
    REQUIRE(back.song.trackCount() == 1);
    CHECK(back.song.track(0).name() == "Piano");
}
