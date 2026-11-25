// SPDX-License-Identifier: AGPL-3.0-or-later
// MIDI export (ADR-0009): the song's sounding notes as a format 1 Standard MIDI File.
#include <elara/core/performance.hpp>
#include <elara/io/midi.hpp>

#include "archive.hpp"
#include "midi_shared.hpp"
#include "parallel.hpp"
#include "smf.hpp"
#include "warnings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <numeric>
#include <string_view>
#include <tuple>

namespace elara::io {

namespace {

using core::Beat;
using core::Fraction;
using core::Note;
using core::Song;
using core::SoundingNote;
using core::Track;
using detail::MidiEvent;
using detail::MidiTrack;
using detail::Warnings;

constexpr int kBaseDivision = 960; // divisible by 2..6, 8, 10, 12, 15, 16...

// Ticks per quarter note: the base, or a multiple of it that makes every time in
// the song a whole number of ticks while the format allows it.
struct Timing {
    int division = kBaseDivision;
    bool exact = true;

    [[nodiscard]] std::int64_t ticks(const Fraction& wholeNotes) const {
        const Fraction quarters = wholeNotes * Fraction(4 * std::int64_t{division});
        const std::int64_t num = quarters.numerator();
        const std::int64_t den = quarters.denominator();
        // Round half up; times are never negative.
        return (2 * num + den) / (2 * den);
    }
};

Timing chooseTiming(const Song& song, const std::vector<SoundingNote>& notes) {
    std::int64_t needed = kBaseDivision;
    bool exact = true;
    const auto add = [&](const Fraction& wholeNotes) {
        // The denominator in quarter notes, without building the fraction.
        const std::int64_t den = wholeNotes.denominator() / std::gcd(wholeNotes.denominator(), 4);
        if (exact && needed % den != 0) {
            needed = std::lcm(needed, den);
            exact = needed <= detail::kMaxDivision;
        }
    };
    Fraction start;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        add(start);
        for (const core::TempoChange& change : song.measureHeader(m).tempoChanges) {
            add(start + change.offset);
        }
        start += song.measureHeader(m).timeSignature.measureLength();
    }
    for (const SoundingNote& note : notes) {
        // Whole ticks for the start and the length mean whole ticks for the end.
        add(note.start);
        add(note.length);
    }
    return exact ? Timing{.division = static_cast<int>(needed), .exact = true}
                 : Timing{.division = kBaseDivision, .exact = false};
}

MidiEvent meta(std::int64_t tick, std::uint8_t type, std::string payload) {
    return MidiEvent{
        .tick = tick, .status = detail::kMetaEvent, .type = type, .payload = std::move(payload)};
}

MidiEvent channel(std::int64_t tick, int kind, int channelNumber, int first, int second = 0) {
    return MidiEvent{.tick = tick,
                     .status = static_cast<std::uint8_t>(kind | channelNumber),
                     .data = {static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second)}};
}

std::string bytes(std::initializer_list<int> values) {
    std::string out;
    for (const int value : values) {
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(value)));
    }
    return out;
}

// MIDI clocks (24 per quarter note) per metronome click: the dotted beat in
// compound meters (6/8, 9/8, 12/8), the denominator's note otherwise.
int clocksPerClick(const core::TimeSignature& time) {
    const bool compound =
        time.numerator() % 3 == 0 && time.numerator() > 3 && time.denominator() >= 8;
    const int clocks = compound ? 288 / time.denominator() : 96 / time.denominator();
    return std::max(clocks, 1);
}

double quarterBpm(const core::Tempo& tempo) {
    return tempo.bpm() * tempo.beatUnit().length().toDouble() * 4.0;
}

