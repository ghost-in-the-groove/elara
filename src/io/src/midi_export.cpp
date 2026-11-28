// SPDX-License-Identifier: AGPL-3.0-or-later
// MIDI export (ADR-0009): the song's sounding notes as a format 1 Standard MIDI File.
#include <elara/core/lyrics.hpp>
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
#include <functional>
#include <limits>
#include <numeric>
#include <string_view>
#include <tuple>
#include <vector>

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

// A gradual tempo change goes in steps this long (ADR-0082).
const Fraction kRampStep(1, 16);

/// A tempo change and where it falls in the song.
struct PlacedChange {
    Fraction at{};
    const core::TempoChange* change = nullptr;
};

std::vector<PlacedChange> placeChanges(const Song& song) {
    std::vector<PlacedChange> placed;
    Fraction start;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const core::MeasureHeader& header = song.measureHeader(m);
        for (const core::TempoChange& change : header.tempoChanges) {
            placed.push_back(PlacedChange{.at = start + change.offset, .change = &change});
        }
        start += header.timeSignature.measureLength();
    }
    return placed;
}

/// Whether the change at `index` ramps to the next one: gradual, and to another speed.
bool ramps(const std::vector<PlacedChange>& changes, std::size_t index) {
    return index + 1 < changes.size() && changes[index].change->gradual &&
           changes[index].change->tempo.wholesPerSecond() !=
               changes[index + 1].change->tempo.wholesPerSecond();
}

/// The measures in the order they are played (ADR-0083): the written measure of
/// each and where it starts in the file, and where each written measure starts.
struct PlayedMeasures {
    std::vector<std::size_t> order{};
    std::vector<Fraction> start{};   // one more than `order`: the end of the file
    std::vector<Fraction> written{}; // one more than the measures: the song's length
    bool straight = true;            // every measure played once, as written
};

PlayedMeasures playedMeasures(const Song& song) {
    PlayedMeasures played;
    played.order = song.playOrder();
    played.written.reserve(song.measureCount() + 1);
    Fraction position;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        played.written.push_back(position);
        position += song.measureHeader(m).timeSignature.measureLength();
    }
    played.written.push_back(position);
    played.straight = played.order.size() == song.measureCount();
    played.start.reserve(played.order.size() + 1);
    position = Fraction();
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        const std::size_t m = played.order[k];
        played.start.push_back(position);
        position += played.written[m + 1] - played.written[m];
        played.straight = played.straight && m == k;
    }
    played.start.push_back(position);
    return played;
}

/// The notes of every played measure, moved to where it is played. A note held
/// past its measure goes on over the measures played after it, as playback
/// holds it.
std::vector<SoundingNote> playedNotes(const std::vector<SoundingNote>& notes,
                                      const PlayedMeasures& played) {
    // The notes of written measure m are notes[byMeasure[firstOf[m] .. firstOf[m + 1]]],
    // in their order (bucketed: an overfull measure has notes past its end).
    const std::size_t count = played.written.size() - 1;
    std::vector<std::size_t> firstOf(count + 2);
    for (const SoundingNote& note : notes) {
        ++firstOf[note.measure + 2];
    }
    for (std::size_t m = 2; m < firstOf.size(); ++m) {
        firstOf[m] += firstOf[m - 1];
    }
    std::vector<std::size_t> byMeasure(notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        byMeasure[firstOf[notes[i].measure + 1]++] = i;
    }
    std::size_t total = 0;
    for (const std::size_t m : played.order) {
        total += firstOf[m + 1] - firstOf[m];
    }
    std::vector<SoundingNote> out;
    out.reserve(total);
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        const std::size_t m = played.order[k];
        const Fraction shift = played.start[k] - played.written[m];
        for (std::size_t j = firstOf[m]; j < firstOf[m + 1]; ++j) {
            SoundingNote note = notes[byMeasure[j]];
            note.start += shift;
            out.push_back(note);
        }
    }
    return out;
}

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

