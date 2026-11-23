// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>
#include <elara/io/midi.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::Fraction;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::KeyMode;
using elara::core::KeySignature;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Tempo;
using elara::core::TempoChange;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuplet;
using elara::io::exportMidi;
using elara::io::ExportResult;
using elara::io::importMidi;
using elara::io::ImportResult;
using elara::io::MidiOptions;

namespace {

std::string bytes(std::initializer_list<int> values) {
    std::string out;
    for (const int value : values) {
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(value)));
    }
    return out;
}

std::vector<std::uint8_t> bytesOf(const std::string& data) {
    return {data.begin(), data.end()};
}

Song pianoSong(std::size_t measures) {
    Song song;
    song.addTrack(
        Track("Piano", Instrument{.name = "Piano", .kind = InstrumentKind::Keyboard}, {Staff{}}));
    song.appendMeasures(measures);
    return song;
}

Track presetTrack(const char* id) {
    return Track::fromPreset(*elara::core::findInstrumentPreset(id));
}

Beat noteBeat(Duration duration, int midi) {
    return Beat{.duration = duration, .notes = {Note{.pitch = Pitch::fromMidi(midi)}}};
}

Beat chordBeat(Duration duration, std::vector<Pitch> pitches, bool tied = false,
               Dynamic dynamic = Dynamic::MF) {
    Beat beat{.duration = duration};
    for (const Pitch& pitch : pitches) {
        Note note{.pitch = pitch};
        note.effects.tiedFromPrevious = tied;
        note.effects.dynamic = dynamic;
        beat.notes.push_back(note);
    }
    return beat;
}

Beat rest(Duration duration) {
    return Beat{.duration = duration};
}

int division(const std::string& smf) {
    return (static_cast<std::uint8_t>(smf[12]) << 8) | static_cast<std::uint8_t>(smf[13]);
}

ImportResult reimport(const Song& song) {
    const auto exported = exportMidi(song);
    const auto imported = importMidi(bytesOf(exported.data));
    REQUIRE(imported.has_value());
    return *imported;
}

// A tiny Standard MIDI File writer for hand-made input: events are {tick,
// bytes} with every status byte written out.
struct Event {
    std::int64_t tick;
    std::string data;
};

std::string varLength(std::uint32_t value) {
    std::string groups(1, static_cast<char>(value & 0x7F));
    while ((value >>= 7) != 0) {
        groups.insert(groups.begin(), static_cast<char>((value & 0x7F) | 0x80));
    }
    return groups;
}

std::string smf(int format, int ticksPerQuarter, const std::vector<std::vector<Event>>& tracks) {
    std::string out =
        bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, format, 0, static_cast<int>(tracks.size()),
               ticksPerQuarter >> 8, ticksPerQuarter & 0xFF});
    for (const auto& events : tracks) {
        std::string body;
        std::int64_t tick = 0;
        for (const Event& event : events) {
            body += varLength(static_cast<std::uint32_t>(event.tick - tick)) + event.data;
            tick = event.tick;
        }
        body += bytes({0, 0xFF, 0x2F, 0});
        const auto size = static_cast<int>(body.size());
        out += bytes({'M', 'T', 'r', 'k', size >> 24, (size >> 16) & 0xFF, (size >> 8) & 0xFF,
                      size & 0xFF}) +
               body;
    }
    return out;
}

Event on(std::int64_t tick, int channel, int key, int velocity = 80) {
    return {tick, bytes({0x90 | channel, key, velocity})};
}

Event off(std::int64_t tick, int channel, int key) {
    return {tick, bytes({0x80 | channel, key, 0})};
}

Event program(std::int64_t tick, int channel, int number) {
    return {tick, bytes({0xC0 | channel, number})};
}

Event metaText(std::int64_t tick, int type, const std::string& text) {
    return {tick, bytes({0xFF, type}) + varLength(static_cast<std::uint32_t>(text.size())) + text};
}

std::vector<Duration> durationsOf(const elara::core::Voice& voice) {
    std::vector<Duration> result;
    for (const Beat& beat : voice.beats) {
        result.push_back(beat.duration);
    }
    return result;
}

std::vector<int> keysOf(const elara::core::Voice& voice) {
    std::vector<int> result;
    for (const Beat& beat : voice.beats) {
        result.push_back(beat.notes.empty() ? -1 : beat.notes.front().pitch.midiNumber());
    }
    return result;
}

