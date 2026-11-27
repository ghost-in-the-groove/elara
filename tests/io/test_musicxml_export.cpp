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
#include <fstream>
#include <ios>
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
    song.measureHeader(0).section = "Intro";
    song.measureHeader(2).section = "Verse & <Chorus>";
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
        // A crescendo over the barline, ending on the first beat of measure 2.
        beats[2].effects.hairpin = Hairpin::Crescendo;
        beats[3].effects.hairpin = Hairpin::Crescendo;
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
        marked.effects.tenuto = true; // portato (ADR-0088)
        marked.effects.accent = Accent::Accent;
        beats = {beatOf(quarter, {tied}),
                 beatOf(triplet8, {bend}),
                 beatOf(triplet8, {release}),
                 beatOf(triplet8, {preBend}),
                 beatOf(triplet4, {preRelease}),
                 beatOf(triplet8, {marked}),
                 beatOf(quarter, {fretted(6, 0, Dynamic::P), fretted(5, 2, Dynamic::P),
                                  fretted(4, 2, Dynamic::P)})};
        beats[0].effects.hairpin = Hairpin::Crescendo;
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
        beats[1].effects.hairpin = Hairpin::Diminuendo;
        beats[2].effects.hairpin = Hairpin::Diminuendo;
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
        beats[1].effects.bassTechnique = BassTechnique::Tap;
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
        // A crescendo on the upper staff of the two, up to the forte.
        upper.measure(1).voices[0].beats[0].effects.hairpin = Hairpin::Crescendo;
        upper.measure(1).voices[0].beats[1].effects.hairpin = Hairpin::Crescendo;
        upper.measure(1).voices[1].beats = {
            beatOf(half, {pitched(Pitch(Step::A, 0, 4))}),
            beatOf(half, {pitched(Pitch(Step::B, 0, 4), Dynamic::F)})};
        // Grace notes (ADR-0079): an eighth before the D, a chord on the F.
        upper.measure(1).voices[0].beats[1].graces = {
            Grace{.duration = eighth, .notes = {pitched(Pitch(Step::E, 0, 5))}}};
        upper.measure(1).voices[0].beats[3].graces = {
            Grace{.duration = Duration(NoteValue::Sixteenth),
                  .notes = {pitched(Pitch(Step::G, 0, 5), Dynamic::F),
                            pitched(Pitch(Step::B, -1, 5), Dynamic::F)},
                  .onBeat = true}};
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

    // ---- The mix: MIDI volume and pan (mute and solo have no place).
    song.track(g).setMix(TrackMix{.volume = 0.7, .pan = -0.4});
    song.track(d).setMix(TrackMix{.volume = 0.9});
    song.track(b).setMix(TrackMix{.volume = 0.795, .pan = 0.25});

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
                // Not at the very start, which is the song's tempo.
                TempoChange{.offset = Fraction(pick(m == 0 ? 1 : 0, 3), 8),
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
                                        // Trills to the next step of the key, or a
                                        // semitone past it with an accidental mark.
                                        if (pick(0, 13) == 0) {
                                            note.effects.trill =
                                                Trill{.semitones = static_cast<std::int8_t>(
                                                          semitonesToNextStep(
                                                              note.pitch,
                                                              song.measureHeader(m).keySignature) +
                                                          pick(0, 1))};
                                        }
                                        if (pick(0, 13) == 0) { // ADR-0091
                                            note.effects.ornament =
                                                static_cast<Ornament>(pick(1, 4));
                                        }
                                    }
                                    note.effects.staccato = pick(0, 9) == 0;
                                    note.effects.tenuto = pick(0, 11) == 0;
                                    note.effects.staccatissimo = // ADR-0094
                                        !note.effects.staccato && pick(0, 13) == 0;
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

TEST_CASE("MusicXML export: grace notes on a guitar keep their strings and slides",
          "[musicxml][export]") {
    core::Song song;
    song.appendMeasures(1);
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("electric-guitar")));
    core::Staff& staff = song.track(g).staff(0);
    core::Note slide = staff.frettedNote(3, 5);
    slide.effects.slideOut = core::SlideOut::Legato;
    auto& beats = staff.measure(0).voices[0].beats;
    beats = {beatOf(core::Duration(core::NoteValue::Half), {staff.frettedNote(3, 7)}),
             beatOf(core::Duration(core::NoteValue::Half), {staff.frettedNote(2, 5)})};
    beats[0].graces = {
        core::Grace{.duration = core::Duration(core::NoteValue::Sixteenth), .notes = {slide}}};
    beats[1].graces = {core::Grace{.duration = core::Duration(core::NoteValue::Eighth),
                                   .notes = {staff.frettedNote(2, 3), staff.frettedNote(1, 3)},
                                   .onBeat = true}};
    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const std::string xml = exportMusicXml(song).data;
    CHECK(xml.find(R"(<grace slash="yes"/>)") != std::string::npos);
    CHECK(xml.find("<grace/>") != std::string::npos);
}

