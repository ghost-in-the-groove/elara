// SPDX-License-Identifier: AGPL-3.0-or-later
// MusicXML export (ADR-0008), written from the MusicXML 4.0 specification. It is
// the reverse of the import mapping: what MusicXML can hold imports back
// unchanged, the rest is listed in the warnings.
#include <elara/core/chord_name.hpp>
#include <elara/core/version.hpp>
#include <elara/io/musicxml.hpp>

#include "archive.hpp"
#include "musicxml_shared.hpp"
#include "parallel.hpp"
#include "warnings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::io {

namespace {

using namespace core;
using detail::kTypeNames;
using detail::Warnings;

// ---- XML text -------------------------------------------------------------------

/// The MusicXML <kind> for a chord name's suffix; "other" when none says it.
std::string_view kindFor(std::string_view suffix) {
    if (const auto known =
            std::ranges::find(detail::kChordKinds, suffix, &detail::ChordKind::suffix);
        known != detail::kChordKinds.end()) {
        return known->kind;
    }
    static constexpr std::array<detail::ChordKind, 10> kAliases{{
        {"minor", "min"},
        {"minor", "-"},
        {"major", "maj"},
        {"major-seventh", "M7"},
        {"augmented", "aug"},
        {"diminished", "o"},
        {"diminished-seventh", "o7"},
        {"half-diminished", "m7(b5)"},
        {"suspended-fourth", "sus"},
        {"minor-seventh", "min7"},
    }};
    if (const auto alias = std::ranges::find(kAliases, suffix, &detail::ChordKind::suffix);
        alias != kAliases.end()) {
        return alias->kind;
    }
    return "other";
}

/// Appends indented XML to a string. The caller writes elements in the order
/// the MusicXML schema requires; the writer only handles the syntax.
class XmlWriter {
public:
    /// `pretty`: one element per line, indented by depth; otherwise no
    /// whitespace at all (for archives, which nobody reads by eye).
    XmlWriter(std::string& out, int depth, bool pretty)
        : out_(out), depth_(depth), pretty_(pretty) {}

    /// <name>, then children until close(name).
    void open(std::string_view name) {
        start(name);
        endOpen();
    }

    /// "<name", then attributes; finish with endOpen(), endEmpty() or endText().
    void start(std::string_view name) {
        newLine();
        out_ += '<';
        out_ += name;
    }

    void attribute(std::string_view name, std::string_view value) {
        out_ += ' ';
        out_ += name;
        out_ += "=\"";
        escape(value);
        out_ += '"';
    }

    void attribute(std::string_view name, std::int64_t value) {
        out_ += ' ';
        out_ += name;
        out_ += "=\"";
        appendNumber(value);
        out_ += '"';
    }

    void endOpen() {
        out_ += '>';
        ++depth_;
    }

    void endEmpty() { out_ += "/>"; }

    void endText(std::string_view name, std::string_view text) {
        out_ += '>';
        escape(text);
        closeInline(name);
    }

    void endNumber(std::string_view name, std::int64_t value) {
        out_ += '>';
        appendNumber(value);
        closeInline(name);
    }

    void close(std::string_view name) {
        --depth_;
        newLine();
        out_ += "</";
        out_ += name;
        out_ += '>';
    }

    /// <name/>
    void empty(std::string_view name) {
        start(name);
        endEmpty();
    }

    /// <name>text</name>
    void text(std::string_view name, std::string_view value) {
        start(name);
        endText(name, value);
    }

    /// <name>number</name>
    void number(std::string_view name, std::int64_t value) {
        start(name);
        endNumber(name, value);
    }

private:
    void newLine() {
        if (!pretty_) {
            return;
        }
        out_ += '\n';
        out_.append(static_cast<std::size_t>(2 * depth_), ' ');
    }

    void closeInline(std::string_view name) {
        out_ += "</";
        out_ += name;
        out_ += '>';
    }

    void appendNumber(std::int64_t value) {
        std::array<char, 24> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        out_.append(buffer.data(), result.ptr);
    }

    void escape(std::string_view text) {
        while (!text.empty()) {
            const auto special = std::ranges::find_if(text, [](char c) {
                return c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' ||
                       (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' &&
                        c != '\r');
            });
            const auto plain = static_cast<std::size_t>(special - text.begin());
            out_.append(text.substr(0, plain));
            if (plain == text.size()) {
                return;
            }
            switch (text[plain]) {
            case '&':
                out_ += "&amp;";
                break;
            case '<':
                out_ += "&lt;";
                break;
            case '>':
                out_ += "&gt;";
                break;
            case '"':
                out_ += "&quot;";
                break;
            case '\'':
                out_ += "&apos;";
                break;
            default:
                break; // control characters are not allowed in XML 1.0
            }
            text.remove_prefix(plain + 1);
        }
    }

    std::string& out_;
    int depth_;
    bool pretty_;
};

/// Shortest text that reads back as exactly `value`.
std::string decimal(double value) {
    return std::format("{}", value);
}

/// `value` with at most four decimals, so 0.7 * 100 is written as 70.
double fourDecimals(double value) {
    return std::round(value * 1e4) / 1e4;
}

// ---- Vocabulary -----------------------------------------------------------------

constexpr std::array<std::string_view, 7> kStepNames{"C", "D", "E", "F", "G", "A", "B"};
constexpr std::array<std::string_view, 8> kDynamicNames{"ppp", "pp", "p",  "mp",
                                                        "mf",  "f",  "ff", "fff"};
constexpr std::array<std::string_view, 4> kKindNames{"fretted", "percussion", "keyboard",
                                                     "pitched"};

/// Where a drum is drawn on a five-line staff (docs/instruments.md, treble-clef
/// positions) and its notehead.
struct DrumDisplay {
    int key = 0;
    std::string_view step{};
    int octave = 5;
    std::string_view notehead{};
};

constexpr std::array kDrumMap{
    DrumDisplay{35, "F", 4, "normal"},   DrumDisplay{36, "F", 4, "normal"},
    DrumDisplay{37, "C", 5, "circle-x"}, DrumDisplay{38, "C", 5, "normal"},
    DrumDisplay{40, "C", 5, "normal"},   DrumDisplay{41, "A", 4, "normal"},
    DrumDisplay{42, "G", 5, "x"},        DrumDisplay{43, "A", 4, "normal"},
    DrumDisplay{44, "D", 4, "x"},        DrumDisplay{45, "D", 5, "normal"},
    DrumDisplay{46, "G", 5, "x"},        DrumDisplay{47, "D", 5, "normal"},
    DrumDisplay{48, "E", 5, "normal"},   DrumDisplay{49, "A", 5, "x"},
    DrumDisplay{50, "E", 5, "normal"},   DrumDisplay{51, "F", 5, "x"},
    DrumDisplay{53, "F", 5, "diamond"},  DrumDisplay{57, "A", 5, "x"},
    DrumDisplay{59, "F", 5, "x"}};

DrumDisplay drumDisplay(int key) {
    const auto found = std::ranges::find(kDrumMap, key, &DrumDisplay::key);
    return found != kDrumMap.end() ? *found : DrumDisplay{key, "C", 5, "normal"};
}

/// General MIDI percussion names, keys 35 to 81.
constexpr std::array<std::string_view, 47> kDrumNames{"Acoustic Bass Drum",
                                                      "Bass Drum 1",
                                                      "Side Stick",
                                                      "Acoustic Snare",
                                                      "Hand Clap",
                                                      "Electric Snare",
                                                      "Low Floor Tom",
                                                      "Closed Hi-Hat",
                                                      "High Floor Tom",
                                                      "Pedal Hi-Hat",
                                                      "Low Tom",
                                                      "Open Hi-Hat",
                                                      "Low-Mid Tom",
                                                      "Hi-Mid Tom",
                                                      "Crash Cymbal 1",
                                                      "High Tom",
                                                      "Ride Cymbal 1",
                                                      "Chinese Cymbal",
                                                      "Ride Bell",
                                                      "Tambourine",
                                                      "Splash Cymbal",
                                                      "Cowbell",
                                                      "Crash Cymbal 2",
                                                      "Vibraslap",
                                                      "Ride Cymbal 2",
                                                      "Hi Bongo",
                                                      "Low Bongo",
                                                      "Mute Hi Conga",
                                                      "Open Hi Conga",
                                                      "Low Conga",
                                                      "High Timbale",
                                                      "Low Timbale",
                                                      "High Agogo",
                                                      "Low Agogo",
                                                      "Cabasa",
                                                      "Maracas",
                                                      "Short Whistle",
                                                      "Long Whistle",
                                                      "Short Guiro",
                                                      "Long Guiro",
                                                      "Claves",
                                                      "Hi Wood Block",
                                                      "Low Wood Block",
                                                      "Mute Cuica",
                                                      "Open Cuica",
                                                      "Mute Triangle",
                                                      "Open Triangle"};

std::string drumName(int key) {
    if (key >= 35 && key <= 81) {
        return std::string(kDrumNames[static_cast<std::size_t>(key - 35)]);
    }
    return std::format("Percussion {}", key);
}

int floorDiv(int a, int b) {
    const int quotient = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? quotient - 1 : quotient;
}

/// The note of `beat` that `note` (at `index` in its own beat) continues: the
/// one on the same string, else the one at the same index, else the first.
std::optional<std::size_t> partnerIn(const Beat& beat, const Note& note, std::size_t index) {
    if (beat.notes.empty()) {
        return std::nullopt;
    }
    if (note.position) {
        for (std::size_t k = 0; k < beat.notes.size(); ++k) {
            const auto& position = beat.notes[k].position;
            if (position && position->string == note.position->string) {
                return k;
            }
        }
    }
    return index < beat.notes.size() ? index : 0;
}

const Beat* beatBefore(const Staff& staff, std::size_t measure, std::size_t voice,
                       std::size_t beat) {
    if (beat > 0) {
        return &staff.measure(measure).voices[voice].beats[beat - 1];
    }
    if (measure == 0) {
        return nullptr;
    }
    const auto& beats = staff.measure(measure - 1).voices[voice].beats;
    return beats.empty() ? nullptr : &beats.back();
}

const Beat* beatAfter(const Staff& staff, std::size_t measure, std::size_t voice,
                      std::size_t beat) {
    const auto& beats = staff.measure(measure).voices[voice].beats;
    if (beat + 1 < beats.size()) {
        return &beats[beat + 1];
    }
    if (measure + 1 >= staff.measureCount()) {
        return nullptr;
    }
    const auto& next = staff.measure(measure + 1).voices[voice].beats;
    return next.empty() ? nullptr : &next.front();
}

// ---- Divisions --------------------------------------------------------------------

/// Divisions per quarter note that make every length of the song a whole number
/// of divisions. A song whose tuplets need more than kMaxDivisions is rounded.
class Grid {
public:
    static constexpr std::int64_t kMaxDivisions = std::int64_t{1} << 20;