// "v0: 4 C4, 8 D4 | v1: ..." for failure messages.
std::string describe(const elara::core::Measure& measure) {
    std::string text;
    for (std::size_t v = 0; v < measure.voices.size(); ++v) {
        if (measure.voices[v].isEmpty()) {
            continue;
        }
        text += std::format("v{}:", v);
        for (const Beat& beat : measure.voices[v].beats) {
            text += " " + beat.duration.toString();
            for (const Note& note : beat.notes) {
                text += (note.effects.tiedFromPrevious ? "~" : "") + note.pitch.toString() +
                        std::format("@{}", static_cast<int>(note.effects.dynamic));
            }
            if (beat.notes.empty()) {
                text += "r";
            }
        }
        text += " | ";
    }
    return text;
}

// What a song plays, independent of how it is written: (track, start, length,
// key, velocity), sorted. Drum lengths are left out: the importer derives them.
using Played = std::tuple<std::size_t, Fraction, Fraction, int, int>;

std::vector<Played> played(const Song& song) {
    std::vector<Played> result;
    for (const auto& note : elara::core::soundingNotes(song)) {
        const bool drums = song.track(note.track).instrument().kind == InstrumentKind::Percussion;
        result.emplace_back(note.track, note.start, drums ? Fraction(0) : note.length, note.key,
                            note.velocity);
    }
    std::ranges::sort(result);
    return result;
}

// Random songs written with what MIDI holds: preset instruments, one voice per
// staff (drums: hands and feet), chords, ties, rests, tuplets on beats, meter,
// key and tempo changes.
Song randomSong(std::uint32_t seed) {
    using namespace elara::core;
    std::mt19937 random(seed);
    const auto pick = [&random](int low, int high) {
        return std::uniform_int_distribution<int>(low, high)(random);
    };
    Song song;
    song.info().title = std::format("Random {}", seed);
    song.setStartTempo(Tempo(static_cast<double>(pick(50, 200))));
    constexpr std::array<TimeSignature, 6> kMeters{TimeSignature(4, 4), TimeSignature(3, 4),
                                                   TimeSignature(6, 8), TimeSignature(2, 2),
                                                   TimeSignature(7, 8), TimeSignature(5, 4)};
    song.appendMeasures(16);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        if (m % 4 == 0) {
            song.measureHeader(m).timeSignature = kMeters[static_cast<std::size_t>(pick(0, 5))];
            song.measureHeader(m).keySignature =
                KeySignature(pick(-4, 4), pick(0, 1) == 0 ? KeyMode::Major : KeyMode::Minor);
        } else {
            song.measureHeader(m) = song.measureHeader(m - 1).continuation();
        }
        if (m > 0 && pick(0, 3) == 0) {
            song.measureHeader(m).tempoChanges.push_back(
                TempoChange{.offset = Fraction(pick(0, 2), 8),
                            .tempo = Tempo(static_cast<double>(pick(40, 220)))});
        }
    }
    for (const char* preset :
         {"piano", "electric-guitar", "alto-sax", "electric-bass", "drum-kit"}) {
        song.addTrack(Track::fromPreset(*findInstrumentPreset(preset)));
    }

    const std::array<Duration, 8> kPlain{
        Duration(NoteValue::Half),       Duration(NoteValue::Quarter),
        Duration(NoteValue::Quarter, 1), Duration(NoteValue::Eighth),
        Duration(NoteValue::Eighth, 1),  Duration(NoteValue::Sixteenth),
        Duration(NoteValue::Sixteenth),  Duration(NoteValue::ThirtySecond)};
    const std::array<Dynamic, 4> kDynamics{Dynamic::P, Dynamic::MF, Dynamic::F, Dynamic::FF};
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        Track& track = song.track(t);
        const bool drums = track.instrument().kind == InstrumentKind::Percussion;
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            // Disjoint ranges keep the piano's staves apart (the importer splits at C4).
            int low = 40;
            int high = 76;
            if (track.staffCount() == 2) {
                low = s == 0 ? 60 : 36;
                high = s == 0 ? 84 : 59;
            }
            Dynamic dynamic = Dynamic::MF;
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                const TimeSignature& time = song.measureHeader(m).timeSignature;
                const Fraction length = time.measureLength();
                const bool compound = time.numerator() % 3 == 0 && time.numerator() > 3;
                for (std::size_t v = 0; v < (drums ? 2U : 1U); ++v) {
                    if (pick(0, 7) == 0) {
                        continue; // a measure rest
                    }
                    auto& beats = track.staff(s).measure(m).voices[v].beats;
                    Fraction used;
                    std::vector<int> previous;
                    while (used < length) {
                        std::vector<Duration> group;
                        const bool onBeat = (used / Fraction(1, 4)).denominator() == 1;
                        if (!drums && !compound && onBeat && used + Fraction(1, 4) <= length &&
                            pick(0, 6) == 0) {
                            group = pick(0, 1) == 0
                                        ? std::vector<Duration>(
                                              3, Duration(NoteValue::Eighth, 0, Tuplet(3, 2)))
                                        : std::vector<Duration>(
                                              5, Duration(NoteValue::Sixteenth, 0, Tuplet(5, 4)));
                        } else {
                            const Duration plain = kPlain[static_cast<std::size_t>(pick(0, 7))];
                            if (used + plain.length() > length) {
                                group.emplace_back(NoteValue::ThirtySecond);
                                if (used + group.back().length() > length) {
                                    break;
                                }
                            } else {
                                group.push_back(plain);
                            }
                        }
                        if (pick(0, 7) == 0) {
                            dynamic = kDynamics[static_cast<std::size_t>(pick(0, 3))];
                        }
                        for (const Duration& duration : group) {
                            Beat beat{.duration = duration};
                            if (!previous.empty() && !drums && pick(0, 4) == 0) {
                                for (const int key : previous) { // tied on
                                    Note note{.pitch =
                                                  song.measureHeader(m).keySignature.spell(key)};
                                    note.effects.tiedFromPrevious = true;
                                    note.effects.dynamic =
                                        beats.back().notes.front().effects.dynamic;
                                    beat.notes.push_back(note);
                                }
                            } else if (pick(0, 5) != 0) {
                                std::vector<int> keys;
                                const int count = pick(1, 3);
                                for (int n = 0; n < count; ++n) {
                                    constexpr std::array kHands{38, 42, 46, 49, 51, 41, 45};
                                    const int key =
                                        drums ? (v == 1
                                                     ? 36
                                                     : kHands[static_cast<std::size_t>(pick(0, 6))])
                                              : pick(low, high);
                                    if (std::ranges::find(keys, key) == keys.end()) {
                                        keys.push_back(key);
                                    }
                                }
                                std::ranges::sort(keys, std::greater{});
                                for (const int key : keys) {
                                    Note note{
                                        .pitch =
                                            drums ? Pitch::fromMidi(key)
                                                  : song.measureHeader(m).keySignature.spell(key)};
                                    note.effects.dynamic = dynamic;
                                    beat.notes.push_back(note);
                                }
                            }
                            previous.clear();
                            for (const Note& note : beat.notes) {
                                previous.push_back(note.pitch.midiNumber());
                            }
                            used += duration.length();
                            beats.push_back(std::move(beat));
                        }
                    }
                    // Ties into an empty measure would join nothing.
                    while (used < length) {
                        beats.push_back(rest(Duration(NoteValue::ThirtySecond)));
                        used += Fraction(1, 32);
                    }
                }
            }
        }
    }
    return song;
}

} // namespace