TEST_CASE("MusicXML export: chord symbols keep their names and diagrams", "[musicxml][export]") {
    // ADR-0080: names MusicXML can spell, one it cannot ("6/9" is a suffix,
    // not a bass), one that is no pitch, a diagram alone, on a guitar and on
    // the second staff of a piano.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("electric-guitar")));
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    core::Staff& staff = song.track(g).staff(0);
    constexpr std::int8_t x = core::ChordDiagram::kMuted;
    const auto chord = [](std::string name, std::optional<core::ChordDiagram> diagram = {}) {
        return core::SharedOptional(core::ChordSymbol{.name = std::move(name), .diagram = diagram});
    };
    const core::Duration quarter(core::NoteValue::Quarter);
    auto& beats = staff.measure(0).voices[0].beats;
    beats = {beatOf(quarter, {staff.frettedNote(3, 2)}), beatOf(quarter, {staff.frettedNote(2, 3)}),
             beatOf(quarter, {staff.frettedNote(1, 5)}), beatOf(quarter)};
    beats[0].chord = chord("Am", core::ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}});
    beats[1].chord = chord("Bb7/F");
    beats[2].chord =
        chord("C#m7b5", core::ChordDiagram{.firstFret = 5, .frets = {x, 5, 5, 4, 5, x}});
    beats[3].chord = chord("N.C.");
    auto& left = song.track(p).staff(1).measure(0).voices[0].beats;
    left = {beatOf(core::Duration(core::NoteValue::Half),
                   {core::Note{.pitch = core::Pitch::fromMidi(48)}}),
            beatOf(core::Duration(core::NoteValue::Half),
                   {core::Note{.pitch = core::Pitch::fromMidi(43)}})};
    left[0].chord = chord("G6/9");
    left[1].chord = chord("", core::ChordDiagram{.firstFret = 1, .frets = {3, 0, 0, 0, 2, 3}});

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const std::string xml = exportMusicXml(song).data;
    CHECK(xml.find(R"(<harmony print-frame="yes">)") != std::string::npos);
    CHECK(xml.find(R"(<kind text="m">minor</kind>)") != std::string::npos);
    CHECK(xml.find("<root-alter>-1</root-alter>") != std::string::npos);
    CHECK(xml.find("<bass-step>F</bass-step>") != std::string::npos);
    CHECK(xml.find(R"(<kind text="m7b5">half-diminished</kind>)") != std::string::npos);
    CHECK(xml.find("<first-fret>5</first-fret>") != std::string::npos);
    CHECK(xml.find(R"(<kind text="6/9">other</kind>)") != std::string::npos);
    CHECK(xml.find(R"(<kind text="N.C.">none</kind>)") != std::string::npos);
    CHECK(exportMusicXml(song).warnings.empty());
}

TEST_CASE("MusicXML export: beat text comes back as words", "[musicxml][export]") {
    // ADR-0081: above the staff it belongs to, on a rest as well as a note.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("electric-guitar")));
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    core::Staff& staff = song.track(g).staff(0);
    const core::Duration half(core::NoteValue::Half);
    auto& beats = staff.measure(0).voices[0].beats;
    beats = {beatOf(half, {staff.frettedNote(3, 2)}), beatOf(half)};
    beats[0].text = core::SharedOptional<std::string>("let ring");
    beats[1].text = core::SharedOptional<std::string>("a <tempo> & \"fill\"");
    auto& left = song.track(p).staff(1).measure(0).voices[0].beats;
    left = {beatOf(core::Duration(core::NoteValue::Whole),
                   {core::Note{.pitch = core::Pitch::fromMidi(48)}})};
    left[0].text = core::SharedOptional<std::string>("Ped. ad lib.");

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const std::string xml = exportMusicXml(song).data;
    CHECK(xml.find("<words>let ring</words>") != std::string::npos);
    CHECK(xml.find("<words>a &lt;tempo&gt; &amp; &quot;fill&quot;</words>") != std::string::npos);
    CHECK(xml.find(R"(<direction placement="above">)") != std::string::npos);
    CHECK(exportMusicXml(song).warnings.empty());
}

TEST_CASE("MusicXML export: fermatas come back with their lengths", "[musicxml][export]") {
    // ADR-0085: angled, normal and square fermatas on a note, a chord and a rest.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const auto key = [](int midi) {
        return core::Note{.pitch = core::Pitch::fromMidi(midi)};
    };
    auto& beats = song.track(p).staff(0).measure(0).voices[0].beats;
    beats = {beatOf(quarter, {key(60)}), beatOf(quarter, {key(64), key(67)}), beatOf(quarter),
             beatOf(quarter, {key(72)})};
    beats[0].effects.fermata = core::Fermata::Short;
    beats[1].effects.fermata = core::Fermata::Normal;
    beats[2].effects.fermata = core::Fermata::Long;

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    CHECK(exported.data.find(R"(<fermata type="upright">angled</fermata>)") != std::string::npos);
    CHECK(exported.data.find(R"(<fermata type="upright">normal</fermata>)") != std::string::npos);
    CHECK(exported.data.find(R"(<fermata type="upright">square</fermata>)") != std::string::npos);
}