MidiTrack conductorTrack(const Song& song, const Timing& timing, Warnings& warnings) {
    MidiTrack track;
    const core::SongInfo& info = song.info();
    if (!info.title.empty()) {
        track.events.push_back(meta(0, detail::kMetaTrackName, info.title));
    }
    if (!info.copyright.empty()) {
        track.events.push_back(meta(0, detail::kMetaCopyright, info.copyright));
    }
    const bool otherInfo = !info.subtitle.empty() || !info.artist.empty() || !info.album.empty() ||
                           !info.words.empty() || !info.music.empty() ||
                           !info.transcriber.empty() || !info.notes.empty();
    if (otherInfo) {
        warnings.add("", "song info other than the title and copyright dropped");
    }

    const auto tempoEvent = [&](std::int64_t tick, const core::Tempo& tempo, std::size_t m) {
        const double bpm = quarterBpm(tempo);
        const long long micros = std::clamp(detail::microsecondsFromBpm(bpm), 1LL, 0xFFFFFFLL);
        if (tempo.beatUnit() != core::Duration{}) {
            warnings.add(
                "", "tempos in beat units other than the quarter note written in quarter notes", m);
        }
        if (std::abs(detail::bpmFromMicroseconds(micros) - bpm) > 1e-9) {
            warnings.add("", "tempos rounded to whole microseconds per quarter note", m);
        }
        track.events.push_back(
            meta(tick, detail::kMetaTempo,
                 bytes({static_cast<int>((micros >> 16) & 0xFF),
                        static_cast<int>((micros >> 8) & 0xFF), static_cast<int>(micros & 0xFF)})));
    };

    Fraction start;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const core::MeasureHeader& header = song.measureHeader(m);
        const std::int64_t tick = timing.ticks(start);
        const core::MeasureHeader* previous = m == 0 ? nullptr : &song.measureHeader(m - 1);
        if (previous == nullptr || previous->timeSignature != header.timeSignature) {
            const core::TimeSignature& time = header.timeSignature;
            track.events.push_back(
                meta(tick, detail::kMetaTimeSignature,
                     bytes({time.numerator(),
                            std::countr_zero(static_cast<unsigned>(time.denominator())),
                            clocksPerClick(time), 8})));
        }
        if (!header.section.empty()) { // ADR-0062
            track.events.push_back(meta(tick, detail::kMetaMarker, header.section));
        }
        if (previous == nullptr || previous->keySignature != header.keySignature) {
            const core::KeySignature& key = header.keySignature;
            track.events.push_back(
                meta(tick, detail::kMetaKeySignature,
                     bytes({key.fifths(), key.mode() == core::KeyMode::Minor ? 1 : 0})));
        }
        if (m == 0) {
            const bool replaced =
                !header.tempoChanges.empty() && header.tempoChanges.front().offset == Fraction(0);
            if (replaced) {
                // It sounds the same, but reads back as the start tempo.
                warnings.add("", "a tempo change at the start replaces the start tempo", 0);
            } else {
                tempoEvent(0, song.startTempo(), 0);
            }
        }
        for (const core::TempoChange& change : header.tempoChanges) {
            tempoEvent(timing.ticks(start + change.offset), change.tempo, m);
        }
        start += header.timeSignature.measureLength();
    }
    if (song.measureCount() == 0) {
        tempoEvent(0, song.startTempo(), 0);
    }
    track.end = timing.ticks(song.length());
    return track;
}