    void add(const Fraction& wholes) {
        const std::int64_t need = (wholes * Fraction(4)).denominator();
        if (divisions_ % need == 0) {
            return;
        }
        const std::int64_t factor = need / std::gcd(divisions_, need);
        if (divisions_ > kMaxDivisions / factor) {
            exact_ = false;
            return;
        }
        divisions_ *= factor;
    }

    [[nodiscard]] std::int64_t divisions() const noexcept { return divisions_; }
    [[nodiscard]] bool exact() const noexcept { return exact_; }

    [[nodiscard]] std::int64_t toDivisions(const Fraction& wholes) const {
        const Fraction value = wholes * Fraction(4 * divisions_);
        if (value.denominator() == 1) {
            return value.numerator();
        }
        return static_cast<std::int64_t>(std::llround(value.toDouble()));
    }

private:
    std::int64_t divisions_ = 1;
    bool exact_ = true;
};

/// The song's grid, and how much each track has to write (beats plus notes),
/// to size buffers and decide on threads.
struct SongScan {
    Grid grid{};
    std::vector<std::size_t> work{};
};

SongScan scan(const Song& song) {
    SongScan result;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const MeasureHeader& header = song.measureHeader(m);
        result.grid.add(header.timeSignature.measureLength());
        for (const TempoChange& change : header.tempoChanges) {
            result.grid.add(change.offset);
        }
    }
    result.work.resize(song.trackCount());
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const Track& track = song.track(t);
        std::size_t work = 0;
        std::optional<Duration> last;
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            const Staff& staff = track.staff(s);
            for (std::size_t m = 0; m < staff.measureCount(); ++m) {
                for (const Voice& voice : staff.measure(m).voices) {
                    for (const Beat& beat : voice.beats) {
                        work += 1 + beat.notes.size();
                        if (beat.duration != last) {
                            result.grid.add(beat.duration.length());
                            last = beat.duration;
                        }
                    }
                }
            }
        }
        result.work[t] = work;
    }
    return result;
}

// ---- Parts ------------------------------------------------------------------------

/// written + semitones() = sounding, as in MusicXML's <transpose>.
struct Transposition {
    int chromatic = 0;
    int diatonic = 0;
    int octaveChange = 0;
    [[nodiscard]] int semitones() const noexcept { return chromatic + 12 * octaveChange; }
    [[nodiscard]] int steps() const noexcept { return diatonic + 7 * octaveChange; }
};

struct Spelled {
    int step = 0;
    int alter = 0;
    int octave = 4;
};

struct DynamicMark {
    Fraction onset{};
    Dynamic dynamic = Dynamic::MF;
    bool written = false;
};

struct TempoMark {
    Fraction offset{};
    Tempo tempo{};
    bool written = false;
};

struct Onset {
    Fraction onset{};
    Dynamic dynamic = Dynamic::MF;
};

/// Where a beat is written: its neighbours in the voice (across measures), the
/// MusicXML voice and staff numbers and its tuplet bracket marks.
struct BeatPlace {
    const Beat* beat = nullptr;
    const Beat* previous = nullptr;
    const Beat* next = nullptr;
    int voice = 1;
    int staff = 1;
    std::size_t measure = 0;
    std::uint8_t tuplet = 0; // bit 0: bracket starts, bit 1: bracket stops
    std::uint8_t grace = 0;  // a grace note (ADR-0079): 1 before the beat, 2 on it
};

constexpr std::uint8_t kGraceBefore = 1;
constexpr std::uint8_t kGraceOn = 2;

constexpr std::uint8_t kTupletStart = 1;
constexpr std::uint8_t kTupletStop = 2;

/// Writes one track as a <part>, mirroring how the importer reads it back.
class PartWriter {
public:
    PartWriter(const Song& song, std::size_t index, const Grid& grid, int channel)
        : song_(song), track_(song.track(index)), index_(index), id_(std::format("P{}", index + 1)),
          grid_(grid), channel_(channel),
          percussion_(track_.instrument().kind == InstrumentKind::Percussion),
          tabCopy_(!percussion_ && track_.staffCount() == 1 && track_.staff(0).isFretted()),
          staffCount_(static_cast<int>(track_.staffCount())),
          xmlStaves_(staffCount_ + (tabCopy_ ? 1 : 0)),
          dynamics_(track_.staffCount(), Dynamic::MF) {
        transposition_ = transpositionFor();
        if (percussion_) {
            collectDrumKeys();
            if (track_.instrument().name != track_.name()) {
                warnings_.add(track_.name(), "drum kit names other than the track name dropped", 0);
            }
        }
        if (track_.mix().mute || track_.mix().solo) {
            warnings_.add(track_.name(), "mute and solo dropped");
        }
        const InstrumentKind readBack = kindReadBack();
        if (readBack != track_.instrument().kind && song_.measureCount() > 0) {
            warnings_.add(
                track_.name(),
                std::format("{} instrument read back as {}",
                            kKindNames[static_cast<std::size_t>(track_.instrument().kind)],
                            kKindNames[static_cast<std::size_t>(readBack)]),
                0);
        }
    }

    void writeScorePart(XmlWriter& xml) const {
        xml.start("score-part");
        xml.attribute("id", id_);
        xml.endOpen();
        xml.text("part-name", track_.name());
        const auto instrument = [&](int number, std::string_view name) {
            xml.start("score-instrument");
            xml.attribute("id", std::format("{}-I{}", id_, number));
            xml.endOpen();
            xml.text("instrument-name", name);
            xml.close("score-instrument");
        };
        const auto midi = [&](int number, int channel, int program, std::optional<int> key) {
            xml.start("midi-instrument");
            xml.attribute("id", std::format("{}-I{}", id_, number));
            xml.endOpen();
            xml.number("midi-channel", channel);
            xml.number("midi-program", program + 1);
            if (key) {
                xml.number("midi-unpitched", *key + 1);
            }
            // The mix: the volume as a percentage, the pan in degrees from -90
            // (hard left) to 90 (hard right). Every drum of a kit carries it.
            const TrackMix& mix = track_.mix();
            if (mix.volume != TrackMix::kDefaultVolume) {
                xml.text("volume", decimal(fourDecimals(mix.volume * 100.0)));
            }
            if (mix.pan != 0.0) {
                xml.text("pan", decimal(fourDecimals(mix.pan * 90.0)));
            }
            xml.close("midi-instrument");
        };
        const int program = std::clamp(track_.instrument().midiProgram, 0, 127);
        if (percussion_ && !drumKeys_.empty()) {
            for (const int key : drumKeys_) {
                instrument(key + 1, drumName(key));
            }
            for (const int key : drumKeys_) {
                midi(key + 1, 10, 0, key);
            }
        } else {
            instrument(1, track_.instrument().name);
            midi(1, percussion_ ? 10 : channel_, percussion_ ? 0 : program, std::nullopt);
        }
        xml.close("score-part");
    }