TEST_CASE("MusicXML export: octave lines come back on every voice of their staff",
          "[musicxml][export]") {
    // ADR-0089: one octave-shift per run of the staff's first voice, which the
    // import gives every voice; 8va over a rest and into the next measure,
    // 15ma, and 8vb on the left hand.
    core::Song song;
    song.appendMeasures(2);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const core::Duration half(core::NoteValue::Half);
    const core::Duration whole(core::NoteValue::Whole);
    const auto key = [](int midi) {
        return core::Note{.pitch = core::Pitch::fromMidi(midi)};
    };
    using enum core::Ottava;
    core::Staff& right = song.track(p).staff(0);
    auto& first = right.measure(0).voices[0].beats;
    first = {beatOf(quarter, {key(72)}), beatOf(quarter, {key(76)}), beatOf(quarter, {key(79)}),
             beatOf(quarter)};
    first[1].effects.ottava = first[2].effects.ottava = first[3].effects.ottava = Alta;
    auto& lower = right.measure(0).voices[1].beats;
    lower = {beatOf(half, {key(69)}), beatOf(half, {key(72)})};
    lower[1].effects.ottava = Alta;
    auto& second = right.measure(1).voices[0].beats;
    second = {beatOf(quarter, {key(84)}), beatOf(quarter, {key(74)}), beatOf(half, {key(88)})};
    second[0].effects.ottava = Alta;
    second[2].effects.ottava = QuindicesimaAlta;
    core::Staff& left = song.track(p).staff(1);
    for (std::size_t m = 0; m < 2; ++m) {
        left.measure(m).voices[0].beats = {beatOf(whole, {key(36)})};
        left.measure(m).voices[0].beats[0].effects.ottava = Bassa;
    }

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const auto count = [&](std::string_view text) {
        std::size_t found = 0;
        for (std::size_t at = exported.data.find(text); at != std::string::npos;
             at = exported.data.find(text, at + 1)) {
            ++found;
        }
        return found;
    };
    CHECK(count(R"(<octave-shift type="down" size="8")") == 1);
    CHECK(count(R"(<octave-shift type="down" size="15")") == 1);
    CHECK(count(R"(<octave-shift type="up" size="8")") == 1);
    CHECK(count(R"(<octave-shift type="stop")") == 3);
}

TEST_CASE("MusicXML export: breath marks and caesuras come back after their beats",
          "[musicxml][export]") {
    // ADR-0095: written once, on the beat's first note.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration half(core::NoteValue::Half);
    const auto key = [](int midi) {
        return core::Note{.pitch = core::Pitch::fromMidi(midi)};
    };
    core::Beat comma = beatOf(half, {key(60), key(64)});
    comma.effects.breath = core::Breath::Comma;
    core::Beat caesura = beatOf(half, {key(67)});
    caesura.effects.breath = core::Breath::Caesura;
    song.track(p).staff(0).measure(0).voices[0].beats = {comma, caesura};
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    std::size_t marks = 0;
    for (std::size_t at = exported.data.find("<breath-mark/>"); at != std::string::npos;
         at = exported.data.find("<breath-mark/>", at + 1)) {
        ++marks;
    }
    CHECK(marks == 1);
    CHECK(exported.data.find("<caesura/>") != std::string::npos);
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: the sustain pedal comes back under its staff", "[musicxml][export]") {
    // ADR-0092: a start where it goes down, a change, a stop after the last
    // beat under it; over a rest and on into the next measure.
    core::Song song;
    song.appendMeasures(2);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const core::Duration half(core::NoteValue::Half);
    const core::Duration whole(core::NoteValue::Whole);
    const auto key = [](int midi) {
        return core::Note{.pitch = core::Pitch::fromMidi(midi)};
    };
    using enum core::Pedal;
    for (std::size_t m = 0; m < 2; ++m) {
        song.track(p).staff(0).measure(m).voices[0].beats = {beatOf(whole, {key(72)})};
    }
    core::Staff& left = song.track(p).staff(1);
    auto& first = left.measure(0).voices[0].beats;
    first = {beatOf(quarter, {key(48)}), beatOf(quarter, {key(52)}), beatOf(half, {key(55)})};
    first[0].effects.pedal = first[1].effects.pedal = Down;
    first[2].effects.pedal = Change;
    auto& second = left.measure(1).voices[0].beats;
    second = {beatOf(half), beatOf(half, {key(48)})};
    second[0].effects.pedal = Down;

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const auto count = [&](std::string_view text) {
        std::size_t found = 0;
        for (std::size_t at = exported.data.find(text); at != std::string::npos;
             at = exported.data.find(text, at + 1)) {
            ++found;
        }
        return found;
    };
    CHECK(count(R"(<pedal type="start")") == 1);
    CHECK(count(R"(<pedal type="change")") == 1);
    CHECK(count(R"(<pedal type="stop")") == 1);
}

TEST_CASE("MusicXML export: trills come back, with an accidental mark for an unusual note",
          "[musicxml][export]") {
    // ADR-0090: in F major A trills with Bb unless written otherwise; a trill
    // to B natural needs an accidental mark. Trills down and other speeds warn.
    core::Song song;
    song.appendMeasures(1);
    song.measureHeader(0).keySignature = core::KeySignature(-1, core::KeyMode::Major);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const auto trilled = [](std::string_view pitch, int semitones) {
        core::Note note{.pitch = *core::Pitch::parse(pitch)};
        note.effects.trill = core::Trill{.semitones = static_cast<std::int8_t>(semitones)};
        return note;
    };
    auto& beats = song.track(p).staff(0).measure(0).voices[0].beats;
    beats = {beatOf(quarter, {trilled("A4", 1)}), beatOf(quarter, {trilled("A4", 2)}),
             beatOf(quarter, {trilled("E5", 1)}), beatOf(quarter)};

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    CHECK(exported.data.find("<accidental-mark>natural</accidental-mark>") != std::string::npos);

    beats[3] = beatOf(quarter, {trilled("C5", -2)});
    beats[3].notes[0].effects.trill.speed = core::NoteValue::Sixteenth;
    const auto warned = exportMusicXml(song);
    CHECK(hasWarning(warned.warnings,
                     "trills to a lower note written as trills to the next step up"));

    // An alto sax is written a major sixth up, in A major for C major: a
    // concert G trilling with Ab is a written E trilling with F natural.
    core::Song sax;
    sax.appendMeasures(1);
    const std::size_t a =
        sax.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("alto-sax")));
    sax.track(a).staff(0).measure(0).voices[0].beats = {
        beatOf(core::Duration(core::NoteValue::Whole), {trilled("G4", 1)})};
    const auto saxXml = exportMusicXml(sax);
    CHECK(saxXml.warnings.empty());
    CHECK(saxXml.data.find("<accidental-mark>natural</accidental-mark>") != std::string::npos);
    requireSame(sax, roundTrip(sax));
}