// ---- Export ------------------------------------------------------------------

TEST_CASE("MIDI export writes a format 1 file byte for byte", "[io][midi]") {
    Song song = pianoSong(1);
    song.track(0).staff(0).measure(0).voices[0].beats = {noteBeat(Duration(NoteValue::Quarter), 60),
                                                         rest(Duration(NoteValue::Half, 1))};

    // Written from the Standard MIDI File 1.0 specification.
    const std::string expected =
        bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0x03, 0xC0}) + // 960 per quarter
        bytes({'M', 'T', 'r', 'k', 0, 0, 0, 0x1A}) +
        bytes({0, 0xFF, 0x58, 4, 4, 2, 24, 8}) +      // 4/4, 24 clocks per click
        bytes({0, 0xFF, 0x59, 2, 0, 0}) +             // C major
        bytes({0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20}) + // 500000 us per quarter: 120 bpm
        bytes({0x9E, 0x00, 0xFF, 0x2F, 0}) +          // end of track at 3840 ticks
        bytes({'M', 'T', 'r', 'k', 0, 0, 0, 0x23}) +
        bytes({0, 0xFF, 0x03, 5, 'P', 'i', 'a', 'n', 'o'}) + // track name
        bytes({0, 0xFF, 0x04, 5, 'P', 'i', 'a', 'n', 'o'}) + // instrument name
        bytes({0, 0xC0, 0}) +                                // program 1
        bytes({0, 0x90, 60, 80}) +                           // mf
        bytes({0x87, 0x40, 0x80, 60, 64}) +                  // 960 ticks later
        bytes({0x96, 0x40, 0xFF, 0x2F, 0});
    const auto result = exportMidi(song);
    CHECK(result.data == expected);
    CHECK(result.warnings.empty());
}