    void writePart(XmlWriter& xml) {
        xml.start("part");
        xml.attribute("id", id_);
        xml.endOpen();
        for (std::size_t m = 0; m < song_.measureCount(); ++m) {
            writeMeasure(xml, m);
        }
        xml.close("part");
    }

    [[nodiscard]] const Warnings& warnings() const noexcept { return warnings_; }

private:
    // Notes of the TAB copy repeat the standard staff: report them once.
    void warn(std::string_view what, std::size_t measure) {
        if (!tabPass_) {
            warnings_.add(track_.name(), what, measure);
        }
    }

    [[nodiscard]] Transposition transpositionFor() {
        const Instrument& instrument = track_.instrument();
        if (percussion_) {
            if (instrument.writtenOffset != 0) {
                warnings_.add(track_.name(), "written offsets of drums dropped", 0);
            }
            return {};
        }
        int offset = instrument.writtenOffset;
        const Clef clef = track_.staff(0).clef();
        if (clef == Clef::Treble8vb || clef == Clef::Bass8vb) {
            offset -= 12; // the octave clef holds that octave (ADR-0008)
        }
        constexpr int kMaxSemitones = 59; // what <octave-change> -4..4 can carry
        int semitones = -offset;
        if (std::abs(semitones) > kMaxSemitones) {
            warnings_.add(track_.name(), "written offsets beyond 59 semitones clamped", 0);
            semitones = std::clamp(semitones, -kMaxSemitones, kMaxSemitones);
        }
        Transposition result;
        result.octaveChange = semitones / 12;
        result.chromatic = semitones % 12;
        result.diatonic = static_cast<int>(std::lround(result.chromatic * 7.0 / 12.0));
        return result;
    }

    void collectDrumKeys() {
        std::array<bool, 128> used{};
        for (std::size_t s = 0; s < track_.staffCount(); ++s) {
            const Staff& staff = track_.staff(s);
            for (std::size_t m = 0; m < staff.measureCount(); ++m) {
                for (const Voice& voice : staff.measure(m).voices) {
                    for (const Beat& beat : voice.beats) {
                        for (const Note& note : beat.notes) {
                            used[static_cast<std::size_t>(note.pitch.midiNumber())] = true;
                        }
                    }
                }
            }
        }
        for (int key = 0; key < 128; ++key) {
            if (used[static_cast<std::size_t>(key)]) {
                drumKeys_.push_back(key);
            }
        }
    }

    /// The instrument kind the importer derives from what is written.
    [[nodiscard]] InstrumentKind kindReadBack() const {
        bool percussionClef = false;
        bool fretted = false;
        for (std::size_t s = 0; s < track_.staffCount(); ++s) {
            percussionClef = percussionClef || track_.staff(s).clef() == Clef::Percussion;
            fretted = fretted || track_.staff(s).isFretted();
        }
        if ((percussion_ && !drumKeys_.empty()) || percussionClef) {
            return InstrumentKind::Percussion;
        }
        if (fretted && !percussion_) {
            return InstrumentKind::Fretted;
        }
        return track_.staffCount() >= 2 ? InstrumentKind::Keyboard : InstrumentKind::Pitched;
    }

    /// The written key that the importer turns back into `concert`.
    [[nodiscard]] int writtenFifths(const KeySignature& concert, std::size_t measure) {
        const int chromatic = transposition_.chromatic;
        const int diatonic = transposition_.diatonic;
        const int written = detail::transposedFifths(concert.fifths(), -chromatic, -diatonic);
        if (detail::transposedFifths(written, chromatic, diatonic) != concert.fifths()) {
            // e.g. concert C-sharp major for a B-flat instrument: D-sharp major
            // has nine sharps, and E-flat major reads back as D-flat major.
            warnings_.add(track_.name(), "concert keys changed by the transposition", measure);
        }
        return written;
    }

    /// The written spelling: the sounding pitch moved back by the transposition's
    /// steps and semitones, so the importer finds the same spelling.
    [[nodiscard]] Spelled written(const Pitch& sounding) const {
        const int step = static_cast<int>(sounding.step());
        if (transposition_.semitones() == 0) {
            return {step, sounding.alter(), sounding.octave()};
        }
        const int diatonic = sounding.octave() * 7 + step - transposition_.steps();
        const int octave = floorDiv(diatonic, 7);
        const int newStep = diatonic - octave * 7;
        const int midi = sounding.midiNumber() - transposition_.semitones();
        const int alter = midi - detail::midiOf(newStep, 0, octave);
        if (alter >= -3 && alter <= 3) {
            return {newStep, alter, octave};
        }
        constexpr std::array<std::pair<int, int>, 12> kSharps{{{0, 0},
                                                               {0, 1},
                                                               {1, 0},
                                                               {1, 1},
                                                               {2, 0},
                                                               {3, 0},
                                                               {3, 1},
                                                               {4, 0},
                                                               {4, 1},
                                                               {5, 0},
                                                               {5, 1},
                                                               {6, 0}}};
        const int pitchClass = midi - floorDiv(midi, 12) * 12;
        const auto [sharpStep, sharpAlter] = kSharps[static_cast<std::size_t>(pitchClass)];
        return {sharpStep, sharpAlter, floorDiv(midi, 12) - 1};
    }

    // ---- Measures -----------------------------------------------------------------