TEST_CASE("MusicXML export: mordents and turns come back", "[musicxml][export]") {
    // ADR-0091: each kind on a note of its own, a turn with a trill as well.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const auto ornamented = [](core::Ornament ornament) {
        core::Note note{.pitch = *core::Pitch::parse("E5")};
        note.effects.ornament = ornament;
        return note;
    };
    core::Note both = ornamented(core::Ornament::Turn);
    both.effects.trill = core::Trill{.semitones = 1};
    song.track(p).staff(0).measure(0).voices[0].beats = {
        beatOf(quarter, {ornamented(core::Ornament::Mordent)}),
        beatOf(quarter, {ornamented(core::Ornament::UpperMordent)}),
        beatOf(quarter, {ornamented(core::Ornament::InvertedTurn)}), beatOf(quarter, {both})};
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    CHECK(exported.data.find("<inverted-mordent/>") != std::string::npos);
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: fingerings come back by the kind of instrument", "[musicxml][export]") {
    // ADR-0093: a piano's thumb is 1, a guitar's T; the plucking fingers too.
    core::Song song;
    song.appendMeasures(1);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("classical-guitar")));
    const core::Duration whole(core::NoteValue::Whole);
    core::Note key{.pitch = *core::Pitch::parse("C4")};
    key.effects.fingering = core::Finger::Thumb;
    song.track(p).staff(0).measure(0).voices[0].beats = {beatOf(whole, {key})};
    core::Staff& strings = song.track(g).staff(0);
    core::Note low = strings.frettedNote(6, 3);
    low.effects.fingering = core::Finger::Ring;
    low.effects.pluck = core::Finger::Thumb;
    core::Note high = strings.frettedNote(2, 1);
    high.effects.fingering = core::Finger::Thumb;
    high.effects.pluck = core::Finger::Little;
    strings.measure(0).voices[0].beats = {beatOf(whole, {low, high})};
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    CHECK(exported.data.find("<fingering>1</fingering>") != std::string::npos);
    CHECK(exported.data.find("<fingering>3</fingering>") != std::string::npos);
    CHECK(exported.data.find("<fingering>T</fingering>") != std::string::npos);
    CHECK(exported.data.find("<pluck>c</pluck>") != std::string::npos);
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: slurs come back on their voices", "[musicxml][export]") {
    // ADR-0086: a slur starts on the first beat slurred in a row and stops on
    // the beat after the last, across barlines, numbered by staff and voice; a
    // beat slurred to nothing after it is not written.
    core::Song song;
    song.appendMeasures(2);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration quarter(core::NoteValue::Quarter);
    const core::Duration half(core::NoteValue::Half);
    const auto key = [](int midi) {
        return core::Note{.pitch = core::Pitch::fromMidi(midi)};
    };
    const auto slurred = [](core::Beat beat) {
        beat.effects.slur = true;
        return beat;
    };
    core::Staff& upper = song.track(p).staff(0);
    upper.measure(0).voices[0].beats = {slurred(beatOf(quarter, {key(72)})),
                                        beatOf(quarter, {key(74)}), beatOf(quarter, {key(76)}),
                                        slurred(beatOf(quarter, {key(77)}))};
    upper.measure(0).voices[1].beats = {slurred(beatOf(half, {key(60)})), beatOf(half, {key(62)})};
    upper.measure(1).voices[0].beats = {
        beatOf(quarter, {key(79)}), slurred(beatOf(quarter, {key(81)})),
        slurred(beatOf(quarter, {key(83)})), beatOf(quarter, {key(84)})};
    core::Staff& lower = song.track(p).staff(1);
    lower.measure(1).voices[0].beats = {
        slurred(beatOf(core::Duration(core::NoteValue::Whole), {key(48)}))};

    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const auto count = [&](std::string_view text) {
        std::size_t found = 0;
        for (auto at = exported.data.find(text); at != std::string::npos;
             at = exported.data.find(text, at + 1)) {
            ++found;
        }
        return found;
    };
    CHECK(count(R"(<slur type="start" number="1"/>)") == 3);
    CHECK(count(R"(<slur type="stop" number="1"/>)") == 3);
    CHECK(count(R"(<slur type="start" number="2"/>)") == 1);
    CHECK(count(R"(<slur type="stop" number="2"/>)") == 1);
    CHECK(count("<slur ") == 8);

    core::Song expected = song;
    expected.track(p).staff(1).measure(1).voices[0].beats[0].effects.slur = false;
    requireSame(expected, roundTrip(song));
}