TEST_CASE("MIDI export picks a division that keeps every tuplet exact", "[io][midi]") {
    Song song = pianoSong(1);
    const Duration septuplet(NoteValue::Sixteenth, 0, Tuplet(7, 4));
    auto& beats = song.track(0).staff(0).measure(0).voices[0].beats;
    for (int i = 0; i < 7; ++i) {
        beats.push_back(noteBeat(septuplet, 60 + i));
    }
    const auto result = exportMidi(song);
    CHECK(division(result.data) == 6720); // 960 x 7
    CHECK(result.warnings.empty());

    // 7 x 11 x 13 does not fit in 15 bits: the base division, rounded.
    beats.clear();
    beats.push_back(noteBeat(Duration(NoteValue::Quarter, 0, Tuplet(7, 4)), 60));
    beats.push_back(noteBeat(Duration(NoteValue::Quarter, 0, Tuplet(11, 8)), 62));
    beats.push_back(noteBeat(Duration(NoteValue::Quarter, 0, Tuplet(13, 8)), 64));
    const auto rounded = exportMidi(song);
    CHECK(division(rounded.data) == 960);
    CHECK(rounded.warnings ==
          std::vector<std::string>{"times rounded to 960 ticks per quarter note"});
}

TEST_CASE("MIDI export retriggers a key struck again while it sounds", "[io][midi]") {
    Song song = pianoSong(1);
    auto& measure = song.track(0).staff(0).measure(0);
    measure.voices[0].beats = {noteBeat(Duration(NoteValue::Whole), 60)};
    measure.voices[1].beats = {rest(Duration(NoteValue::Half)),
                               noteBeat(Duration(NoteValue::Quarter), 60),
                               rest(Duration(NoteValue::Quarter))};
    const std::string data = exportMidi(song).data;
    // On at 0; off and on again at 1920; off once, at 3840 (when the longer note ends).
    const std::string notes = bytes({0, 0x90, 60, 80}) + bytes({0x8F, 0x00, 0x80, 60, 64}) +
                              bytes({0, 0x90, 60, 80}) + bytes({0x8F, 0x00, 0x80, 60, 64});
    CHECK(data.contains(notes));
}

TEST_CASE("MIDI export lists what MIDI cannot hold", "[io][midi]") {
    Song song;
    song.info().title = "Kept";
    song.info().artist = "Dropped";
    song.setStartTempo(Tempo(60, Duration(NoteValue::Quarter, 1)));
    song.appendMeasures(2);
    const std::size_t guitar = song.addTrack(presetTrack("electric-guitar"));
    auto& staff = song.track(guitar).staff(0);
    Note bent = staff.frettedNote(2, 7);
    bent.effects.bend = elara::core::BendCurve::bend(200);
    Note muted = staff.frettedNote(6, 0);
    muted.effects.palmMute = true;
    staff.measure(1).voices[0].beats = {
        Beat{.duration = Duration(NoteValue::Half), .notes = {bent}},
        Beat{.duration = Duration(NoteValue::Half), .notes = {muted}}};
    song.addTrack(Track(
        "Banjo", Instrument{.name = "Banjo", .kind = InstrumentKind::Fretted, .midiProgram = 105},
        {Staff{}}));
    const auto result = exportMidi(song);
    CHECK(result.warnings ==
          std::vector<std::string>{
              "song info other than the title and copyright dropped",
              "tempos in beat units other than the quarter note written in quarter notes "
              "(measure 1)",
              "Electric Guitar: fret positions dropped (2 times, first in measure 2)",
              "Electric Guitar: bends dropped (measure 2)",
              "Electric Guitar: palm mutes dropped (measure 2)",
              "Banjo: fretted instrument read back as pitched"});
    // The tempo still sounds the same: a dotted quarter at 60 is a quarter at 90.
    const auto imported = importMidi(bytesOf(result.data));
    REQUIRE(imported.has_value());
    CHECK(imported->song.startTempo() == Tempo(90));
}