    void writeMeasure(XmlWriter& xml, std::size_t m) {
        xml.start("measure");
        xml.attribute("number", static_cast<std::int64_t>(m + 1));
        xml.endOpen();
        writeLeftBarline(xml, m);
        writeAttributes(xml, m);
        const Fraction measureLength = song_.measureHeader(m).timeSignature.measureLength();
        tempoMarks_.clear();
        if (index_ == 0) {
            collectTempoMarks(m);
            writeSigns(xml, m);
        }
        for (int s = 0; s < xmlStaves_; ++s) {
            tabPass_ = s >= staffCount_;
            const std::size_t modelStaff = tabPass_ ? 0 : static_cast<std::size_t>(s);
            const Staff& staff = track_.staff(modelStaff);
            const Measure& measure = staff.measure(m);
            dynamicMarks_.clear();
            if (!tabPass_) {
                collectDynamicMarks(measure, modelStaff, m);
            }
            std::size_t last = 0;
            for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                if (!measure.voices[v].isEmpty()) {
                    last = v;
                }
            }
            for (std::size_t v = 0; v <= last; ++v) {
                if (cursor_ > Fraction(0)) {
                    xml.open("backup");
                    xml.number("duration", grid_.toDivisions(cursor_));
                    xml.close("backup");
                }
                cursor_ = writeVoice(xml, staff, m, s, v, measureLength);
            }
        }
        tabPass_ = false;
        cursor_ = Fraction(0);
        if (index_ == 0) {
            writeJumps(xml, m);
        }
        writeRightBarline(xml, m);
        xml.close("measure");
    }

    // "1, 2": the times through the repeat that play an alternate ending.
    static std::string endingNumbers(std::uint8_t times) {
        std::string numbers;
        for (int time = 1; time <= MeasureHeader::kMaxEnding; ++time) {
            if (((static_cast<unsigned>(times) >> static_cast<unsigned>(time - 1)) & 1U) != 0) {
                numbers += (numbers.empty() ? "" : ", ") + std::to_string(time);
            }
        }
        return numbers;
    }

    // A start repeat and the start of an alternate ending, before the notes.
    void writeLeftBarline(XmlWriter& xml, std::size_t m) const {
        const MeasureHeader& header = song_.measureHeader(m);
        const bool endingStarts =
            header.alternateEndings != 0 &&
            (m == 0 || song_.measureHeader(m - 1).alternateEndings != header.alternateEndings);
        if (!header.repeatStart && !endingStarts) {
            return;
        }
        xml.start("barline");
        xml.attribute("location", "left");
        xml.endOpen();
        if (header.repeatStart) {
            xml.text("bar-style", "heavy-light");
        }
        if (endingStarts) {
            xml.start("ending");
            xml.attribute("number", endingNumbers(header.alternateEndings));
            xml.attribute("type", "start");
            xml.endEmpty();
        }
        if (header.repeatStart) {
            xml.start("repeat");
            xml.attribute("direction", "forward");
            xml.endEmpty();
        }
        xml.close("barline");
    }

    // The end of an alternate ending (with a hook when a repeat ends there), an
    // end repeat, or the final barline.
    void writeRightBarline(XmlWriter& xml, std::size_t m) const {
        const MeasureHeader& header = song_.measureHeader(m);
        const bool last = m + 1 == song_.measureCount();
        const bool endingStops =
            header.alternateEndings != 0 &&
            (last || song_.measureHeader(m + 1).alternateEndings != header.alternateEndings);
        if (!last && !header.repeatEnd() && !endingStops) {
            return;
        }
        xml.start("barline");
        xml.attribute("location", "right");
        xml.endOpen();
        if (last || header.repeatEnd()) {
            xml.text("bar-style", "light-heavy");
        }
        if (endingStops) {
            xml.start("ending");
            xml.attribute("number", endingNumbers(header.alternateEndings));
            xml.attribute("type", header.repeatEnd() ? "stop" : "discontinue");
            xml.endEmpty();
        }
        if (header.repeatEnd()) {
            xml.start("repeat");
            xml.attribute("direction", "backward");
            if (header.repeatTimes != 2) {
                xml.attribute("times", std::to_string(header.repeatTimes));
            }
            xml.endEmpty();
        }
        xml.close("barline");
    }

    void writeAttributes(XmlWriter& xml, std::size_t m) {
        const MeasureHeader& header = song_.measureHeader(m);
        const MeasureHeader* previous = m > 0 ? &song_.measureHeader(m - 1) : nullptr;
        const bool key = previous == nullptr || previous->keySignature != header.keySignature;
        const bool time = previous == nullptr || previous->timeSignature != header.timeSignature;
        if (!key && !time) {
            return;
        }
        xml.open("attributes");
        if (previous == nullptr) {
            xml.number("divisions", grid_.divisions());
        }
        if (key) {
            xml.start("key");
            if (percussion_) {
                xml.attribute("print-object", "no");
            }
            xml.endOpen();
            xml.number("fifths", writtenFifths(header.keySignature, m));
            xml.text("mode", header.keySignature.mode() == KeyMode::Minor ? "minor" : "major");
            xml.close("key");
        }
        if (time) {
            xml.open("time");
            xml.number("beats", header.timeSignature.numerator());
            xml.number("beat-type", header.timeSignature.denominator());
            xml.close("time");
        }
        if (previous == nullptr) {
            if (xmlStaves_ > 1) {
                xml.number("staves", xmlStaves_);
            }
            for (int s = 0; s < xmlStaves_; ++s) {
                writeClef(xml, s);
            }
            for (int s = 0; s < xmlStaves_; ++s) {
                writeStaffDetails(xml, s);
            }
            if (transposition_.semitones() != 0) {
                xml.open("transpose");
                xml.number("diatonic", transposition_.diatonic);
                xml.number("chromatic", transposition_.chromatic);
                if (transposition_.octaveChange != 0) {
                    xml.number("octave-change", transposition_.octaveChange);
                }
                xml.close("transpose");
            }
        }
        xml.close("attributes");
    }

    void writeClef(XmlWriter& xml, int s) const {
        xml.start("clef");
        if (xmlStaves_ > 1) {
            xml.attribute("number", s + 1);
        }
        xml.endOpen();
        const auto sign = [&xml](std::string_view name, int line, int octaveChange) {
            xml.text("sign", name);
            if (line != 0) {
                xml.number("line", line);
            }
            if (octaveChange != 0) {
                xml.number("clef-octave-change", octaveChange);
            }
        };
        if (s >= staffCount_) {
            sign("TAB", 5, 0);
        } else {
            switch (track_.staff(static_cast<std::size_t>(s)).clef()) {
            case Clef::Treble:
                sign("G", 2, 0);
                break;
            case Clef::Treble8vb:
                sign("G", 2, -1);
                break;
            case Clef::Bass:
                sign("F", 4, 0);
                break;
            case Clef::Bass8vb:
                sign("F", 4, -1);
                break;
            case Clef::Alto:
                sign("C", 3, 0);
                break;
            case Clef::Tenor:
                sign("C", 4, 0);
                break;
            case Clef::Percussion:
                sign("percussion", 0, 0);
                break;
            }
        }
        xml.close("clef");
    }

    /// Tuning and capo: on the TAB staff of a single fretted staff, otherwise on
    /// each fretted staff.
    void writeStaffDetails(XmlWriter& xml, int s) const {
        const bool tab = s >= staffCount_;
        const Staff& staff = track_.staff(tab ? 0 : static_cast<std::size_t>(s));
        if (percussion_ || !staff.tuning() || (tabCopy_ && !tab)) {
            return;
        }
        const Tuning& tuning = *staff.tuning();
        xml.start("staff-details");
        if (xmlStaves_ > 1) {
            xml.attribute("number", s + 1);
        }
        xml.endOpen();
        if (tab) {
            xml.number("staff-lines", tuning.stringCount());
        }
        // Lines count from the bottom of the TAB staff: the lowest string.
        for (int line = 1; line <= tuning.stringCount(); ++line) {
            const Pitch open = tuning.openString(tuning.stringCount() - line + 1);
            xml.start("staff-tuning");
            xml.attribute("line", line);
            xml.endOpen();
            xml.text("tuning-step", kStepNames[static_cast<std::size_t>(open.step())]);
            if (open.alter() != 0) {
                xml.number("tuning-alter", open.alter());
            }
            xml.number("tuning-octave", open.octave());
            xml.close("staff-tuning");
        }
        if (staff.capo() != 0) {
            xml.number("capo", staff.capo());
        }
        xml.close("staff-details");
    }

    // ---- Directions -----------------------------------------------------------------

    // The signs and jumps (ADR-0049), in the first part: a glyph or a text for
    // the reader and a <sound> attribute for playback, which names the second
    // segno and coda "segno-segno" and "double-coda".
    static void writeNavigation(XmlWriter& xml, std::string_view glyph, std::string_view words,
                                std::string_view attribute, std::string_view value) {
        xml.start("direction");
        xml.attribute("placement", "above");
        xml.endOpen();
        xml.open("direction-type");
        if (glyph.empty()) {
            xml.text("words", words);
        } else {
            xml.empty(glyph);
        }
        xml.close("direction-type");
        xml.start("sound");
        xml.attribute(attribute, value);
        xml.endEmpty();
        xml.close("direction");
    }

    // The section's name (ADR-0062) and the signs open the measure.
    void writeSigns(XmlWriter& xml, std::size_t m) const {
        const MeasureHeader& header = song_.measureHeader(m);
        if (!header.section.empty()) {
            xml.start("direction");
            xml.attribute("placement", "above");
            xml.endOpen();
            xml.open("direction-type");
            xml.text("rehearsal", header.section);
            xml.close("direction-type");
            xml.close("direction");
        }
        if (header.segno) {
            writeNavigation(xml, "segno", {}, "segno", "segno");
        }
        if (header.segnoSegno) {
            writeNavigation(xml, "segno", {}, "segno", "segno-segno");
        }
        if (header.coda) {
            writeNavigation(xml, "coda", {}, "coda", "coda");
        }
        if (header.doubleCoda) {
            writeNavigation(xml, "coda", {}, "coda", "double-coda");
        }
    }

    // Fine, To Coda and the jump close it.
    void writeJumps(XmlWriter& xml, std::size_t m) const {
        const MeasureHeader& header = song_.measureHeader(m);
        if (header.fine) {
            writeNavigation(xml, {}, "Fine", "fine", "yes");
        }
        if (header.toCoda) {
            writeNavigation(xml, {}, "To Coda", "tocoda", "coda");
        }
        if (header.toDoubleCoda) {
            writeNavigation(xml, {}, "To Double Coda", "tocoda", "double-coda");
        }
        if (header.jump == JumpTo::None) {
            return;
        }
        constexpr std::array<std::string_view, 4> kTexts{"", "D.C.", "D.S.", "D.S.S."};
        constexpr std::array<std::string_view, 4> kEndings{"", " al Fine", " al Coda",
                                                           " al Double Coda"};
        const std::string words =
            std::string(kTexts.at(static_cast<std::size_t>(header.jump))) +
            std::string(kEndings.at(static_cast<std::size_t>(header.jumpThen)));
        if (header.jump == JumpTo::Start) {
            writeNavigation(xml, {}, words, "dacapo", "yes");
        } else {
            writeNavigation(xml, {}, words, "dalsegno",
                            header.jump == JumpTo::Segno ? "segno" : "segno-segno");
        }
    }

    void collectTempoMarks(std::size_t m) {
        const MeasureHeader& header = song_.measureHeader(m);
        const bool changeAtStart =
            m == 0 && !header.tempoChanges.empty() && header.tempoChanges.front().offset.isZero();
        if (m == 0) {
            if (changeAtStart) {
                warnings_.add({}, "a tempo change at the start replaces the start tempo", 0);
            } else {
                tempoMarks_.push_back(
                    TempoMark{.offset = Fraction(0), .tempo = writable(song_.startTempo(), m)});
            }
        }
        for (const TempoChange& change : header.tempoChanges) {
            if (change.gradual) {
                warnings_.add({}, "gradual tempo changes written as sudden ones", m);
            }
            tempoMarks_.push_back(
                TempoMark{.offset = change.offset, .tempo = writable(change.tempo, m)});
        }
    }

    /// A metronome mark has no tuplets: such beat units become quarter notes.
    Tempo writable(const Tempo& tempo, std::size_t m) {
        if (tempo.beatUnit().tuplet() == Tuplet{}) {
            return tempo;
        }
        warnings_.add({}, "tempos with a tuplet beat unit written in quarter notes", m);
        const double quarters = tempo.bpm() * tempo.beatUnit().length().toDouble() * 4.0;
        return Tempo(std::clamp(quarters, Tempo::kMinBpm, Tempo::kMaxBpm));
    }

    /// Tempo marks inside [from, to) are written before the element that starts
    /// at `from`, with an offset when they fall inside it; `to` empty: all left.
    void flushTempo(XmlWriter& xml, const Fraction& from, std::optional<Fraction> to) {
        for (TempoMark& mark : tempoMarks_) {
            if (mark.written || mark.offset < from || (to && mark.offset >= *to)) {
                continue;
            }
            mark.written = true;
            const Tempo& tempo = mark.tempo;
            xml.start("direction");
            xml.attribute("placement", "above");
            xml.endOpen();
            xml.open("direction-type");
            xml.open("metronome");
            xml.text("beat-unit", kTypeNames[static_cast<std::size_t>(tempo.beatUnit().value())]);
            for (int dot = 0; dot < tempo.beatUnit().dots(); ++dot) {
                xml.empty("beat-unit-dot");
            }
            xml.text("per-minute", decimal(tempo.bpm()));
            xml.close("metronome");
            xml.close("direction-type");
            if (mark.offset != from) {
                xml.start("offset");
                xml.attribute("sound", "yes");
                xml.endNumber("offset", grid_.toDivisions(mark.offset - from));
            }
            xml.start("sound");
            xml.attribute("tempo",
                          decimal(tempo.bpm() * tempo.beatUnit().length().toDouble() * 4.0));
            xml.endEmpty();
            xml.close("direction");
        }
    }

    /// Dynamics are marked per staff where they change, from MF. Notes that
    /// start together on a staff share the mark, so they need one dynamic.
    void collectDynamicMarks(const Measure& measure, std::size_t staff, std::size_t m) {
        auto& onsets = onsets_;
        onsets.clear();
        std::size_t voices = 0;
        for (const Voice& voice : measure.voices) {
            Fraction onset;
            if (!voice.isEmpty()) {
                ++voices;
            }
            for (const Beat& beat : voice.beats) {
                for (const Note& note : beat.notes) {
                    onsets.push_back(Onset{.onset = onset, .dynamic = note.effects.dynamic});
                }
                onset += beat.duration.length();
            }
        }
        if (voices > 1) {
            std::ranges::stable_sort(onsets, std::less<>{}, &Onset::onset);
        }
        Dynamic& current = dynamics_[staff];
        for (std::size_t i = 0; i < onsets.size();) {
            const Onset& first = onsets[i];
            bool mixed = false;
            std::size_t j = i;
            for (; j < onsets.size() && onsets[j].onset == first.onset; ++j) {
                mixed = mixed || onsets[j].dynamic != first.dynamic;
            }
            if (mixed) {
                warn("notes of different dynamics at the same time written with one dynamic", m);
            }
            if (first.dynamic != current) {
                dynamicMarks_.push_back(
                    DynamicMark{.onset = first.onset, .dynamic = first.dynamic});
                current = first.dynamic;
            }
            i = j;
        }
    }

    void flushDynamics(XmlWriter& xml, const Fraction& onset, int staffNumber) {
        for (DynamicMark& mark : dynamicMarks_) {
            if (mark.written || mark.onset != onset) {
                continue;
            }
            mark.written = true;
            xml.start("direction");
            xml.attribute("placement", "below");
            xml.endOpen();
            xml.open("direction-type");
            xml.open("dynamics");
            xml.empty(kDynamicNames[static_cast<std::size_t>(mark.dynamic)]);
            xml.close("dynamics");
            xml.close("direction-type");
            if (xmlStaves_ > 1) {
                xml.number("staff", staffNumber);
            }
            // <sound dynamics> is a percentage of forte, MIDI velocity 90.
            xml.start("sound");
            xml.attribute("dynamics", static_cast<std::int64_t>(std::lround(
                                          dynamicVelocity(mark.dynamic) * 100.0 / 90.0)));
            xml.endEmpty();
            xml.close("direction");
        }
    }

    /// A wedge's start or stop; voices number their wedges apart.
    void writeWedge(XmlWriter& xml, std::string_view type, std::size_t voice, int staffNumber) {
        xml.start("direction");
        xml.attribute("placement", "below");
        xml.endOpen();
        xml.open("direction-type");
        xml.start("wedge");
        xml.attribute("type", type);
        xml.attribute("number", static_cast<std::int64_t>(voice + 1));
        xml.endEmpty();
        xml.close("direction-type");
        if (xmlStaves_ > 1) {
            xml.number("staff", staffNumber);
        }
        xml.close("direction");
    }

    // ---- Voices and notes -------------------------------------------------------------

    /// Writes one voice of one staff and returns where it ends.
    Fraction writeVoice(XmlWriter& xml, const Staff& staff, std::size_t m, int s, std::size_t v,
                        const Fraction& measureLength) {
        const Voice& voice = staff.measure(m).voices[v];
        const int voiceNumber = s * 4 + static_cast<int>(v) + 1;
        const bool carriesTempo = s == 0 && v == 0;
        if (voice.isEmpty()) {
            if (carriesTempo) {
                flushTempo(xml, Fraction(0), std::nullopt);
            }
            // An empty voice before the last one only keeps the voice numbers.
            xml.start("note");
            if (v > 0) {
                xml.attribute("print-object", "no");
            }
            xml.endOpen();
            xml.start("rest");
            xml.attribute("measure", "yes");
            xml.endEmpty();
            xml.number("duration", grid_.toDivisions(measureLength));
            xml.number("voice", voiceNumber);
            if (xmlStaves_ > 1) {
                xml.number("staff", s + 1);
            }
            xml.close("note");
            return measureLength;
        }
        markTuplets(voice);
        Fraction onset;
        for (std::size_t i = 0; i < voice.beats.size(); ++i) {
            const Beat& beat = voice.beats[i];
            const Fraction length = beat.duration.length();
            if (carriesTempo) {
                flushTempo(xml, onset, onset + length);
            }
            flushDynamics(xml, onset, s + 1);
            const Beat* previous = beatBefore(staff, m, v, i);
            const Beat* next = beatAfter(staff, m, v, i);
            // A run of beats with one hairpin is a wedge from its first beat to
            // the end of its last (ADR-0078).
            const Hairpin hairpin = tabPass_ ? Hairpin::None : beat.effects.hairpin;
            if (hairpin != Hairpin::None &&
                (previous == nullptr || previous->effects.hairpin != hairpin)) {
                writeWedge(xml, hairpin == Hairpin::Crescendo ? "crescendo" : "diminuendo", v,
                           s + 1);
            }
            if (beat.chord && !tabPass_) {
                writeHarmony(xml, *beat.chord, s + 1);
            }
            if (beat.text && !tabPass_) {
                writeWords(xml, *beat.text, s + 1);
            }
            writeGraces(xml, beat, voiceNumber, s + 1, m);
            writeBeat(xml, BeatPlace{.beat = &beat,
                                     .previous = previous,
                                     .next = next,
                                     .voice = voiceNumber,
                                     .staff = s + 1,
                                     .measure = m,
                                     .tuplet = tuplets_[i]});
            if (hairpin != Hairpin::None && (next == nullptr || next->effects.hairpin != hairpin)) {
                writeWedge(xml, "stop", v, s + 1);
            }
            onset += length;
        }
        if (carriesTempo) {
            flushTempo(xml, onset, std::nullopt);
        }
        return onset;
    }

    /// Tuplet brackets: consecutive beats of one tuplet, closed when they fill
    /// `normal` times their shortest note value (three triplet eighths, a
    /// triplet quarter and eighth...), when the tuplet changes or at the end.
    void markTuplets(const Voice& voice) {
        tuplets_.assign(voice.beats.size(), 0);
        for (std::size_t i = 0; i < voice.beats.size();) {
            const Tuplet tuplet = voice.beats[i].duration.tuplet();
            if (tuplet.isNone()) {
                ++i;
                continue;
            }
            const std::size_t first = i;
            Fraction filled;
            Fraction shortest = noteValueLength(voice.beats[i].duration.value());
            while (i < voice.beats.size() && voice.beats[i].duration.tuplet() == tuplet) {
                const Duration& duration = voice.beats[i].duration;
                filled += duration.length();
                shortest = std::min(shortest, noteValueLength(duration.value()));
                ++i;
                if (filled >= Fraction(tuplet.normal()) * shortest) {
                    break;
                }
            }
            tuplets_[first] = static_cast<std::uint8_t>(tuplets_[first] | kTupletStart);
            tuplets_[i - 1] = static_cast<std::uint8_t>(tuplets_[i - 1] | kTupletStop);
        }
    }

    /// A chord symbol before its beat (ADR-0080): the name's root, kind (with
    /// the name's own suffix to print) and bass, and its diagram as a frame. A
    /// name that does not start with a pitch ("N.C.") is printed as it is.
    void writeHarmony(XmlWriter& xml, const ChordSymbol& chord, int staff) {
        xml.start("harmony");
        if (chord.diagram) {
            xml.attribute("print-frame", "yes");
        }
        xml.endOpen();
        const std::optional<ChordNameParts> spelling = splitChordName(chord.name);
        xml.open("root");
        if (spelling) {
            writeStep(xml, "root-step", "root-alter", spelling->root);
        } else {
            xml.start("root-step");
            xml.attribute("text", "");
            xml.endText("root-step", "C");
        }
        xml.close("root");
        xml.start("kind");
        xml.attribute("text", spelling ? spelling->suffix : std::string_view(chord.name));
        xml.endText("kind", spelling ? kindFor(spelling->suffix) : "none");
        if (spelling && spelling->bass) {
            xml.open("bass");
            writeStep(xml, "bass-step", "bass-alter", *spelling->bass);
            xml.close("bass");
        }
        if (chord.diagram) {
            writeFrame(xml, *chord.diagram);
        }
        if (xmlStaves_ > 1) {
            xml.number("staff", staff);
        }
        xml.close("harmony");
    }

    /// A beat's text: words above its staff (ADR-0081).
    void writeWords(XmlWriter& xml, std::string_view text, int staff) const {
        xml.start("direction");
        xml.attribute("placement", "above");
        xml.endOpen();
        xml.open("direction-type");
        xml.text("words", text);
        xml.close("direction-type");
        if (xmlStaves_ > 1) {
            xml.number("staff", staff);
        }
        xml.close("direction");
    }

    static void writeStep(XmlWriter& xml, std::string_view step, std::string_view alter,
                          const ChordPitch& pitch) {
        xml.text(step, kStepNames[static_cast<std::size_t>(pitch.step)]);
        if (pitch.alter != 0) {
            xml.number(alter, pitch.alter);
        }
    }

    /// String 1 is the highest; strings not played get no <frame-note>.
    static void writeFrame(XmlWriter& xml, const ChordDiagram& diagram) {
        xml.open("frame");
        xml.number("frame-strings", static_cast<std::int64_t>(diagram.frets.size()));
        int highest = diagram.firstFret;
        for (const std::int8_t fret : diagram.frets) {
            highest = std::max<int>(highest, fret);
        }
        xml.number("frame-frets", std::max(4, highest - diagram.firstFret + 1));
        if (diagram.firstFret > 1) {
            xml.number("first-fret", diagram.firstFret);
        }
        for (std::size_t i = 0; i < diagram.frets.size(); ++i) {
            if (diagram.frets[i] == ChordDiagram::kMuted) {
                continue;
            }
            xml.open("frame-note");
            xml.number("string", static_cast<std::int64_t>(i + 1));
            xml.number("fret", diagram.frets[i]);
            xml.close("frame-note");
        }
        xml.close("frame");
    }

    /// The grace notes leading into `beat`, before it and with no duration: a
    /// slash for those before the beat (acciaccaturas), none for those on it.
    void writeGraces(XmlWriter& xml, const Beat& beat, int voice, int staff, std::size_t m) {
        for (const Grace& grace : beat.graces) {
            const Beat notes{.duration = grace.duration, .notes = grace.notes};
            const BeatPlace place{.beat = &notes,
                                  .voice = voice,
                                  .staff = staff,
                                  .measure = m,
                                  .grace = grace.onBeat ? kGraceOn : kGraceBefore};
            for (std::size_t n = 0; n < notes.notes.size(); ++n) {
                writeNote(xml, place, n);
            }
        }
    }

    void writeBeat(XmlWriter& xml, const BeatPlace& place) {
        const Beat& beat = *place.beat;
        if (beat.isRest()) {
            BeatEffects others = beat.effects;
            others.hairpin = Hairpin::None;
            if (others != BeatEffects{}) {
                warn("effects on rests dropped", place.measure);
            }
            xml.open("note");
            xml.empty("rest");
            xml.number("duration", grid_.toDivisions(beat.duration.length()));
            xml.number("voice", place.voice);
            writeType(xml, beat.duration);
            if (xmlStaves_ > 1) {
                xml.number("staff", place.staff);
            }
            if (place.tuplet != 0) {
                xml.open("notations");
                writeTupletMarks(xml, place.tuplet);
                xml.close("notations");
            }
            xml.close("note");
            return;
        }
        if (!beat.effects.whammyBar.isEmpty()) {
            warn("whammy bar dropped", place.measure);
        }
        if (beat.effects.bassTechnique == BassTechnique::Slap ||
            beat.effects.bassTechnique == BassTechnique::Pop) {
            warn("slap and pop dropped", place.measure);
        }
        for (std::size_t n = 0; n < beat.notes.size(); ++n) {
            writeNote(xml, place, n);
        }
    }

    void writeType(XmlWriter& xml, const Duration& duration) const {
        xml.text("type", kTypeNames[static_cast<std::size_t>(duration.value())]);
        for (int dot = 0; dot < duration.dots(); ++dot) {
            xml.empty("dot");
        }
        if (duration.tuplet() != Tuplet{}) {
            xml.open("time-modification");
            xml.number("actual-notes", duration.tuplet().actual());
            xml.number("normal-notes", duration.tuplet().normal());
            xml.close("time-modification");
        }
    }

    static void writeTupletMarks(XmlWriter& xml, std::uint8_t marks) {
        if ((marks & kTupletStart) != 0) {
            xml.start("tuplet");
            xml.attribute("type", "start");
            xml.attribute("bracket", "yes");
            xml.endEmpty();
        }
        if ((marks & kTupletStop) != 0) {
            xml.start("tuplet");
            xml.attribute("type", "stop");
            xml.endEmpty();
        }
    }

    void writeNote(XmlWriter& xml, const BeatPlace& place, std::size_t n) {
        const Beat& beat = *place.beat;
        const Note& note = beat.notes[n];
        const NoteEffects& fx = note.effects;
        const BeatEffects& beatFx = beat.effects;
        const bool firstNote = n == 0;
        const int midi = note.pitch.midiNumber();

        // Connections to the neighbouring beats of the voice.
        const bool tieStart =
            place.next != nullptr &&
            std::ranges::any_of(place.next->notes, [midi](const Note& other) {
                return other.effects.tiedFromPrevious && other.pitch.midiNumber() == midi;
            });
        const Note* previousNote = nullptr;
        if (place.previous != nullptr) {
            if (const auto from = partnerIn(*place.previous, note, n)) {
                previousNote = &place.previous->notes[*from];
            }
        }
        const auto slides = [](const Note* other) {
            return other != nullptr && (other->effects.slideOut == SlideOut::Legato ||
                                        other->effects.slideOut == SlideOut::Shift);
        };
        const bool slideStop = slides(previousNote);
        const bool slideStart = slides(&note);
        std::optional<bool> legatoStart; // true: hammer-on, false: pull-off
        if (place.next != nullptr) {
            for (std::size_t j = 0; j < place.next->notes.size(); ++j) {
                const Note& other = place.next->notes[j];
                if (other.effects.legatoFromPrevious && partnerIn(beat, other, j) == n) {
                    legatoStart = other.pitch.midiNumber() >= midi;
                    break;
                }
            }
        }

        if (fx.letRing) {
            warn("let ring dropped", place.measure);
        }
        if (fx.slideOut == SlideOut::Shift) {
            warn("shift slides written as legato slides", place.measure);
        }

        xml.open("note");
        if (place.grace == kGraceBefore) {
            xml.start("grace");
            xml.attribute("slash", "yes");
            xml.endEmpty();
        } else if (place.grace == kGraceOn) {
            xml.empty("grace");
        }
        if (n > 0) {
            xml.empty("chord");
        }
        if (percussion_) {
            const DrumDisplay display = drumDisplay(midi);
            xml.open("unpitched");
            xml.text("display-step", display.step);
            xml.number("display-octave", display.octave);
            xml.close("unpitched");
        } else {
            const Spelled spelled = written(note.pitch);
            xml.open("pitch");
            xml.text("step", kStepNames[static_cast<std::size_t>(spelled.step)]);
            if (spelled.alter != 0) {
                xml.number("alter", spelled.alter);
            }
            xml.number("octave", spelled.octave);
            xml.close("pitch");
        }
        if (place.grace == 0) { // a grace note takes no time
            xml.number("duration", grid_.toDivisions(beat.duration.length()));
        }
        if (fx.tiedFromPrevious) {
            xml.start("tie");
            xml.attribute("type", "stop");
            xml.endEmpty();
        }
        if (tieStart) {
            xml.start("tie");
            xml.attribute("type", "start");
            xml.endEmpty();
        }
        if (percussion_) {
            xml.start("instrument");
            xml.attribute("id", std::format("{}-I{}", id_, midi + 1));
            xml.endEmpty();
        }
        xml.number("voice", place.voice);
        writeType(xml, beat.duration);
        writeNotehead(xml, note, place.measure);
        if (xmlStaves_ > 1) {
            xml.number("staff", place.staff);
        }

        const bool tupletMarks = firstNote && place.tuplet != 0;
        const bool ornaments =
            fx.vibrato != Vibrato::None || (firstNote && beatFx.tremoloPicking.has_value());
        const bool tapped = firstNote && beatFx.bassTechnique == BassTechnique::Tap;
        const bool technical = tapped || (firstNote && beatFx.pickStroke != PickStroke::None) ||
                               fx.legatoFromPrevious || legatoStart.has_value() ||
                               (note.position && !percussion_) || !fx.bend.isEmpty() ||
                               fx.harmonic.kind != HarmonicKind::None;
        const bool articulations =
            fx.staccato || fx.accent != Accent::None || fx.slideIn != SlideIn::None ||
            fx.slideOut == SlideOut::Downwards || fx.slideOut == SlideOut::Upwards;
        const bool arpeggiate = beatFx.strum != Strum::None;
        if (fx.tiedFromPrevious || tieStart || slideStop || slideStart || tupletMarks ||
            ornaments || technical || articulations || arpeggiate) {
            xml.open("notations");
            if (fx.tiedFromPrevious) {
                xml.start("tied");
                xml.attribute("type", "stop");
                xml.endEmpty();
            }
            if (tieStart) {
                xml.start("tied");
                xml.attribute("type", "start");
                xml.endEmpty();
            }
            if (slideStop) {
                xml.start("slide");
                xml.attribute("type", "stop");
                xml.endEmpty();
            }
            if (slideStart) {
                xml.start("slide");
                xml.attribute("type", "start");
                xml.attribute("line-type", "solid");
                xml.endEmpty();
            }
            if (tupletMarks) {
                writeTupletMarks(xml, place.tuplet);
            }
            if (ornaments) {
                writeOrnaments(xml, fx, firstNote ? beatFx.tremoloPicking : std::nullopt,
                               place.measure);
            }
            if (technical) {
                xml.open("technical");
                if (firstNote && beatFx.pickStroke != PickStroke::None) {
                    xml.empty(beatFx.pickStroke == PickStroke::Up ? "up-bow" : "down-bow");
                }
                if (tapped) {
                    xml.text("tap", "T");
                }
                if (fx.legatoFromPrevious) {
                    const bool pull =
                        previousNote != nullptr && midi < previousNote->pitch.midiNumber();
                    xml.start(pull ? "pull-off" : "hammer-on");
                    xml.attribute("type", "stop");
                    xml.endEmpty();
                }
                if (legatoStart) {
                    const std::string_view name = *legatoStart ? "hammer-on" : "pull-off";
                    xml.start(name);
                    xml.attribute("type", "start");
                    xml.endText(name, *legatoStart ? "H" : "P");
                }
                if (note.position && !percussion_) {
                    xml.number("string", note.position->string);
                    xml.number("fret", note.position->fret);
                }
                if (!fx.bend.isEmpty()) {
                    writeBend(xml, fx.bend, place.measure);
                }
                if (fx.harmonic.kind != HarmonicKind::None) {
                    writeHarmonic(xml, note, place.measure);
                }
                xml.close("technical");
            }
            if (articulations) {
                xml.open("articulations");
                if (fx.accent != Accent::None) {
                    xml.empty(fx.accent == Accent::Marcato ? "strong-accent" : "accent");
                }
                if (fx.staccato) {
                    xml.empty("staccato");
                }
                if (fx.slideIn != SlideIn::None) {
                    xml.empty(fx.slideIn == SlideIn::FromBelow ? "scoop" : "plop");
                }
                if (fx.slideOut == SlideOut::Upwards) {
                    xml.empty("doit");
                } else if (fx.slideOut == SlideOut::Downwards) {
                    xml.empty("falloff");
                }
                xml.close("articulations");
            }
            if (arpeggiate) {
                // A downstroke plays the low strings first: rolled upwards.
                xml.start("arpeggiate");
                xml.attribute("direction", beatFx.strum == Strum::Up ? "down" : "up");
                xml.endEmpty();
            }
            xml.close("notations");
        }
        if (fx.palmMute) {
            xml.open("play");
            xml.text("mute", "palm");
            xml.close("play");
        }
        xml.close("note");
    }

    void writeNotehead(XmlWriter& xml, const Note& note, std::size_t m) {
        const NoteEffects& fx = note.effects;
        std::string_view head;
        if (percussion_) {
            if (fx.deadNote) {
                warn("dead notes on drums dropped", m);
            }
            head = drumDisplay(note.pitch.midiNumber()).notehead;
            if (head == "normal" && !fx.ghost) {
                return;
            }
        } else if (fx.deadNote || fx.ghost) {
            head = fx.deadNote ? "x" : "normal";
        } else {
            return;
        }
        xml.start("notehead");
        if (fx.ghost) {
            xml.attribute("parentheses", "yes");
        }
        xml.endText("notehead", head);
    }

    void writeOrnaments(XmlWriter& xml, const NoteEffects& fx, std::optional<NoteValue> tremolo,
                        std::size_t m) {
        xml.open("ornaments");
        if (fx.vibrato != Vibrato::None) {
            xml.start("wavy-line");
            xml.attribute("type", "start");
            xml.attribute("smufl", fx.vibrato == Vibrato::Wide ? detail::kWideVibratoGlyph
                                                               : detail::kVibratoGlyph);
            xml.endEmpty();
            xml.start("wavy-line");
            xml.attribute("type", "stop");
            xml.endEmpty();
        }
        if (tremolo) {
            // Strokes through the stem: 1 eighths, 2 sixteenths, 3 thirty-seconds.
            int marks = static_cast<int>(*tremolo) - static_cast<int>(NoteValue::Quarter);
            if (marks < 1 || marks > 3) {
                warn("tremolo picking written at the nearest speed (eighths, sixteenths or "
                     "thirty-seconds)",
                     m);
                marks = std::clamp(marks, 1, 3);
            }
            xml.start("tremolo");
            xml.attribute("type", "single");
            xml.endNumber("tremolo", marks);
        }
        xml.close("ornaments");
    }

    /// MusicXML bends are a bend, optionally a pre-bend, optionally released:
    /// other curves become the nearest of those.
    void writeBend(XmlWriter& xml, const BendCurve& curve, std::size_t m) {
        const int first = curve.points().front().cents;
        const int last = curve.points().back().cents;
        const bool preBend = first > 0;
        const int cents = preBend ? first : curve.maxCents();
        if (cents <= 0) {
            warn("bend curves written as the nearest bend, release or pre-bend", m);
            return;
        }
        const bool release = last < cents;
        const BendCurve nearest =
            preBend ? (release ? BendCurve::preBendRelease(cents) : BendCurve::preBend(cents))
                    : (release ? BendCurve::bendRelease(cents) : BendCurve::bend(cents));
        if (nearest != curve) {
            warn("bend curves written as the nearest bend, release or pre-bend", m);
        }
        xml.open("bend");
        xml.text("bend-alter", decimal(cents / 100.0));
        if (preBend) {
            xml.empty("pre-bend");
        }
        xml.close("bend");
        if (release) {
            xml.open("bend");
            xml.text("bend-alter", decimal(-cents / 100.0));
            xml.empty("release");
            xml.close("bend");
        }
    }

    void writeHarmonic(XmlWriter& xml, const Note& note, std::size_t m) {
        const Harmonic& harmonic = note.effects.harmonic;
        bool natural = false;
        switch (harmonic.kind) {
        case HarmonicKind::Natural:
            natural = true;
            if (harmonic.fretTenths != (note.position ? note.position->fret * 10 : 120)) {
                warn("natural harmonics written at their fret", m);
            }
            break;
        case HarmonicKind::Artificial:
            if (harmonic.fretTenths != 120) {
                warn("artificial harmonics written twelve frets above the note", m);
            }
            break;
        default:
            warn("pinch, tapped and semi harmonics written as artificial harmonics", m);
            break;
        }
        xml.open("harmonic");
        xml.empty(natural ? "natural" : "artificial");
        xml.close("harmonic");
    }

    const Song& song_;
    const Track& track_;
    std::size_t index_;
    std::string id_;
    const Grid& grid_;
    int channel_;
    bool percussion_;
    bool tabCopy_; // a single fretted staff is written with a TAB staff after it
    int staffCount_;
    int xmlStaves_;
    Transposition transposition_{};
    std::vector<int> drumKeys_{};
    std::vector<Dynamic> dynamics_; // per model staff, at the end of the last measure
    Warnings warnings_{};
    bool tabPass_ = false;
    Fraction cursor_{};

    // Buffers reused from measure to measure.
    std::vector<TempoMark> tempoMarks_{};
    std::vector<DynamicMark> dynamicMarks_{};
    std::vector<Onset> onsets_{};
    std::vector<std::uint8_t> tuplets_{};
};