TEST_CASE("MusicXML export: clef changes come back on their staves", "[musicxml][export][clef]") {
    // ADR-0087: a measure that changes a staff's clef writes it in its
    // attributes, numbered by staff; a change to the clef in force writes none.
    core::Song song;
    song.appendMeasures(4);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    const core::Duration whole(core::NoteValue::Whole);
    for (std::size_t s = 0; s < 2; ++s) {
        for (std::size_t m = 0; m < 4; ++m) {
            song.track(p).staff(s).measure(m).voices[0].beats = {
                beatOf(whole, {core::Note{.pitch = core::Pitch::fromMidi(s == 0 ? 72 : 48)}})};
        }
    }
    song.track(p).staff(1).measure(1).clef = core::Clef::Treble;
    song.track(p).staff(1).measure(3).clef = core::Clef::Bass;
    song.track(p).staff(0).measure(2).clef = core::Clef::Treble; // already in force

    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const std::string& xml = exported.data;
    const std::string treble = R"(<clef number="2"><sign>G</sign><line>2</line></clef>)";
    const std::string bass = R"(<clef number="2"><sign>F</sign><line>4</line></clef>)";
    // A measure's text without the indentation between its tags.
    const auto measure = [&](int number) {
        const auto from = xml.find(std::format(R"(<measure number="{}")", number));
        const auto to = xml.find("</measure>", from);
        std::string text;
        bool between = false;
        for (const char c : xml.substr(from, to - from)) {
            if (between && (c == ' ' || c == '\n' || c == '\r')) {
                continue;
            }
            between = c == '>' || (between && c != '<');
            text.push_back(c);
        }
        return text;
    };
    CHECK(measure(2).find(treble) != std::string::npos);
    CHECK(measure(3).find("<clef") == std::string::npos);
    CHECK(measure(3).find("<attributes>") == std::string::npos);
    CHECK(measure(4).find(bass) != std::string::npos);

    core::Song expected = song;
    expected.track(p).staff(0).measure(2).clef = std::nullopt;
    requireSame(expected, roundTrip(song));
}

TEST_CASE("MusicXML export: measure repeats come back, their music written out",
          "[musicxml][export][simile]") {
    // ADR-0102: a run of repeats starts with <measure-repeat type="start">
    // and the measures it holds, numbered by staff; the first measure after
    // it stops it. Each repeat writes out the music it plays.
    core::Song song;
    song.appendMeasures(7);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    core::Staff& right = song.track(p).staff(0);
    core::Staff& left = song.track(p).staff(1);
    const core::Duration half(core::NoteValue::Half);
    for (const int m : {0, 3, 6}) {
        right.measure(static_cast<std::size_t>(m)).voices[0].beats = {
            beatOf(half, {core::Note{.pitch = core::Pitch::fromMidi(72 + m)}}),
            beatOf(half, {core::Note{.pitch = core::Pitch::fromMidi(74)}})};
    }
    for (std::size_t m = 0; m < 7; ++m) {
        left.measure(m).voices[0].beats = {
            beatOf(core::Duration(core::NoteValue::Whole),
                   {core::Note{.pitch = core::Pitch::fromMidi(48 + static_cast<int>(m))}})};
    }
    right.measure(1).repeat = core::MeasureRepeat::One;
    right.measure(2).repeat = core::MeasureRepeat::One;
    right.measure(4).repeat = core::MeasureRepeat::TwoFirst;
    right.measure(5).repeat = core::MeasureRepeat::TwoSecond;

    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const std::string& xml = exported.data;
    const auto measure = [&](int number) {
        const auto from = xml.find(std::format(R"(<measure number="{}")", number));
        return xml.substr(from, xml.find("</measure>", from) - from);
    };
    CHECK(measure(2).find(R"(<measure-style number="1">)") != std::string::npos);
    CHECK(measure(2).find(R"(<measure-repeat type="start">1</measure-repeat>)") !=
          std::string::npos);
    CHECK(measure(3).find("measure-repeat") == std::string::npos);
    CHECK(measure(4).find(R"(<measure-repeat type="stop"/>)") != std::string::npos);
    CHECK(measure(5).find(R"(<measure-repeat type="start">2</measure-repeat>)") !=
          std::string::npos);
    CHECK(measure(7).find(R"(<measure-repeat type="stop"/>)") != std::string::npos);
    // The music written out: C5 D5 in measures 2 and 3, as in measure 1.
    for (const int number : {2, 3}) {
        const std::string text = measure(number);
        CHECK(text.find("<step>C</step>") != std::string::npos);
        CHECK(text.find("<step>D</step>") != std::string::npos);
    }
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: a guitar's measure repeat covers its TAB staff; one that does not "
          "count goes",
          "[musicxml][export][simile]") {
    core::Song song;
    song.appendMeasures(3);
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("electric-guitar")));
    core::Staff& guitar = song.track(g).staff(0);
    guitar.measure(0).repeat = core::MeasureRepeat::One; // nothing before it
    guitar.measure(1).voices[0].beats = {
        beatOf(core::Duration(core::NoteValue::Whole), {guitar.frettedNote(1, 3)})};
    guitar.measure(2).repeat = core::MeasureRepeat::One;

    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings ==
          std::vector<std::string>{std::format("{}: measure repeats that do not count dropped "
                                               "(measure 1)",
                                               song.track(g).name())});
    const std::string& xml = exported.data;
    // One style for the notation staff and the TAB staff written from it.
    CHECK(xml.find("<measure-style>") != std::string::npos);
    CHECK(xml.find("<measure-style number") == std::string::npos);
    CHECK(xml.find(R"(<measure-repeat type="start">1</measure-repeat>)") ==
          xml.rfind("<measure-repeat"));

    guitar.measure(0).repeat = core::MeasureRepeat::None;
    const core::Song back = roundTrip(song);
    const core::Staff& read = back.track(0).staff(0);
    CHECK(read.measure(2).repeat == core::MeasureRepeat::One);
    CHECK(read.measure(2).isEmpty());
}