TEST_CASE("MIDI export reports a tempo change at the very start", "[io][midi]") {
    Song song = pianoSong(1);
    song.setStartTempo(Tempo(100));
    song.measureHeader(0).tempoChanges.push_back(
        TempoChange{.offset = Fraction(0), .tempo = Tempo(70)});
    const auto result = exportMidi(song);
    CHECK(result.warnings ==
          std::vector<std::string>{
              "a tempo change at the start replaces the start tempo (measure 1)"});
    const auto imported = importMidi(bytesOf(result.data));
    REQUIRE(imported.has_value());
    CHECK(imported->song.startTempo() == Tempo(70));
    CHECK(imported->song.measureHeader(0).tempoChanges.empty());
}

TEST_CASE("MIDI export gives drums channel 10 and skips it for other tracks", "[io][midi]") {
    Song song;
    song.appendMeasures(1);
    for (int t = 0; t < 10; ++t) {
        song.addTrack(presetTrack("flute"));
    }
    song.addTrack(presetTrack("drum-kit"));
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        song.track(t).staff(0).measure(0).voices[0].beats = {
            noteBeat(Duration(NoteValue::Whole), t == 10 ? 38 : 60 + static_cast<int>(t))};
    }
    const std::string data = exportMidi(song).data;
    // The tenth flute plays on channel 11 (status 0x9A); the drums on channel 10.
    CHECK(data.contains(bytes({0x00, 0x9A, 69, 80})));
    CHECK(data.contains(bytes({0x00, 0x99, 38, 80})));
    const auto imported = importMidi(bytesOf(data));
    REQUIRE(imported.has_value());
    REQUIRE(imported->song.trackCount() == 11);
    CHECK(imported->song.track(10).instrument().kind == InstrumentKind::Percussion);
}

TEST_CASE("MIDI files are written atomically", "[io][midi]") {
    const auto folder = std::filesystem::temp_directory_path() / "elara-midi-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    Song song = pianoSong(1);
    song.track(0).staff(0).measure(0).voices[0].beats = {noteBeat(Duration(NoteValue::Whole), 64)};
    const auto path = folder / "song.mid";
    CHECK(elara::io::exportMidiFile(song, path).empty());
    const auto imported = elara::io::importMidiFile(path);
    REQUIRE(imported.has_value());
    CHECK(keysOf(imported->song.track(0).staff(0).measure(0).voices[0]) == std::vector<int>{64});
    CHECK_FALSE(std::filesystem::exists(folder / "song.mid.saving"));
    const auto missing = folder / "no-such-folder" / "song.mid";
    CHECK_THROWS_AS(elara::io::exportMidiFile(song, missing), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(missing));
    std::filesystem::remove_all(folder);
}

// ---- Import ------------------------------------------------------------------