// Notation MIDI cannot carry, reported once per kind and track.
void reportDropped(const Track& track, Warnings& warnings) {
    const std::string& who = track.name();
    for (std::size_t s = 0; s < track.staffCount(); ++s) {
        const core::Staff& staff = track.staff(s);
        if (staff.capo() > 0) {
            warnings.add(who, "capo dropped", 0);
        }
        for (std::size_t m = 0; m < staff.measureCount(); ++m) {
            for (const core::Voice& voice : staff.measure(m).voices) {
                for (const Beat& beat : voice.beats) {
                    const core::BeatEffects& be = beat.effects;
                    const auto drop = [&](bool present, std::string_view what) {
                        if (present) {
                            warnings.add(who, what, m);
                        }
                    };
                    drop(!beat.graces.empty(), "grace notes dropped");
                    drop(be.hairpin != core::Hairpin::None, "hairpins dropped");
                    if (!beat.notes.empty()) {
                        drop(!be.whammyBar.isEmpty(), "whammy bar dropped");
                        drop(be.pickStroke != core::PickStroke::None, "pick strokes dropped");
                        drop(be.strum != core::Strum::None, "strums dropped");
                        drop(be.tremoloPicking.has_value(), "tremolo picking dropped");
                        drop(be.bassTechnique != core::BassTechnique::None,
                             "bass techniques (tap, slap, pop) dropped");
                    }
                    for (const Note& note : beat.notes) {
                        const core::NoteEffects& e = note.effects;
                        drop(note.position.has_value(), "fret positions dropped");
                        drop(!e.bend.isEmpty(), "bends dropped");
                        drop(e.slideIn != core::SlideIn::None || e.slideOut != core::SlideOut::None,
                             "slides dropped");
                        drop(e.legatoFromPrevious, "hammer-ons and pull-offs dropped");
                        drop(e.vibrato != core::Vibrato::None, "vibrato dropped");
                        drop(e.harmonic.kind != core::HarmonicKind::None, "harmonics dropped");
                        drop(e.palmMute, "palm mutes dropped");
                        drop(e.deadNote, "dead notes dropped");
                        drop(e.ghost, "ghost notes dropped");
                        drop(e.letRing, "let ring dropped");
                        drop(e.staccato, "staccato dropped");
                        drop(e.accent != core::Accent::None, "accents dropped");
                    }
                }
            }
        }
    }
}

std::string_view kindName(core::InstrumentKind kind) {
    switch (kind) {
    case core::InstrumentKind::Fretted:
        return "fretted";
    case core::InstrumentKind::Percussion:
        return "percussion";
    case core::InstrumentKind::Keyboard:
        return "keyboard";
    case core::InstrumentKind::Pitched:
        return "pitched";
    }
    return "pitched";
}

// Note events of one track. A note struck again while the same key still
// sounds on the channel is retriggered; the key is released when the last of
// its notes ends.
void addNotes(MidiTrack& track, int channelNumber, const std::vector<const SoundingNote*>& notes,
              const Timing& timing) {
    struct Edge {
        std::int64_t tick;
        bool on;
        int key;
        int velocity;
    };
    std::vector<Edge> edges;
    edges.reserve(notes.size() * 2);
    for (const SoundingNote* note : notes) {
        const std::int64_t start = timing.ticks(note->start);
        const std::int64_t end = std::max(timing.ticks(note->start + note->length), start + 1);
        edges.push_back({start, true, note->key, note->velocity});
        edges.push_back({end, false, note->key, 0});
    }
    std::ranges::stable_sort(edges, [](const Edge& a, const Edge& b) {
        return std::tie(a.tick, a.on, a.key) < std::tie(b.tick, b.on, b.key);
    });
    std::array<int, 128> sounding{};
    for (const Edge& edge : edges) {
        int& count = sounding[static_cast<std::size_t>(edge.key)];
        if (edge.on) {
            if (count > 0) {
                track.events.push_back(
                    channel(edge.tick, detail::kNoteOff, channelNumber, edge.key, 64));
            }
            track.events.push_back(channel(edge.tick, detail::kNoteOn, channelNumber, edge.key,
                                           std::clamp(edge.velocity, 1, 127)));
            ++count;
        } else if (--count == 0) {
            track.events.push_back(
                channel(edge.tick, detail::kNoteOff, channelNumber, edge.key, 64));
        }
    }
}

} // namespace