TEST_CASE("MusicXML export: the triplet feel comes back as swing where it changes",
          "[musicxml][export][feel]") {
    // ADR-0104: written once, in the first part, where the feel changes.
    core::Song song;
    song.appendMeasures(5);
    for (const char* preset : {"electric-guitar", "electric-bass"}) {
        const std::size_t t =
            song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset(preset)));
        for (std::size_t m = 0; m < 5; ++m) {
            song.track(t).staff(0).measure(m).voices[0].beats = {
                beatOf(core::Duration(core::NoteValue::Whole),
                       {song.track(t).staff(0).frettedNote(1, 3)})};
        }
    }
    using core::TripletFeel;
    song.measureHeader(0).tripletFeel = TripletFeel::Eighth;
    song.measureHeader(1).tripletFeel = TripletFeel::Eighth;
    song.measureHeader(2).tripletFeel = TripletFeel::ScottishSixteenth;
    song.measureHeader(4).tripletFeel = TripletFeel::DottedEighth;
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    std::string xml; // without the indentation between elements
    bool between = false;
    for (const char c : exported.data) {
        if (between && (c == ' ' || c == '\n' || c == '\r')) {
            continue;
        }
        between = c == '>';
        xml += c;
    }
    const auto count = [&xml](std::string_view text) {
        std::size_t n = 0;
        for (auto at = xml.find(text); at != std::string::npos; at = xml.find(text, at + 1)) {
            ++n;
        }
        return n;
    };
    CHECK(count("<swing>") == 4);
    CHECK(count("<swing><first>2</first><second>1</second><swing-type>eighth</swing-type>") == 1);
    CHECK(count("<swing><first>1</first><second>3</second><swing-type>16th</swing-type>") == 1);
    CHECK(count("<swing><straight/></swing>") == 1);
    CHECK(count("<swing><first>3</first><second>1</second><swing-type>eighth</swing-type>") == 1);
    requireSame(song, roundTrip(song));
}

TEST_CASE("MusicXML export: double barlines come back as light-light barlines",
          "[musicxml][export][barline]") {
    // ADR-0103: one under an end repeat or at the song's end does not show,
    // and is not written.
    core::Song song;
    song.appendMeasures(4);
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    for (std::size_t m = 0; m < 4; ++m) {
        song.track(p).staff(0).measure(m).voices[0].beats = {
            beatOf(core::Duration(core::NoteValue::Whole),
                   {core::Note{.pitch = core::Pitch::fromMidi(60 + static_cast<int>(m))}})};
    }
    song.measureHeader(0).doubleBar = true;
    song.measureHeader(2).doubleBar = true;
    const auto exported = exportMusicXml(song);
    CHECK(exported.warnings.empty());
    const std::string& xml = exported.data;
    const auto measure = [&](int number) {
        const auto from = xml.find(std::format(R"(<measure number="{}")", number));
        return xml.substr(from, xml.find("</measure>", from) - from);
    };
    // Written once, at the right of the measure, though the part has two staves.
    for (const int number : {1, 3}) {
        const std::string text = measure(number);
        const std::string_view style = "<bar-style>light-light</bar-style>";
        const auto at = text.find(style);
        REQUIRE(at != std::string::npos);
        CHECK(text.find("light-light", at + style.size()) == std::string::npos);
    }
    CHECK(measure(2).find("light-light") == std::string::npos);
    requireSame(song, roundTrip(song));

    song.measureHeader(1).repeatTimes = 2;
    song.measureHeader(1).doubleBar = true;
    song.measureHeader(3).doubleBar = true;
    const std::string under = exportMusicXml(song).data;
    CHECK(under.find("light-light") != std::string::npos);
    const auto second = under.find(R"(<measure number="2")");
    const std::string closing = under.substr(second, under.find("</measure>", second) - second);
    CHECK(closing.find("light-light") == std::string::npos);
    CHECK(closing.find("light-heavy") != std::string::npos);
}

TEST_CASE("MusicXML export: lyrics come back with their verses, hyphens and extenders",
          "[musicxml][export]") {
    // ADR-0084: each verse sung on a beat is a <lyric> on its first note, the
    // syllabic following from the hyphens, also across barlines and on rests;
    // the TAB copy of a guitar sings nothing.
    using core::Syllable;
    using Verses = std::vector<Syllable>;
    core::Song song;
    song.appendMeasures(2);
    const std::size_t g =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("electric-guitar")));
    const std::size_t p =
        song.addTrack(core::Track::fromPreset(*core::findInstrumentPreset("piano")));
    core::Staff& staff = song.track(g).staff(0);
    const core::Duration quarter(core::NoteValue::Quarter);
    const core::Duration eighth(core::NoteValue::Eighth);
    auto& first = staff.measure(0).voices[0].beats;
    first = {beatOf(quarter, {staff.frettedNote(3, 0)}), beatOf(quarter, {staff.frettedNote(3, 2)}),
             beatOf(quarter, {staff.frettedNote(2, 0)}),
             beatOf(quarter, {staff.frettedNote(2, 1)})};
    first[0].lyrics = core::SharedOptional(Verses{{.text = "be", .hyphen = true}, {.text = "oh"}});
    first[1].lyrics = core::SharedOptional(Verses{{.text = "low", .extend = true}});
    first[3].lyrics = core::SharedOptional(Verses{{.text = "sha", .hyphen = true}});
    auto& second = staff.measure(1).voices[0].beats;
    second = {beatOf(quarter, {staff.frettedNote(2, 3)}), beatOf(quarter),
              beatOf(eighth, {staff.frettedNote(1, 0)}), beatOf(eighth, {staff.frettedNote(1, 1)}),
              beatOf(quarter, {staff.frettedNote(1, 3)})};
    second[0].lyrics = core::SharedOptional(Verses{{.text = "dows"}});
    second[1].lyrics = core::SharedOptional(Verses{{}, {.text = "ah"}});
    second[2].lyrics = core::SharedOptional(Verses{{.text = "a", .hyphen = true}});
    second[3].lyrics = core::SharedOptional(Verses{{.text = "ga", .hyphen = true}});
    second[4].lyrics = core::SharedOptional(Verses{{.text = "in"}});
    auto& left = song.track(p).staff(1).measure(0).voices[0].beats;
    left = {beatOf(core::Duration(core::NoteValue::Whole),
                   {core::Note{.pitch = core::Pitch::fromMidi(48)}})};
    left[0].lyrics = core::SharedOptional(Verses{{.text = "lo"}});
    fillVoices(song);

    const core::Song back = roundTrip(song);
    requireSame(song, back);
    // One element per line in the document: joined to find whole lyrics.
    std::string xml;
    const std::string pretty = exportMusicXml(song).data;
    for (std::size_t i = 0; i < pretty.size(); ++i) {
        if (pretty[i] == '\n') {
            while (i + 1 < pretty.size() && pretty[i + 1] == ' ') {
                ++i;
            }
        } else {
            xml += pretty[i];
        }
    }
    CHECK(xml.find(R"(<lyric number="1"><syllabic>begin</syllabic><text>be</text></lyric>)") !=
          std::string::npos);
    CHECK(xml.find(R"(<lyric number="2"><syllabic>single</syllabic><text>oh</text></lyric>)") !=
          std::string::npos);
    CHECK(xml.find(R"(<syllabic>end</syllabic><text>low</text><extend/></lyric>)") !=
          std::string::npos);
    CHECK(xml.find(R"(<syllabic>end</syllabic><text>dows</text>)") != std::string::npos);
    CHECK(xml.find(R"(<syllabic>middle</syllabic><text>ga</text>)") != std::string::npos);
    std::size_t lyrics = 0;
    for (std::size_t at = xml.find("<lyric "); at != std::string::npos;
         at = xml.find("<lyric ", at + 1)) {
        ++lyrics;
    }
    CHECK(lyrics == 10);
}