TEST_CASE("MIDI import gives back a song written the way the importer writes", "[io][midi]") {
    Song song;
    song.info().title = "Round trip";
    song.info().copyright = "CC0";
    song.setStartTempo(Tempo(96));
    song.appendMeasures(4);
    song.measureHeader(0).keySignature = KeySignature(1, KeyMode::Major);
    song.measureHeader(1) = song.measureHeader(0).continuation();
    song.measureHeader(1).tempoChanges.push_back({.offset = Fraction(1, 2), .tempo = Tempo(132.5)});
    song.measureHeader(2).timeSignature = TimeSignature(3, 4);
    song.measureHeader(2).keySignature = KeySignature(-5, KeyMode::Minor);
    song.measureHeader(3) = song.measureHeader(2).continuation();
    song.measureHeader(3).timeSignature = TimeSignature(6, 8);
    const std::size_t piano = song.addTrack(presetTrack("piano"));
    const std::size_t guitar = song.addTrack(presetTrack("electric-guitar"));
    const std::size_t drums = song.addTrack(presetTrack("drum-kit"));
    const std::size_t trumpet = song.addTrack(presetTrack("trumpet"));

    const Duration whole(NoteValue::Whole);
    const Duration half(NoteValue::Half);
    const Duration dottedHalf(NoteValue::Half, 1);
    const Duration quarter(NoteValue::Quarter);
    const Duration dottedQuarter(NoteValue::Quarter, 1);
    const Duration eighth(NoteValue::Eighth);
    const Duration dottedEighth(NoteValue::Eighth, 1);
    const Duration sixteenth(NoteValue::Sixteenth);
    const Duration triplet(NoteValue::Eighth, 0, Tuplet(3, 2));
    const Duration quintuplet(NoteValue::Sixteenth, 0, Tuplet(5, 4));
    // Pitches spelled in the key of their measure, as the importer spells them.
    const auto k = [&song](std::size_t m, int midi) {
        return song.measureHeader(m).keySignature.spell(midi);
    };

    auto& treble = song.track(piano).staff(0);
    treble.measure(0).voices[0].beats = {
        chordBeat(quarter, {k(0, 67)}), chordBeat(eighth, {k(0, 69)}),
        chordBeat(eighth, {k(0, 71)}), chordBeat(half, {k(0, 79), k(0, 74)}, false, Dynamic::FF)};
    treble.measure(1).voices[0].beats = {
        chordBeat(triplet, {k(1, 72)}), chordBeat(triplet, {k(1, 74)}),
        chordBeat(triplet, {k(1, 76)}), rest(quarter), chordBeat(half, {k(1, 79)})};
    treble.measure(2).voices[0].beats = {
        chordBeat(quarter, {k(2, 79)}, true), chordBeat(dottedEighth, {k(2, 77)}),
        chordBeat(sixteenth, {k(2, 75)}), chordBeat(quarter, {k(2, 73)})};
    treble.measure(3).voices[0].beats = {
        chordBeat(eighth, {k(3, 70)}), chordBeat(eighth, {k(3, 72)}), chordBeat(eighth, {k(3, 73)}),
        chordBeat(dottedQuarter, {k(3, 75)})};
    auto& bass = song.track(piano).staff(1);
    bass.measure(0).voices[0].beats = {chordBeat(whole, {k(0, 43)})};
    bass.measure(1).voices[0].beats = {chordBeat(half, {k(1, 48)}), chordBeat(half, {k(1, 50)})};
    bass.measure(2).voices[0].beats = {chordBeat(dottedHalf, {k(2, 46)})};
    bass.measure(3).voices[0].beats = {chordBeat(dottedQuarter, {k(3, 41)}),
                                       chordBeat(dottedQuarter, {k(3, 46)})};

    auto& strings = song.track(guitar).staff(0);
    strings.measure(0).voices[0].beats = {
        chordBeat(eighth, {k(0, 64)}), chordBeat(eighth, {k(0, 67)}),
        chordBeat(dottedQuarter, {k(0, 69)}), chordBeat(eighth, {k(0, 71)}), rest(quarter)};
    auto& m1 = strings.measure(1).voices[0].beats;
    for (const int midi : {64, 66, 67, 69, 71}) {
        m1.push_back(chordBeat(quintuplet, {k(1, midi)}));
    }
    m1.push_back(chordBeat(quarter, {k(1, 67)}));
    m1.push_back(chordBeat(half, {k(1, 64), k(1, 59), k(1, 52)}));

    auto& kit = song.track(drums).staff(0).measure(0);
    for (int i = 0; i < 8; ++i) {
        std::vector<Pitch> hit{Pitch::fromMidi(42)};
        if (i == 2 || i == 6) {
            hit.push_back(Pitch::fromMidi(38));
        }
        kit.voices[0].beats.push_back(chordBeat(eighth, hit));
    }
    kit.voices[1].beats = {chordBeat(quarter, {Pitch::fromMidi(36)}), rest(quarter),
                           chordBeat(quarter, {Pitch::fromMidi(36)}), rest(quarter)};

    song.track(trumpet).staff(0).measure(3).voices[0].beats = {
        chordBeat(dottedQuarter, {k(3, 72)}), chordBeat(dottedQuarter, {k(3, 74)})};

    REQUIRE(exportMidi(song).warnings.empty());
    const ImportResult imported = reimport(song);
    CHECK(imported.warnings.empty());
    CHECK(imported.song.info() == song.info());
    CHECK(imported.song.startTempo() == song.startTempo());
    REQUIRE(imported.song.measureCount() == song.measureCount());
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        CHECK(imported.song.measureHeader(m) == song.measureHeader(m));
    }
    REQUIRE(imported.song.trackCount() == song.trackCount());
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        INFO("track " << t);
        CHECK(imported.song.track(t).name() == song.track(t).name());
        CHECK(imported.song.track(t).instrument() == song.track(t).instrument());
        for (std::size_t s = 0; s < song.track(t).staffCount(); ++s) {
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                INFO("staff " << s << ", measure " << m);
                INFO("imported: " << describe(imported.song.track(t).staff(s).measure(m)));
                INFO("expected: " << describe(song.track(t).staff(s).measure(m)));
                CHECK(imported.song.track(t).staff(s).measure(m) ==
                      song.track(t).staff(s).measure(m));
            }
        }
    }
    CHECK(imported.song == song);
}