// Whole ticks for the written times and where each measure is played mean
// whole ticks for the played ones, which are their differences and sums.
Timing chooseTiming(const Song& song, const PlayedMeasures& played,
                    const std::vector<SoundingNote>& notes) {
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
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const Fraction& start = played.written[m];
        add(start);
        for (const core::TempoChange& change : song.measureHeader(m).tempoChanges) {
            add(start + change.offset);
            if (change.gradual) {
                add(start + change.offset + kRampStep);
            }
        }
    }
    if (!played.straight) {
        std::ranges::for_each(played.start, add);
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

/// A tempo written at a tick, in microseconds per quarter note.
struct TempoAt {
    std::int64_t tick = 0;
    long long micros = 0;
};

/// What a fermata holds, in ticks of the played song (ADR-0085).
struct HeldTicks {
    std::int64_t start = 0;
    std::int64_t end = 0;
    double stretch = 1.0;
};

/// The tempos `written` (tick-ordered) with the holds (ordered and apart)
/// played slower: at a hold's start the tempo in force times its stretch, any
/// tempo inside it stretched too, and at its end the tempo in force again.
std::vector<TempoAt> holdTempos(const std::vector<TempoAt>& written,
                                const std::vector<HeldTicks>& holds) {
    const auto stretched = [](long long micros, double stretch) {
        return std::clamp(std::llround(static_cast<double>(micros) * stretch), 1LL, 0xFFFFFFLL);
    };
    std::vector<TempoAt> out;
    out.reserve(written.size() + 2 * holds.size());
    long long inForce = detail::microsecondsFromBpm(120.0); // the MIDI default
    std::size_t w = 0;
    // Writes the tempos before `tick`, stretched by `stretch`.
    const auto writeUpTo = [&](std::int64_t tick, double stretch) {
        for (; w < written.size() && written[w].tick < tick; ++w) {
            inForce = written[w].micros;
            out.push_back({written[w].tick, stretched(inForce, stretch)});
        }
    };
    for (std::size_t h = 0; h < holds.size(); ++h) {
        const HeldTicks& hold = holds[h];
        writeUpTo(hold.start, 1.0);
        for (; w < written.size() && written[w].tick == hold.start; ++w) {
            inForce = written[w].micros; // replaced by its stretched tempo
        }
        out.push_back({hold.start, stretched(inForce, hold.stretch)});
        writeUpTo(hold.end, hold.stretch);
        const bool next = h + 1 < holds.size() && holds[h + 1].start == hold.end;
        const bool writtenThere = w < written.size() && written[w].tick == hold.end;
        if (!next && !writtenThere) {
            out.push_back({hold.end, inForce});
        }
    }
    writeUpTo(std::numeric_limits<std::int64_t>::max(), 1.0);
    return out;
}

MidiTrack conductorTrack(const Song& song, const PlayedMeasures& played, const Timing& timing,
                         Warnings& warnings) {
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
    detail::reportLayoutDropped(song, warnings);

    // Returns the microseconds per quarter note written. The tempos go into
    // the track at the end, with what fermatas hold.
    std::vector<TempoAt> tempos;
    const auto tempoMeta = [&](std::int64_t tick, double bpm) {
        const long long micros = std::clamp(detail::microsecondsFromBpm(bpm), 1LL, 0xFFFFFFLL);
        tempos.push_back({tick, micros});
        return micros;
    };
    const auto checkBeatUnit = [&](const core::Tempo& tempo, std::size_t m) {
        if (tempo.beatUnit() != core::Duration{}) {
            warnings.add(
                "", "tempos in beat units other than the quarter note written in quarter notes", m);
        }
    };
    // A tempo is reported where it is written, the first time its measure is played.
    const auto tempoEvent = [&](std::int64_t tick, const core::Tempo& tempo, std::size_t m,
                                bool report) {
        const double bpm = quarterBpm(tempo);
        const long long micros = tempoMeta(tick, bpm);
        if (report) {
            checkBeatUnit(tempo, m);
            if (std::abs(detail::bpmFromMicroseconds(micros) - bpm) > 1e-9) {
                warnings.add("", "tempos rounded to whole microseconds per quarter note", m);
            }
        }
    };

    const std::vector<PlacedChange> changes = placeChanges(song);
    // The changes of measure m are changes[firstChange[m] .. firstChange[m + 1]].
    std::vector<std::size_t> firstChange(song.measureCount() + 1);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        firstChange[m + 1] = firstChange[m] + song.measureHeader(m).tempoChanges.size();
    }

    // The tempo set by the last of the first `count` changes.
    const auto tempoBefore = [&](std::size_t count) -> const core::Tempo& {
        return count == 0 ? song.startTempo() : changes[count - 1].change->tempo;
    };

    // The steps of the ramp from change i over [from, to) of the written song,
    // moved by `shift`: one at `from`, then one every sixteenth from the change.
    // A step lasts as long as its stretch of the ramp, so the song keeps its
    // timing (to the microsecond a quarter note); `to` is at most the end of a
    // measure, so every barline falls where it is written.
    const auto rampEvents = [&](std::size_t i, Fraction from, Fraction to, Fraction shift) {
        const PlacedChange& ramp = changes[i];
        const PlacedChange& next = changes[i + 1];
        const double speed = ramp.change->tempo.wholesPerSecond();
        const double slope =
            (next.change->tempo.wholesPerSecond() - speed) / (next.at - ramp.at).toDouble();
        for (Fraction step = from; step < to;) {
            const Fraction into = step - ramp.at;
            const Fraction sixteenths = into / kRampStep;
            const Fraction stepEnd = std::min(
                to, ramp.at + kRampStep *
                                  Fraction(sixteenths.numerator() / sixteenths.denominator() + 1));
            const double length = (stepEnd - step).toDouble();
            const double seconds =
                core::rampSeconds(speed + slope * into.toDouble(), slope, length);
            tempoMeta(timing.ticks(step + shift), length / seconds * 240.0); // wholes/s -> q/min
            step = stepEnd;
        }
    };

    // Measure by measure as played (ADR-0083). After a jump the tempo in force
    // where the music lands is set again; a measure inside a ramp starts with a
    // step.
    std::vector<bool> reported(song.measureCount());
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        const std::size_t m = played.order[k];
        const core::MeasureHeader& header = song.measureHeader(m);
        const Fraction& start = played.written[m];
        const Fraction& end = played.written[m + 1];
        const Fraction shift = played.start[k] - start;
        const std::int64_t tick = timing.ticks(played.start[k]);
        const core::MeasureHeader* previous =
            k == 0 ? nullptr : &song.measureHeader(played.order[k - 1]);
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

        const bool jumped = k == 0 || played.order[k - 1] + 1 != m;
        const bool report = !reported[m];
        reported[m] = true;
        const std::size_t first = firstChange[m];
        const std::size_t last = firstChange[m + 1];
        const bool changesAtStart = first < last && changes[first].change->offset == Fraction(0);
        if (first > 0 && ramps(changes, first - 1) && changes[first].at > start) {
            // A ramp comes into the measure (and ends at a later change).
            const Fraction& rampEnd = changes[first].at;
            rampEvents(first - 1, start, std::min(end, rampEnd), shift);
        } else if (jumped && !changesAtStart) {
            // Set again unless the measure played before ends at that same speed.
            const std::size_t before = k == 0 ? 0 : firstChange[played.order[k - 1] + 1];
            const bool same =
                k > 0 && !(before > 0 && ramps(changes, before - 1)) &&
                tempoBefore(before).wholesPerSecond() == tempoBefore(first).wholesPerSecond();
            if (!same) {
                tempoEvent(tick, tempoBefore(first), m, k == 0);
            }
        } else if (k == 0) {
            // It sounds the same, but reads back as the start tempo.
            warnings.add("", "a tempo change at the start replaces the start tempo", m);
        }
        for (std::size_t i = first; i < last; ++i) {
            const PlacedChange& change = changes[i];
            if (ramps(changes, i)) {
                if (report) {
                    checkBeatUnit(change.change->tempo, m);
                }
                const Fraction& rampEnd = changes[i + 1].at;
                rampEvents(i, change.at, std::min(end, rampEnd), shift);
            } else {
                tempoEvent(timing.ticks(change.at + shift), change.change->tempo, m, report);
            }
        }
    }
    if (played.order.empty()) {
        tempoEvent(0, song.startTempo(), 0, true);
    }

    // What fermatas hold plays slower (ADR-0085), on every pass.
    std::vector<HeldTicks> holds;
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        for (const core::FermataHold& hold : song.fermataHolds(played.order[k])) {
            const Fraction start = played.start[k] + hold.start;
            const HeldTicks held{.start = timing.ticks(start),
                                 .end = timing.ticks(played.start[k] + hold.end),
                                 .stretch = hold.stretch};
            if (held.end > held.start) {
                holds.push_back(held);
            }
        }
    }
    std::ranges::stable_sort(tempos, std::less<>{}, &TempoAt::tick);
    for (const TempoAt& tempo : holdTempos(tempos, holds)) {
        const long long micros = tempo.micros;
        track.events.push_back(
            meta(tempo.tick, detail::kMetaTempo,
                 bytes({static_cast<int>((micros >> 16) & 0xFF),
                        static_cast<int>((micros >> 8) & 0xFF), static_cast<int>(micros & 0xFF)})));
    }
    std::ranges::stable_sort(track.events, std::less<>{}, &MidiEvent::tick);
    track.end = timing.ticks(played.start.back());
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
                    if (!beat.notes.empty()) {
                        drop(!be.whammyBar.isEmpty(), "whammy bar dropped");
                        drop(be.pickStroke != core::PickStroke::None, "pick strokes dropped");
                        drop(be.strum != core::Strum::None, "strums dropped");
                        drop(be.tremoloPicking.has_value(), "tremolo picking dropped");
                        drop(be.bassTechnique != core::BassTechnique::None,
                             "bass techniques (tap, slap, pop) dropped");
                    }
                    // Grace notes sound (ADR-0079), with what MIDI drops of any note.
                    const auto dropFrom = [&](const Note& note) {
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
                        drop(e.staccatissimo, "staccatissimo dropped");
                        drop(e.tenuto, "tenuto dropped");
                        drop(e.trill.isTrill(), "trills dropped");
                        drop(e.ornament != core::Ornament::None, "mordents and turns dropped");
                        drop(e.accent != core::Accent::None, "accents dropped");
                    };
                    std::ranges::for_each(beat.notes, dropFrom);
                    for (const core::Grace& grace : beat.graces) {
                        std::ranges::for_each(grace.notes, dropFrom);
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

// The lyrics of the track's first staff that sings, a Lyric meta event per
// syllable at its beat, with a hyphen after it when the word goes on and a
// space when it ends (ADR-0084). MIDI has one line of lyrics: each pass through
// a repeated measure sings the verse of that pass (the first verse on the
// first pass) when the measure has it, else the first verse.
std::vector<MidiEvent> lyricEvents(const Song& song, const Track& track,
                                   const PlayedMeasures& played, const Timing& timing,
                                   Warnings& warnings) {
    const core::Staff* sung = nullptr;
    for (std::size_t s = 0; s < track.staffCount(); ++s) {
        if (core::verseCount(track.staff(s)) == 0) {
            continue;
        }
        if (sung == nullptr) {
            sung = &track.staff(s);
        } else {
            warnings.add(track.name(), "lyrics of a second staff dropped");
            break;
        }
    }
    if (sung == nullptr) {
        return {};
    }
    // The verses each measure sings anywhere, and those its passes sang.
    std::vector<std::uint32_t> verses(sung->measureCount());
    for (std::size_t m = 0; m < sung->measureCount(); ++m) {
        for (const core::Voice& voice : sung->measure(m).voices) {
            for (const Beat& beat : voice.beats) {
                if (!beat.lyrics) {
                    continue;
                }
                for (std::size_t v = 0; v < beat.lyrics->size() && v < core::kMaxVerses; ++v) {
                    if (!(*beat.lyrics)[v].text.empty()) {
                        verses[m] |= 1U << v;
                    }
                }
            }
        }
    }
    std::vector<std::uint32_t> sang(sung->measureCount());
    std::vector<std::size_t> passes(sung->measureCount());
    std::vector<MidiEvent> events;
    bool extenders = false;
    struct Placed {
        Fraction start{};
        const core::Syllable* syllable = nullptr;
    };
    std::vector<Placed> placed;
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        const std::size_t m = played.order[k];
        const std::size_t pass = passes[m]++;
        const std::size_t verse =
            pass < core::kMaxVerses && (verses[m] >> pass & 1U) != 0 ? pass : 0;
        if ((verses[m] >> verse & 1U) == 0) {
            continue;
        }
        sang[m] |= 1U << verse;
        // The first syllable at each place, voices in order.
        placed.clear();
        for (const core::Voice& voice : sung->measure(m).voices) {
            Fraction start;
            for (const Beat& beat : voice.beats) {
                if (beat.lyrics && verse < beat.lyrics->size() &&
                    !(*beat.lyrics)[verse].text.empty() &&
                    std::ranges::find(placed, start, &Placed::start) == placed.end()) {
                    placed.push_back({.start = start, .syllable = &(*beat.lyrics)[verse]});
                }
                start += beat.duration.length();
            }
        }
        std::ranges::sort(placed, {}, &Placed::start);
        const core::MeasureHeader& header = song.measureHeader(m);
        for (const Placed& at : placed) {
            const core::Syllable& syllable = *at.syllable;
            extenders = extenders || syllable.extend;
            // Sung with the notes, as the triplet feel plays them (ADR-0104).
            const Fraction start =
                core::swung(header.tripletFeel, at.start, header.timeSignature.measureLength());
            events.push_back(meta(timing.ticks(played.start[k] + start), detail::kMetaLyric,
                                  syllable.text + (syllable.hyphen ? '-' : ' ')));
        }
    }
    for (std::size_t m = 0; m < verses.size(); ++m) {
        if ((verses[m] & ~sang[m]) != 0) {
            warnings.add(track.name(), "verses no repeat sings dropped", m);
        }
    }
    if (extenders) {
        warnings.add(track.name(), "lyric extenders dropped");
    }
    return events;
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

// The sustain pedal of track t as controller 64 (ADR-0092): 127 where it goes
// down, 0 where it comes up; a change lifts it and presses it again at once.
// A pedalling goes on into the next played measure only where that is the
// next written one.
void addPedal(MidiTrack& track, int channelNumber, const Song& song, std::size_t t,
              const PlayedMeasures& played, const Timing& timing) {
    struct Pedalling {
        Fraction start;
        Fraction end;
    };
    std::vector<Pedalling> spans;
    for (std::size_t k = 0; k < played.order.size(); ++k) {
        const std::size_t m = played.order[k];
        const bool follows = k > 0 && played.order[k - 1] + 1 == m;
        for (const core::PedalSpan& span : song.pedalSpans(t, m)) {
            const Pedalling pedalling{.start = played.start[k] + span.start,
                                      .end = played.start[k] + span.end};
            if (!span.change && !spans.empty() && spans.back().end == pedalling.start &&
                (follows || !span.start.isZero())) {
                spans.back().end = pedalling.end;
            } else {
                spans.push_back(pedalling);
            }
        }
    }
    constexpr int kSustain = 64;
    for (const Pedalling& span : spans) {
        track.events.push_back(
            channel(timing.ticks(span.start), detail::kController, channelNumber, kSustain, 127));
        track.events.push_back(
            channel(timing.ticks(span.end), detail::kController, channelNumber, kSustain, 0));
    }
}

} // namespace