ExportResult exportMidi(const Song& song, const MidiOptions& options) {
    const std::vector<SoundingNote> notes = core::soundingNotes(song);
    const Timing timing = chooseTiming(song, notes);
    Warnings warnings;
    if (!timing.exact) {
        warnings.add("",
                     std::format("times rounded to {} ticks per quarter note", timing.division));
    }

    const std::size_t trackCount = song.trackCount();
    std::vector<std::string> bodies(trackCount + 1);
    bodies[0] = detail::encodeTrack(conductorTrack(song, timing, warnings));

    std::vector<std::vector<const SoundingNote*>> byTrack(trackCount);
    for (const SoundingNote& note : notes) {
        byTrack[note.track].push_back(&note);
    }
    constexpr std::array<int, 15> kPitchedChannels{0, 1,  2,  3,  4,  5,  6, 7,
                                                   8, 10, 11, 12, 13, 14, 15};
    std::vector<int> channels(trackCount, detail::kPercussionChannel);
    std::size_t pitchedTracks = 0;
    for (std::size_t t = 0; t < trackCount; ++t) {
        if (song.track(t).instrument().kind != core::InstrumentKind::Percussion) {
            channels[t] = kPitchedChannels[pitchedTracks % kPitchedChannels.size()];
            ++pitchedTracks;
        }
    }

    // Each track only reads the song and its notes: encoded on several threads
    // into its own chunk, warnings merged in track order.
    const std::int64_t end = timing.ticks(song.length());
    std::vector<Warnings> trackWarnings(trackCount);
    const auto write = [&](std::size_t t) {
        const Track& track = song.track(t);
        const core::Instrument& instrument = track.instrument();
        const bool percussion = instrument.kind == core::InstrumentKind::Percussion;
        const int channelNumber = channels[t];
        MidiTrack out;
        if (!track.name().empty()) {
            out.events.push_back(meta(0, detail::kMetaTrackName, track.name()));
        }
        if (!instrument.name.empty()) {
            out.events.push_back(meta(0, detail::kMetaInstrumentName, instrument.name));
        }
        const int program = std::clamp(instrument.midiProgram, 0, 127);
        out.events.push_back(channel(0, detail::kProgramChange, channelNumber, program));
        // The mix as channel volume and pan; players assume the default ones.
        const core::TrackMix& mix = track.mix();
        if (mix.volume != core::TrackMix::kDefaultVolume) {
            out.events.push_back(channel(0, detail::kController, channelNumber, 7,
                                         static_cast<int>(std::lround(mix.volume * 127.0))));
        }
        if (mix.pan != 0.0) {
            out.events.push_back(channel(
                0, detail::kController, channelNumber, 10,
                std::clamp(static_cast<int>(std::lround(64.0 + (mix.pan * 63.0))), 0, 127)));
        }
        if (mix.mute || mix.solo) {
            trackWarnings[t].add(track.name(), "mute and solo dropped");
        }
        if (const core::InstrumentKind back = detail::kindForProgram(program, percussion);
            back != instrument.kind) {
            trackWarnings[t].add(track.name(),
                                 std::format("{} instrument read back as {}",
                                             kindName(instrument.kind), kindName(back)));
        }
        addNotes(out, channelNumber, byTrack[t], timing);
        reportDropped(track, trackWarnings[t]);
        out.end = end;
        bodies[t + 1] = detail::encodeTrack(out);
    };
    constexpr std::size_t kParallelNotes = 20'000; // measured on the benchmark song
    detail::forEachIndex(
        0, trackCount,
        detail::threadsFor(options.threads, notes.size(), kParallelNotes, trackCount), write);
    for (const Warnings& track : trackWarnings) {
        warnings.append(track);
    }
    if (pitchedTracks > kPitchedChannels.size()) {
        warnings.add("", "more than 15 pitched tracks share MIDI channels");
    }
    return ExportResult{.data = detail::writeSmf(1, timing.division, bodies),
                        .warnings = warnings.lines()};
}

std::vector<std::string> exportMidiFile(const Song& song, const std::filesystem::path& path) {
    ExportResult result = exportMidi(song);
    detail::writeFileAtomically(path, result.data);
    return std::move(result.warnings);
}

} // namespace elara::io