// ---- Score ------------------------------------------------------------------------

void writeHeader(XmlWriter& xml, std::string& out, const SongInfo& info) {
    out += R"(<?xml version="1.0" encoding="UTF-8" standalone="no"?>)"
           "\n"
           R"(<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" )"
           R"("http://www.musicxml.org/dtds/partwise.dtd">)";
    xml.start("score-partwise");
    xml.attribute("version", "4.0");
    xml.endOpen();
    // The importer reads work-title as the title and a different movement-title
    // as the subtitle; a subtitle that cannot go there goes in a credit.
    bool subtitleCredit = false;
    if (!info.title.empty()) {
        xml.open("work");
        xml.text("work-title", info.title);
        xml.close("work");
        if (!info.subtitle.empty() && info.subtitle != info.title) {
            xml.text("movement-title", info.subtitle);
        } else {
            subtitleCredit = !info.subtitle.empty();
        }
    } else {
        subtitleCredit = !info.subtitle.empty();
    }
    xml.open("identification");
    const auto creator = [&xml](std::string_view type, const std::string& name) {
        if (!name.empty()) {
            xml.start("creator");
            xml.attribute("type", type);
            xml.endText("creator", name);
        }
    };
    creator("composer", info.music);
    creator("lyricist", info.words);
    creator("artist", info.artist);
    creator("transcriber", info.transcriber);
    if (!info.copyright.empty()) {
        xml.text("rights", info.copyright);
    }
    xml.open("encoding");
    xml.text("software", std::format("Elara {}", core::version()));
    for (const char* element : {"accidental", "beam", "stem"}) {
        xml.start("supports");
        xml.attribute("element", element);
        xml.attribute("type", "no");
        xml.endEmpty();
    }
    xml.close("encoding");
    if (!info.album.empty() || !info.notes.empty()) {
        xml.open("miscellaneous");
        const auto field = [&xml](std::string_view name, const std::string& value) {
            if (!value.empty()) {
                xml.start("miscellaneous-field");
                xml.attribute("name", name);
                xml.endText("miscellaneous-field", value);
            }
        };
        field("album", info.album);
        field("notes", info.notes);
        xml.close("miscellaneous");
    }
    xml.close("identification");
    if (subtitleCredit) {
        xml.start("credit");
        xml.attribute("page", 1);
        xml.endOpen();
        xml.text("credit-type", "subtitle");
        xml.text("credit-words", info.subtitle);
        xml.close("credit");
    }
}