TEST_CASE("MIDI import quantizes played music to the written rhythm", "[io][midi]") {
    // A flute plays a quarter, two eighths, an eighth-note triplet and a quarter
    // at 480 ticks per quarter, early or late by up to 15 ticks, releasing early.
    const std::vector<std::pair<int, int>> times{
        {0, 410}, {492, 690}, {708, 935}, {962, 1100}, {1125, 1265}, {1271, 1420}, {1450, 1880}};
    std::vector<Event> notes;
    int key = 72;
    for (const auto& [start, end] : times) {
        notes.push_back(on(start, 0, key));
        notes.push_back(off(end, 0, key));
        ++key;
    }
    std::ranges::stable_sort(notes, {}, &Event::tick);
    std::vector<Event> events{program(0, 0, 73)};
    events.insert(events.end(), notes.begin(), notes.end());

    const auto imported = importMidi(bytesOf(smf(0, 480, {events})));
    REQUIRE(imported.has_value());
    const Song& song = imported->song;
    REQUIRE(song.measureCount() == 1);
    REQUIRE(song.trackCount() == 1);
    CHECK(song.track(0).instrument().name == "Flute");
    const auto& voice = song.track(0).staff(0).measure(0).voices[0];
    const Duration triplet(NoteValue::Eighth, 0, Tuplet(3, 2));
    CHECK(durationsOf(voice) == std::vector<Duration>{Duration(NoteValue::Quarter),
                                                      Duration(NoteValue::Eighth),
                                                      Duration(NoteValue::Eighth), triplet, triplet,
                                                      triplet, Duration(NoteValue::Quarter)});
    CHECK(keysOf(voice) == std::vector<int>{72, 73, 74, 75, 76, 77, 78});
    CHECK(imported->warnings.empty());
}

TEST_CASE("MIDI import puts held notes in their own voice", "[io][midi]") {
    // At 96 ticks per quarter: C5 held for a whole note under E5, F5 and G5.
    const std::vector<Event> events{program(0, 0, 73), on(0, 0, 72),    on(0, 0, 76),
                                    off(96, 0, 76),    on(96, 0, 77),   off(192, 0, 77),
                                    on(192, 0, 79),    off(384, 0, 79), off(384, 0, 72)};
    const auto imported = importMidi(bytesOf(smf(0, 96, {events})));
    REQUIRE(imported.has_value());
    const auto& measure = imported->song.track(0).staff(0).measure(0);
    CHECK(keysOf(measure.voices[0]) == std::vector<int>{76, 77, 79});
    CHECK(durationsOf(measure.voices[0]) == std::vector<Duration>{Duration(NoteValue::Quarter),
                                                                  Duration(NoteValue::Quarter),
                                                                  Duration(NoteValue::Half)});
    CHECK(keysOf(measure.voices[1]) == std::vector<int>{72});
    CHECK(durationsOf(measure.voices[1]) == std::vector<Duration>{Duration(NoteValue::Whole)});
    CHECK(imported->warnings.empty());
}

TEST_CASE("MIDI import splits a format 0 file by channel and names tracks by program",
          "[io][midi]") {
    const std::vector<Event> events{metaText(0, 0x03, "Demo"),
                                    program(0, 0, 33),
                                    program(0, 1, 0),
                                    on(0, 9, 36),
                                    on(0, 0, 40),
                                    on(0, 1, 64),
                                    on(0, 1, 48),
                                    off(480, 9, 36),
                                    off(1920, 0, 40),
                                    off(1920, 1, 64),
                                    off(1920, 1, 48)};
    const auto imported = importMidi(bytesOf(smf(0, 480, {events})));
    REQUIRE(imported.has_value());
    const Song& song = imported->song;
    CHECK(song.info().title == "Demo");
    REQUIRE(song.trackCount() == 3);
    // Tracks in the order their channels are first used.
    CHECK(song.track(0).name() == "Electric Bass (finger)");
    CHECK(song.track(0).instrument().kind == InstrumentKind::Fretted);
    CHECK(song.track(0).staff(0).isFretted());
    CHECK(song.track(1).name() == "Acoustic Grand Piano");
    REQUIRE(song.track(1).staffCount() == 2);
    // The piano is split at middle C.
    CHECK(keysOf(song.track(1).staff(0).measure(0).voices[0]) == std::vector<int>{64});
    CHECK(keysOf(song.track(1).staff(1).measure(0).voices[0]) == std::vector<int>{48});
    CHECK(song.track(2).name() == "Drums");
    CHECK(song.track(2).instrument().kind == InstrumentKind::Percussion);
    // A drum hit lasts until the end of its beat.
    CHECK(durationsOf(song.track(2).staff(0).measure(0).voices[1]) ==
          std::vector<Duration>{Duration(NoteValue::Quarter), Duration(NoteValue::Quarter),
                                Duration(NoteValue::Half)});
}