TEST_CASE("MusicXML export: a gradual tempo change is a word with dashes to the next tempo",
          "[musicxml][export]") {
    // ADR-0082: "rit." or "accel." by the speeds, then dashes to the next tempo.
    using namespace elara::core;
    Song song;
    song.setStartTempo(Tempo(100));
    song.appendMeasures(3);
    song.measureHeader(0).tempoChanges = {
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(100), .gradual = true}};
    song.measureHeader(1).tempoChanges = {
        TempoChange{.tempo = Tempo(60)},
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(80), .gradual = true}};
    // Half = 60 is faster than quarter = 80.
    song.measureHeader(2).tempoChanges = {
        TempoChange{.tempo = Tempo(60, Duration(NoteValue::Half))}};
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    fillVoices(song);

    const Song back = roundTrip(song);
    requireSame(song, back);
    const std::string xml = exportMusicXml(song).data;
    const auto count = [&](std::string_view needle) {
        std::size_t found = 0;
        for (auto at = xml.find(needle); at != std::string::npos; at = xml.find(needle, at + 1)) {
            ++found;
        }
        return found;
    };
    CHECK(count(R"(<words font-style="italic">rit.</words>)") == 1);
    CHECK(count(R"(<words font-style="italic">accel.</words>)") == 1);
    CHECK(count(R"(<dashes type="start"/>)") == 2);
    CHECK(count(R"(<dashes type="stop"/>)") == 2);
}

TEST_CASE("MusicXML export: random songs come back unchanged", "[musicxml][export]") {
    for (const std::uint32_t seed : {1U, 2U, 3U, 20261123U}) {
        INFO("seed " << seed);
        const core::Song song = randomSong(seed);
        requireSame(song, roundTrip(song));
    }
}

TEST_CASE("MusicXML export: signs and jumps come back unchanged", "[musicxml][export]") {
    using namespace elara::core;
    // Every jump, to every ending, next to every sign (ADR-0049).
    for (const JumpTo to : {JumpTo::Start, JumpTo::Segno, JumpTo::SegnoSegno}) {
        for (const JumpThen then :
             {JumpThen::End, JumpThen::Fine, JumpThen::Coda, JumpThen::DoubleCoda}) {
            Song song;
            song.appendMeasures(7);
            song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
            song.measureHeader(1).segno = true;
            song.measureHeader(2).segnoSegno = true;
            song.measureHeader(2).fine = true;
            song.measureHeader(3).toCoda = true;
            song.measureHeader(3).toDoubleCoda = true;
            song.measureHeader(4).jump = to;
            song.measureHeader(4).jumpThen = then;
            song.measureHeader(5).coda = true;
            song.measureHeader(5).segno = true;
            song.measureHeader(6).doubleCoda = true;
            fillVoices(song);
            INFO(std::format("jump {} then {}", static_cast<int>(to), static_cast<int>(then)));
            requireSame(song, roundTrip(song));
        }
    }

    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    song.measureHeader(0).segnoSegno = true;
    song.measureHeader(1).jump = JumpTo::SegnoSegno;
    song.measureHeader(1).jumpThen = JumpThen::DoubleCoda;
    fillVoices(song);
    const std::string xml = exportMusicXml(song).data;
    CHECK(xml.contains("<segno/>"));
    CHECK(xml.contains(R"(<sound segno="segno-segno"/>)"));
    CHECK(xml.contains("<words>D.S.S. al Double Coda</words>"));
    CHECK(xml.contains(R"(<sound dalsegno="segno-segno"/>)"));
}