constexpr std::string_view kMxlContainer =
    R"(<?xml version="1.0" encoding="UTF-8"?>
<container>
  <rootfiles>
    <rootfile full-path="score.musicxml" media-type="application/vnd.recordare.musicxml+xml"/>
  </rootfiles>
</container>
)";

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

ExportResult exportMusicXml(const Song& song, const MusicXmlExportOptions& options) {
    const SongScan songScan = scan(song);
    const Grid& grid = songScan.grid;
    Warnings scoreWarnings;
    if (!grid.exact()) {
        scoreWarnings.add({}, "durations rounded to the MusicXML grid", 0);
    }
    if (song.trackCount() == 0 && song.measureCount() > 0) {
        scoreWarnings.add({}, "songs without tracks keep no measures", 0);
    }
    if (song.trackCount() > 0 && song.measureCount() == 0) {
        scoreWarnings.add({}, "songs without measures keep only track names and instruments", 0);
    }

    std::vector<PartWriter> parts;
    parts.reserve(song.trackCount());
    int pitchedParts = 0;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        // Channels 1-16 without 10, which General MIDI keeps for drums.
        const int slot = pitchedParts % 15;
        if (song.track(t).instrument().kind != InstrumentKind::Percussion) {
            ++pitchedParts;
        }
        parts.emplace_back(song, t, grid, slot < 9 ? slot + 1 : slot + 2);
    }

    std::size_t work = 0;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const bool tab = song.track(t).staffCount() == 1 && song.track(t).staff(0).isFretted();
        work += songScan.work[t] * (tab ? 2 : 1);
    }
    constexpr std::size_t kBytesPerItem = 260; // measured on the benchmark song
    std::string xml;
    xml.reserve(work * kBytesPerItem + 4096);
    XmlWriter writer(xml, 0, !options.compressed);
    writeHeader(writer, xml, song.info());
    writer.open("part-list");
    for (const PartWriter& part : parts) {
        part.writeScorePart(writer);
    }
    writer.close("part-list");

    constexpr std::size_t kParallelWork = 20'000; // beats + notes; measured
    const unsigned threads =
        parts.size() <= 1 ? 1U
                          : detail::threadsFor(options.threads, work, kParallelWork, parts.size());
    if (threads <= 1) {
        for (PartWriter& part : parts) {
            XmlWriter partWriter(xml, 1, !options.compressed);
            part.writePart(partWriter);
        }
    } else {
        // Parts only read the song: each is written to its own text on any
        // thread, then joined in score order.
        std::vector<std::string> texts(parts.size());
        detail::forEachIndex(0, parts.size(), threads, [&](std::size_t i) {
            texts[i].reserve(songScan.work[i] * kBytesPerItem * 2);
            XmlWriter partWriter(texts[i], 1, !options.compressed);
            parts[i].writePart(partWriter);
        });
        for (const std::string& text : texts) {
            xml += text;
        }
    }
    writer.close("score-partwise");
    xml += '\n';

    for (const PartWriter& part : parts) {
        scoreWarnings.append(part.warnings());
    }
    ExportResult result{.data = {}, .warnings = scoreWarnings.lines()};
    if (options.compressed) {
        const std::array entries{
            detail::ZipEntry{.name = "mimetype", .content = detail::kMxlMimeType, .level = 0},
            detail::ZipEntry{.name = "META-INF/container.xml", .content = kMxlContainer},
            detail::ZipEntry{.name = "score.musicxml", .content = xml}};
        const std::vector<std::uint8_t> bytes = detail::writeZip(entries);
        result.data.assign(bytes.begin(), bytes.end());
    } else {
        result.data = std::move(xml);
    }
    return result;
}

std::vector<std::string> exportMusicXmlFile(const Song& song, const std::filesystem::path& path,
                                            const MusicXmlExportOptions& options) {
    MusicXmlExportOptions chosen = options;
    chosen.compressed = lowercase(detail::displayPath(path.extension())) == ".mxl";
    ExportResult result = exportMusicXml(song, chosen);
    detail::writeFileAtomically(path, result.data);
    return std::move(result.warnings);
}

} // namespace elara::io