TEST_CASE("MIDI import rejects what is not a Standard MIDI File", "[io][midi]") {
    const auto text = importMidi(bytesOf("hello"));
    REQUIRE_FALSE(text.has_value());
    CHECK(text.error().message.contains("not a Standard MIDI File"));

    const auto smpte =
        importMidi(bytesOf(bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 0, 0xE7, 0x28})));
    REQUIRE_FALSE(smpte.has_value());
    CHECK(smpte.error().message.contains("SMPTE"));

    // A data byte where a status byte must come first.
    const std::string damaged = bytes({'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96}) +
                                bytes({'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0x40, 0x40, 0});
    const auto bad = importMidi(bytesOf(damaged));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message ==
          "MIDI: data byte without a status byte in the event at byte 22 (track 1)");
}

TEST_CASE("MIDI import lists what the model cannot hold", "[io][midi]") {
    const std::vector<Event> events{
        program(0, 0, 0),
        metaText(0, 0x05, "la"),
        {0, bytes({0xB0, 64, 127})}, // sustain pedal down
        {0, bytes({0xE0, 0, 0x50})}, // pitch bend up
        on(0, 0, 60),
        off(480, 0, 60),
        program(480, 0, 40), // a second program on the channel
        on(480, 0, 62),      // never released
    };
    const auto imported = importMidi(bytesOf(smf(1, 480, {events})));
    REQUIRE(imported.has_value());
    CHECK(imported->warnings ==
          std::vector<std::string>{"lyrics dropped", "sustain pedal dropped", "pitch bends dropped",
                                   "program changes dropped",
                                   "notes without a note-off end with their track"});
}

TEST_CASE("MIDI import of random songs keeps every note, and a second pass changes nothing",
          "[io][midi]") {
    for (const std::uint32_t seed : {1U, 2U, 3U, 4U, 20261123U}) {
        INFO("seed " << seed);
        const Song song = randomSong(seed);
        REQUIRE(exportMidi(song).warnings.empty());
        const ImportResult first = reimport(song);
        CHECK(first.warnings.empty());
        CHECK(played(first.song) == played(song));
        CHECK(first.song.measureCount() == song.measureCount());
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            CHECK(first.song.measureHeader(m) == song.measureHeader(m));
        }
        const ImportResult second = reimport(first.song);
        CHECK(second.song == first.song);
        CHECK(exportMidi(second.song).data == exportMidi(first.song).data);
    }
}

TEST_CASE("MIDI import and export give the same result on any number of threads", "[io][midi]") {
    for (const std::uint32_t seed : {5U, 6U}) {
        INFO("seed " << seed);
        const Song song = randomSong(seed);
        const ExportResult exported = exportMidi(song, MidiOptions{.threads = 1});
        const ExportResult exportedInParallel = exportMidi(song, MidiOptions{.threads = 4});
        CHECK(exportedInParallel.data == exported.data);
        CHECK(exportedInParallel.warnings == exported.warnings);
        const auto& data = exported.data;
        const auto serial = importMidi(bytesOf(data), MidiOptions{.threads = 1});
        const auto parallel = importMidi(bytesOf(data), MidiOptions{.threads = 4});
        REQUIRE(serial.has_value());
        REQUIRE(parallel.has_value());
        CHECK(parallel->song == serial->song);
        CHECK(parallel->warnings == serial->warnings);
    }
}

TEST_CASE("MIDI import counts a warning of tracks with the same name once", "[io][midi]") {
    // Two tracks named alike, each with five notes sounding at once.
    const auto crowded = [](int channel) {
        std::vector<Event> events{metaText(0, 0x03, "Lead"), program(0, channel, 0)};
        for (int i = 0; i < 5; ++i) {
            events.push_back(on(0, channel, 60 + 2 * i));
        }
        for (int i = 0; i < 5; ++i) {
            events.push_back(off(480 * (i + 1), channel, 60 + 2 * i));
        }
        return events;
    };
    const auto data = bytesOf(smf(1, 480, {crowded(0), crowded(1)}));
    for (const unsigned threads : {1U, 2U}) {
        INFO("threads " << threads);
        const auto imported = importMidi(data, MidiOptions{.threads = threads});
        REQUIRE(imported.has_value());
        CHECK(imported->warnings ==
              std::vector<std::string>{
                  "Lead: notes overlapping more than four voices shortened (2 times)"});
    }
}