TEST_CASE("MusicXML export: sections are rehearsal marks", "[musicxml][export]") {
    using namespace elara::core;
    Song song;
    song.appendMeasures(2);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-bass")));
    song.measureHeader(1).section = "Chorus";
    fillVoices(song);
    const std::string xml = exportMusicXml(song).data;
    // Once, in the first part, as the signs are (ADR-0062).
    CHECK(xml.contains("<rehearsal>Chorus</rehearsal>"));
    CHECK(xml.find("<rehearsal>") == xml.rfind("<rehearsal>"));
    requireSame(song, roundTrip(song));
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

TEST_CASE("MusicXML export: the mix is written as each instrument's MIDI volume and pan",
          "[musicxml][export]") {
    using namespace elara::core;
    Song song;
    song.appendMeasures(1);
    const std::size_t piano = song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    const std::size_t drums = song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    const std::size_t organ = song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    song.track(drums).staff(0).measure(0).voices[0].beats = {
        beatOf(Duration(NoteValue::Half), {pitched(Pitch::fromMidi(36))}),
        beatOf(Duration(NoteValue::Half), {pitched(Pitch::fromMidi(38))})};
    fillVoices(song);
    song.track(piano).setMix(TrackMix{.volume = 1.0, .pan = -1.0, .mute = true});
    song.track(drums).setMix(TrackMix{.volume = 0.5, .pan = 0.5, .solo = true});

    const ExportResult exported = exportMusicXml(song);
    const std::string& xml = exported.data;
    CHECK(xml.contains("<volume>100</volume>"));
    CHECK(xml.contains("<pan>-90</pan>"));
    // Every drum of the kit carries the kit's mix.
    std::size_t halves = 0;
    for (std::size_t at = xml.find("<pan>45</pan>"); at != std::string::npos;
         at = xml.find("<pan>45</pan>", at + 1)) {
        ++halves;
    }
    CHECK(halves == 2);
    CHECK(xml.contains("<volume>50</volume>"));
    // The default mix writes nothing: three instruments carry a volume.
    std::size_t volumes = 0;
    for (std::size_t at = xml.find("<volume>"); at != std::string::npos;
         at = xml.find("<volume>", at + 1)) {
        ++volumes;
    }
    CHECK(volumes == 3);
    CHECK(exported.warnings ==
          std::vector<std::string>{"Piano: mute and solo dropped", "Drums: mute and solo dropped"});

    const ImportResult back = reimport(exported);
    REQUIRE(back.song.trackCount() == 3);
    CHECK(back.song.track(piano).mix() == TrackMix{.volume = 1.0, .pan = -1.0});
    CHECK(back.song.track(drums).mix() == TrackMix{.volume = 0.5, .pan = 0.5});
    CHECK(back.song.track(organ).mix() == TrackMix{});
}

TEST_CASE("MusicXML export: what MusicXML cannot hold is reported and changed predictably",
          "[musicxml][export]") {
    using namespace elara::core;
    Song song;
    song.setStartTempo(Tempo(100.0, Duration(NoteValue::Quarter, 0, Tuplet(3, 2))));
    song.appendMeasures(1);
    song.measureHeader(0).tempoChanges = {
        TempoChange{.offset = Fraction(1, 2), .tempo = Tempo(90.0), .gradual = true}};
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
    beats[1].graces = {elara::core::Grace{.duration = Duration(NoteValue::Eighth),
                                          .notes = {staff.frettedNote(2, 3)}}};
    Note louder = staff.frettedNote(6, 0);
    louder.effects.dynamic = Dynamic::F;
    staff.measure(0).voices[1].beats = {beatOf(Duration(NoteValue::Whole), {louder})};

    const ExportResult exported = exportMusicXml(song);
    const auto& warnings = exported.warnings;
    CHECK(hasWarning(warnings, "Electric Guitar: let ring dropped (measure 1)"));
    CHECK(hasWarning(warnings, "whammy bar dropped"));
    CHECK_FALSE(hasWarning(warnings, "gradual")); // with no tempo after it, nothing to write
    CHECK(hasWarning(warnings, "slap and pop dropped"));
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

TEST_CASE("MusicXML export: a file written from parts on several threads holds the same document",
          "[musicxml][export]") {
    // The parts go to the disk one after the other without being joined first.
    const core::Song song = everythingSong();
    const ExportResult serial = exportMusicXml(song, {.threads = 1});
    const auto directory = std::filesystem::temp_directory_path() / "elara-musicxml-parts-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    for (const unsigned threads : {1U, 2U, 3U, 8U}) {
        INFO(threads << " threads");
        const auto path = directory / std::format("song-{}.musicxml", threads);
        CHECK(exportMusicXmlFile(song, path, {.threads = threads}) == serial.warnings);
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        std::string written(static_cast<std::size_t>(in.tellg()), ' ');
        in.seekg(0);
        in.read(written.data(), static_cast<std::streamsize>(written.size()));
        CHECK(written == serial.data);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("MusicXML export: an .mxl packed from parts written on several threads holds the score",
          "[musicxml][export]") {
    // The parts are packed one after the other without being joined first.
    const core::Song song = everythingSong();
    const ExportResult serial = exportMusicXml(song, {.compressed = true, .threads = 1});
    requireSame(song, reimport(serial).song);
    for (const unsigned threads : {2U, 3U, 8U}) {
        INFO(threads << " threads");
        const ExportResult parallel =
            exportMusicXml(song, {.compressed = true, .threads = threads});
        CHECK(parallel.warnings == serial.warnings);
        CHECK(parallel.data.size() == serial.data.size());
        requireSame(song, reimport(parallel).song);
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