ExportResult exportMidi(const Song& song, const MidiOptions& options) {
    // As played: with the triplet feel of their measures (ADR-0104).
    std::vector<SoundingNote> written = core::soundingNotes(song);
    core::applyTripletFeel(song, written);
    const PlayedMeasures played = playedMeasures(song);
    const std::vector<SoundingNote> moved =
        played.straight ? std::vector<SoundingNote>{} : playedNotes(written, played);
    const std::vector<SoundingNote>& notes = played.straight ? written : moved;
    const Timing timing = chooseTiming(song, played, written);
    Warnings warnings;
    if (!played.straight) {
        warnings.add("", "repeats and jumps written as played");
    }
    if (!timing.exact) {
        warnings.add("",
                     std::format("times rounded to {} ticks per quarter note", timing.division));
    }

    const std::size_t trackCount = song.trackCount();
    std::vector<std::string> bodies(trackCount + 1);
    bodies[0] = detail::encodeTrack(conductorTrack(song, played, timing, warnings));

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
    const std::int64_t end = timing.ticks(played.start.back());
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
        // Lyric events go before the notes they start with.
        const std::vector<MidiEvent> lyrics =
            lyricEvents(song, track, played, timing, trackWarnings[t]);
        const auto header = static_cast<std::ptrdiff_t>(out.events.size());
        out.events.insert(out.events.end(), lyrics.begin(), lyrics.end());
        const auto notesFrom = static_cast<std::ptrdiff_t>(out.events.size());
        addNotes(out, channelNumber, byTrack[t], timing);
        const auto byTick = [](const MidiEvent& a, const MidiEvent& b) {
            return a.tick < b.tick;
        };
        std::inplace_merge(out.events.begin() + header, out.events.begin() + notesFrom,
                           out.events.end(), byTick);
        // The pedal after the notes struck with it.
        const auto pedalFrom = static_cast<std::ptrdiff_t>(out.events.size());
        addPedal(out, channelNumber, song, t, played, timing);
        std::inplace_merge(out.events.begin() + header, out.events.begin() + pedalFrom,
                           out.events.end(), byTick);
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
