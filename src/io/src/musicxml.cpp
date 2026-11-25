// SPDX-License-Identifier: AGPL-3.0-or-later
// MusicXML import (ADR-0007), written from the MusicXML 4.0 specification.
#include <elara/core/lyrics.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/musicxml.hpp>

#include "archive.hpp"
#include "chord_pool.hpp"
#include "musicxml_shared.hpp"
#include "parallel.hpp"
#include "warnings.hpp"
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace elara::io {

namespace {

using namespace core;
using namespace std::string_view_literals;
using detail::kTypeNames;
using detail::transposedFifths;
using detail::Warnings;

// ---- Small helpers ------------------------------------------------------------

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

/// The element's text, trimmed; a view into the document.
std::string_view textView(pugi::xml_node node) {
    return trim(node.child_value());
}

std::string textOf(pugi::xml_node node) {
    return std::string(textView(node));
}

// Numbers are clamped where they are read, so a damaged file cannot overflow
// the arithmetic that follows.

int intIn(pugi::xml_node node, int fallback, int low, int high) {
    return std::clamp(node.text().as_int(fallback), low, high);
}

double numberIn(pugi::xml_node node, double low, double high) {
    const double value = node.text().as_double(0.0);
    return std::isnan(value) ? 0.0 : std::clamp(value, low, high);
}

/// A decimal from `low` to `high`, or nullopt if the text is not one.
std::optional<double> decimalIn(pugi::xml_node node, double low, double high) {
    const char* text = node.text().get();
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) {
        return std::nullopt;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        ++end;
    }
    if (*end != '\0' || !(value >= low && value <= high)) {
        return std::nullopt;
    }
    return value;
}

int roundedIn(pugi::xml_node node, int low, int high) {
    return static_cast<int>(std::lround(numberIn(node, low, high)));
}

bool isType(pugi::xml_node node, std::string_view type) {
    return node.attribute("type").value() == type;
}

std::optional<Step> stepOf(std::string_view text) {
    if (text.size() != 1) {
        return std::nullopt;
    }
    switch (text[0]) {
    case 'C':
        return Step::C;
    case 'D':
        return Step::D;
    case 'E':
        return Step::E;
    case 'F':
        return Step::F;
    case 'G':
        return Step::G;
    case 'A':
        return Step::A;
    case 'B':
        return Step::B;
    default:
        return std::nullopt;
    }
}

int midiOf(Step step, int alter, int octave) {
    return detail::midiOf(static_cast<int>(step), alter, octave);
}

std::optional<NoteValue> noteValueOf(std::string_view type) {
    const auto found = std::ranges::find(kTypeNames, type);
    if (found == kTypeNames.end()) {
        return std::nullopt;
    }
    return static_cast<NoteValue>(std::distance(kTypeNames.begin(), found));
}

/// The single duration (value and dots, no tuplet) that lasts `length`.
std::optional<Duration> exactDuration(const Fraction& length) {
    for (std::size_t value = 0; value < kTypeNames.size(); ++value) {
        for (int dots = 0; dots <= Duration::kMaxDots; ++dots) {
            const Duration duration(static_cast<NoteValue>(value), dots);
            if (duration.length() == length) {
                return duration;
            }
        }
    }
    return std::nullopt;
}

/// `length` as one duration or as the fewest plain values, largest first;
/// nullopt when it is not a multiple of a 128th.
std::optional<std::vector<Duration>> splitLength(Fraction length) {
    if (length <= Fraction(0)) {
        return std::vector<Duration>{};
    }
    if (const auto exact = exactDuration(length)) {
        return std::vector<Duration>{*exact};
    }
    std::vector<Duration> pieces;
    for (std::size_t value = 0; value < kTypeNames.size() && !length.isZero(); ++value) {
        const Duration piece(static_cast<NoteValue>(value));
        while (length >= piece.length()) {
            pieces.push_back(piece);
            length -= piece.length();
            if (pieces.size() > 64) {
                return std::nullopt;
            }
        }
    }
    if (!length.isZero()) {
        return std::nullopt;
    }
    return pieces;
}

/// Rests that fill `gap`: plain values, or rests of the next note's tuplet
/// when the gap is inside a tuplet.
std::optional<std::vector<Duration>> restsFor(const Fraction& gap,
                                              const std::optional<Duration>& next) {
    if (auto plain = splitLength(gap)) {
        return plain;
    }
    if (next && !next->tuplet().isNone()) {
        const Duration unit(next->value(), 0, next->tuplet());
        const Fraction count = gap / unit.length();
        if (count.denominator() == 1 && count.numerator() > 0 && count.numerator() <= 64) {
            return std::vector<Duration>(static_cast<std::size_t>(count.numerator()), unit);
        }
    }
    return std::nullopt;
}

/// The fermata a <fermata> writes by its shape (ADR-0085): angled ones are
/// short, square ones long, the others (an empty one too) normal.
Fermata fermataOf(pugi::xml_node fermata) {
    const std::string_view shape = fermata.child_value();
    if (shape == "angled" || shape == "double-angled") {
        return Fermata::Short;
    }
    if (shape == "square" || shape == "double-square") {
        return Fermata::Long;
    }
    return Fermata::Normal;
}

std::optional<Dynamic> dynamicOf(pugi::xml_node dynamics) {
    for (pugi::xml_node mark : dynamics.children()) {
        const std::string_view name = mark.name();
        if (name.starts_with("ppp")) {
            return Dynamic::PPP;
        }
        if (name.starts_with("fff")) {
            return Dynamic::FFF;
        }
        if (name == "pp") {
            return Dynamic::PP;
        }
        if (name == "p") {
            return Dynamic::P;
        }
        if (name == "mp") {
            return Dynamic::MP;
        }
        if (name == "mf") {
            return Dynamic::MF;
        }
        if (name == "f") {
            return Dynamic::F;
        }
        if (name == "ff") {
            return Dynamic::FF;
        }
    }
    return std::nullopt; // sf, sfz, fp...: accents rather than levels
}

/// Number at the start of text such as "100" or "c. 92".
std::optional<double> leadingNumber(std::string_view text) {
    const auto digit = text.find_first_of("0123456789");
    if (digit == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string number(text.substr(digit));
    return std::strtod(number.c_str(), nullptr);
}

/// The note sounded at `midi`, keeping the source spelling when only the
/// octave differs.
Pitch respell(const Pitch& source, int midi, const KeySignature& key) {
    const int difference = midi - source.midiNumber();
    if (difference % 12 == 0) {
        try {
            return Pitch(source.step(), source.alter(), source.octave() + difference / 12);
        } catch (const std::invalid_argument&) {
        }
    }
    return key.spell(midi);
}

void mergeEffects(NoteEffects& into, const NoteEffects& from) {
    if (into.bend.isEmpty()) {
        into.bend = from.bend;
    }
    if (into.harmonic.kind == HarmonicKind::None) {
        into.harmonic = from.harmonic;
    }
    into.tiedFromPrevious = into.tiedFromPrevious || from.tiedFromPrevious;
    into.legatoFromPrevious = into.legatoFromPrevious || from.legatoFromPrevious;
    if (into.slideIn == SlideIn::None) {
        into.slideIn = from.slideIn;
    }
    if (into.slideOut == SlideOut::None) {
        into.slideOut = from.slideOut;
    }
    if (into.vibrato == Vibrato::None) {
        into.vibrato = from.vibrato;
    }
    into.palmMute = into.palmMute || from.palmMute;
    into.deadNote = into.deadNote || from.deadNote;
    into.ghost = into.ghost || from.ghost;
    into.letRing = into.letRing || from.letRing;
    into.staccato = into.staccato || from.staccato;
    into.tenuto = into.tenuto || from.tenuto;
    if (into.accent == Accent::None) {
        into.accent = from.accent;
    }
}

// ---- Part list and part layout ------------------------------------------------

struct PartInfo {
    std::string id{};
    std::string name{};
    std::string instrumentName{}; // the first <score-instrument>; drum names on percussion
    int midiProgram = 0;          // 0-based
    std::vector<std::pair<std::string, int>> drumKeys{}; // instrument id -> GM key
    TrackMix mix{};
};

std::vector<PartInfo> readPartList(pugi::xml_node score) {
    std::vector<PartInfo> parts;
    for (pugi::xml_node part : score.child("part-list").children("score-part")) {
        PartInfo info;
        info.id = part.attribute("id").value();
        info.name = textOf(part.child("part-name"));
        if (info.name.empty()) {
            info.name = textOf(part.child("part-abbreviation"));
        }
        info.instrumentName = textOf(part.child("score-instrument").child("instrument-name"));
        bool programSet = false;
        bool volumeSet = false;
        bool panSet = false;
        for (pugi::xml_node midi : part.children("midi-instrument")) {
            const int program = midi.child("midi-program").text().as_int(0);
            if (!programSet && program >= 1 && program <= 128) {
                info.midiProgram = program - 1;
                programSet = true;
            }
            // The mix: the volume is a percentage, the pan in degrees from -90
            // (hard left) to 90 (hard right); beyond is behind the listener,
            // as far to the side as its mirror in front.
            if (const auto volume = decimalIn(midi.child("volume"), 0.0, 100.0);
                !volumeSet && volume) {
                info.mix.volume = *volume / 100.0;
                volumeSet = true;
            }
            if (auto pan = decimalIn(midi.child("pan"), -180.0, 180.0); !panSet && pan) {
                if (*pan > 90.0) {
                    *pan = 180.0 - *pan;
                } else if (*pan < -90.0) {
                    *pan = -180.0 - *pan;
                }
                info.mix.pan = *pan / 90.0;
                panSet = true;
            }
            const int key = midi.child("midi-unpitched").text().as_int(0);
            if (key >= 1 && key <= 128) {
                info.drumKeys.emplace_back(midi.attribute("id").value(), key - 1);
            }
        }
        parts.push_back(std::move(info));
    }
    return parts;
}

struct ClefInfo {
    std::string sign = "G";
    int line = 2;
    int octaveChange = 0;
    friend bool operator==(const ClefInfo&, const ClefInfo&) = default;
};

ClefInfo readClef(pugi::xml_node clef) {
    ClefInfo info;
    info.sign = textOf(clef.child("sign"));
    info.line = clef.child("line").text().as_int(info.sign == "F" ? 4 : 2);
    info.octaveChange = intIn(clef.child("clef-octave-change"), 0, -2, 2);
    return info;
}

struct Transposition {
    int chromatic = 0;
    int diatonic = 0;
    int octaveChange = 0;
    [[nodiscard]] int semitones() const noexcept { return chromatic + 12 * octaveChange; }
    [[nodiscard]] int steps() const noexcept { return diatonic + 7 * octaveChange; }
    friend bool operator==(const Transposition&, const Transposition&) = default;
};

Transposition readTranspose(pugi::xml_node transpose) {
    Transposition result;
    result.chromatic = intIn(transpose.child("chromatic"), 0, -24, 24);
    result.octaveChange = intIn(transpose.child("octave-change"), 0, -4, 4);
    const pugi::xml_node diatonic = transpose.child("diatonic");
    // Without <diatonic>, the nearest number of steps keeps the spelling sane.
    result.diatonic = diatonic ? intIn(diatonic, 0, -14, 14)
                               : static_cast<int>(std::lround(result.chromatic * 7.0 / 12.0));
    return result;
}

struct StaffDetails {
    std::optional<Tuning> tuning{};
    int capo = 0;
};

/// MusicXML numbers tuning lines from the bottom of the TAB staff (the lowest
/// string); the model numbers strings from the top.
std::optional<StaffDetails> readStaffDetails(pugi::xml_node details) {
    StaffDetails result;
    result.capo = details.child("capo").text().as_int(0);
    std::vector<std::pair<int, Pitch>> lines;
    for (pugi::xml_node tuning : details.children("staff-tuning")) {
        const auto step = stepOf(textOf(tuning.child("tuning-step")));
        if (!step) {
            continue;
        }
        const int alter = roundedIn(tuning.child("tuning-alter"), -2, 2);
        const int octave = intIn(tuning.child("tuning-octave"), 4, -1, 9);
        try {
            lines.emplace_back(tuning.attribute("line").as_int(0), Pitch(*step, alter, octave));
        } catch (const std::invalid_argument&) {
        }
    }
    std::ranges::sort(lines, std::less<>{}, &std::pair<int, Pitch>::first);
    const bool complete = !lines.empty() && std::cmp_less_equal(lines.size(), Tuning::kMaxStrings);
    bool numbered = complete;
    for (std::size_t i = 0; numbered && i < lines.size(); ++i) {
        numbered = lines[i].first == static_cast<int>(i) + 1;
    }
    if (numbered) {
        std::vector<Pitch> strings;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            strings.push_back(it->second);
        }
        result.tuning = Tuning(std::span<const Pitch>(strings));
    }
    if (!result.tuning && result.capo == 0) {
        return std::nullopt;
    }
    return result;
}

/// Where a MusicXML staff (1-based number) goes in the track.
struct StaffMap {
    std::size_t staff = 0; // index of the model staff
    bool tab = false;      // a TAB staff merged into that staff
};

struct PartLayout {
    std::vector<StaffMap> map{}; // index = MusicXML staff number - 1
    std::vector<Staff> staves{};
    Instrument instrument{};
    Transposition transposition{};
};

Clef clefFor(const ClefInfo& clef, const std::optional<Tuning>& tuning) {
    if (clef.sign == "G") {
        return clef.octaveChange == -1 ? Clef::Treble8vb : Clef::Treble;
    }
    if (clef.sign == "F") {
        return clef.octaveChange == -1 ? Clef::Bass8vb : Clef::Bass;
    }
    if (clef.sign == "C") {
        return clef.line == 4 ? Clef::Tenor : Clef::Alto;
    }
    if (clef.sign == "percussion") {
        return Clef::Percussion;
    }
    if (clef.sign == "TAB" && tuning) {
        int lowest = 127;
        for (int string = 1; string <= tuning->stringCount(); ++string) {
            lowest = std::min(lowest, tuning->openString(string).midiNumber());
        }
        return lowest < 36 ? Clef::Bass8vb : Clef::Treble8vb; // below C2: a bass
    }
    return clef.sign == "TAB" ? Clef::Treble8vb : Clef::Treble;
}

/// The clef a change to `clef` gives a staff whose own clef is `own`
/// (ADR-0087), and whether the change's octave differs from the staff's: an
/// octave clef's octave is in the instrument's written offset, the same for
/// the whole staff, so a change keeps the staff's. Nullopt for a change
/// Elara does not draw: to or from percussion or tablature.
struct ClefChange {
    std::optional<Clef> clef;
    bool octaveDropped = false;
};

ClefChange clefChange(const ClefInfo& clef, Clef own) {
    const bool octave = own == Clef::Treble8vb || own == Clef::Bass8vb;
    if (own == Clef::Percussion) {
        return {};
    }
    if (clef.sign == "G" || clef.sign == "F") {
        const bool treble = clef.sign == "G";
        return {.clef = octave ? (treble ? Clef::Treble8vb : Clef::Bass8vb)
                               : (treble ? Clef::Treble : Clef::Bass),
                .octaveDropped = clef.octaveChange != (octave ? -1 : 0)};
    }
    if (clef.sign == "C") {
        return {.clef = clef.line == 4 ? Clef::Tenor : Clef::Alto, .octaveDropped = false};
    }
    return {};
}

/// Reads the staff structure of a part before its notes: how many staves, which
/// are tablature, tunings, transposition and the kind of instrument.
PartLayout layoutPart(pugi::xml_node part, const PartInfo& info, Warnings& warnings) {
    int staffCount = 1;
    std::vector<std::optional<ClefInfo>> clefs;
    std::vector<std::optional<StaffDetails>> details;
    std::optional<Transposition> transposition;
    bool unpitched = false;
    const auto slot = [](auto& list, int number) -> auto& {
        if (std::cmp_less(list.size(), number)) {
            list.resize(static_cast<std::size_t>(number));
        }
        return list[static_cast<std::size_t>(number - 1)];
    };
    for (pugi::xml_node measure : part.children("measure")) {
        for (pugi::xml_node attributes : measure.children("attributes")) {
            staffCount = std::max(staffCount, attributes.child("staves").text().as_int(1));
            for (pugi::xml_node clef : attributes.children("clef")) {
                const int number = clef.attribute("number").as_int(1);
                if (number >= 1 && number <= 16) {
                    staffCount = std::max(staffCount, number);
                    auto& first = slot(clefs, number);
                    if (!first) {
                        first = readClef(clef);
                    }
                }
            }
            for (pugi::xml_node staffDetails : attributes.children("staff-details")) {
                const int number = staffDetails.attribute("number").as_int(1);
                if (number >= 1 && number <= 16) {
                    auto& first = slot(details, number);
                    if (!first) {
                        first = readStaffDetails(staffDetails);
                    }
                }
            }
            if (const pugi::xml_node transpose = attributes.child("transpose");
                transpose && !transposition) {
                transposition = readTranspose(transpose);
            }
        }
        if (!unpitched) {
            // pugixml iterators predate C++20 ranges: use the classic algorithms.
            const auto notes = measure.children("note");
            unpitched = std::any_of(notes.begin(), notes.end(), [](pugi::xml_node note) {
                return static_cast<bool>(note.child("unpitched"));
            });
        }
    }
    staffCount = std::clamp(staffCount, 1, 16);
    clefs.resize(static_cast<std::size_t>(staffCount));
    details.resize(static_cast<std::size_t>(staffCount));

    std::vector<int> standard;
    std::vector<int> tabs;
    bool percussion = unpitched;
    for (int number = 1; number <= staffCount; ++number) {
        const auto& clef = clefs[static_cast<std::size_t>(number - 1)];
        (clef && clef->sign == "TAB" ? tabs : standard).push_back(number);
        percussion = percussion || (clef && clef->sign == "percussion");
    }
    const bool tabOnly = standard.empty();
    const std::vector<int>& modelStaves = tabOnly ? tabs : standard;

    PartLayout layout;
    layout.transposition = transposition.value_or(Transposition{});
    layout.map.resize(static_cast<std::size_t>(staffCount));
    bool fretted = false;
    for (std::size_t index = 0; index < modelStaves.size(); ++index) {
        const auto number = static_cast<std::size_t>(modelStaves[index]);
        layout.map[number - 1] = StaffMap{.staff = index, .tab = false};
        // A merged TAB staff carries the tuning of the first standard staff.
        std::optional<StaffDetails> setup = details[number - 1];
        if (!tabOnly && index == 0 && !tabs.empty()) {
            if (const auto& tab = details[static_cast<std::size_t>(tabs.front() - 1)]; tab) {
                setup = tab;
            }
        }
        std::optional<Tuning> tuning;
        int capo = 0;
        if (setup && !percussion) {
            tuning = setup->tuning;
            capo = setup->capo;
            if (capo < 0 || capo > Tuning::kMaxFret) {
                warnings.add(info.name, "capo off the neck dropped", 0);
                capo = 0;
            }
            if (!tuning && capo != 0) {
                warnings.add(info.name, "capo without a tuning dropped", 0);
                capo = 0;
            }
        }
        fretted = fretted || tuning.has_value();
        const ClefInfo clef = clefs[number - 1].value_or(
            percussion ? ClefInfo{.sign = "percussion", .line = 3, .octaveChange = 0} : ClefInfo{});
        layout.staves.emplace_back(clefFor(clef, tuning), tuning, capo);
    }
    if (!tabOnly) {
        for (const int number : tabs) {
            layout.map[static_cast<std::size_t>(number - 1)] = StaffMap{.staff = 0, .tab = true};
        }
    }

    // The written offset makes notes sit where the source shows them, like the
    // presets: an octave clef (treble 8vb for guitar) means written +12.
    int writtenOffset = -layout.transposition.semitones();
    if (tabOnly) {
        writtenOffset = transposition ? writtenOffset : 12;
    } else if (const auto& clef = clefs[static_cast<std::size_t>(standard.front() - 1)];
               clef && (clef->sign == "G" || clef->sign == "F")) {
        writtenOffset -= 12 * clef->octaveChange;
    }
    InstrumentKind kind = InstrumentKind::Pitched;
    if (percussion) {
        kind = InstrumentKind::Percussion;
        writtenOffset = 0;
    } else if (fretted) {
        kind = InstrumentKind::Fretted;
    } else if (layout.staves.size() >= 2) {
        kind = InstrumentKind::Keyboard;
    }
    // A pitched part's <score-instrument> names its instrument; a drum part's
    // name its drums.
    const bool named = !percussion && !info.instrumentName.empty();
    layout.instrument = Instrument{.name = named ? info.instrumentName : info.name,
                                   .kind = kind,
                                   .midiProgram = percussion ? 0 : info.midiProgram,
                                   .writtenOffset = writtenOffset};
    return layout;
}

// ---- Measures -------------------------------------------------------------------

/// One <note> on its way into a voice. Onsets are from the start of the measure,
/// in whole notes.
/// A slur's start or stop on a note, by its number (ADR-0086).
struct SlurMark {
    int number = 1;
    bool start = false;
    bool grace = false; // on a grace note of the beat
};

struct Event {
    Fraction onset{};
    Fraction length{};    // from <duration>: what moves the cursor
    Fraction tolerance{}; // one division: rounding allowed between the two
    std::optional<Duration> written{};
    std::size_t staff = 0;
    bool tab = false;
    bool chord = false;
    bool measureRest = false;
    std::string voice{};
    std::optional<Note> note{}; // nullopt: a rest
    BeatEffects beatEffects{};
    std::vector<Grace> graces{};    // lead into the beat (ADR-0079)
    std::vector<Syllable> lyrics{}; // verse n + 1 at index n, ending with one sung (ADR-0084)
    std::vector<SlurMark> slurs{};  // its grace notes' first
};

/// Grace notes read before the note they lead into (ADR-0079).
struct PendingGrace {
    std::size_t staff = 0;
    bool tab = false;
    std::string voice{};
    Grace grace{};
    std::vector<SlurMark> slurs{};
};

/// A beat a slur starts or stops on (ADR-0086).
struct SlurEnd {
    std::size_t measure = 0;
    std::size_t staff = 0;
    std::size_t voice = 0;
    std::size_t beat = 0;
    bool grace = false;
};

struct DynamicMark {
    Fraction onset{};
    std::optional<std::size_t> staff{}; // nullopt: every staff
    Dynamic dynamic = Dynamic::MF;
};

struct TempoMark {
    Fraction onset{};
    std::optional<Tempo> tempo{}; // none: the tempo in force
    bool gradual = false;         // "accel.", "rit." (ADR-0082)
};

/// A chord symbol and where it goes (ADR-0080).
struct ChordMark {
    Fraction onset{};
    std::optional<std::size_t> staff{}; // nullopt: the first staff
    ChordSymbol symbol{};
};

/// A direction's words and where they go (ADR-0081).
struct TextMark {
    Fraction onset{};
    std::optional<std::size_t> staff{}; // nullopt: the first staff
    std::string text{};
};

/// A clef change read in a measure, where its <attributes> stood (ADR-0087).
struct ClefMark {
    std::size_t staff = 0;
    Fraction onset{};
    Clef clef = Clef::Treble;
};

/// The beat of `measure` that starts at `onset`, the voices looked at in
/// order; none when every voice is between beats there.
Beat* beatStartingAt(Measure& measure, const Fraction& onset) {
    for (Voice& voice : measure.voices) {
        Fraction at;
        for (Beat& beat : voice.beats) {
            if (at == onset) {
                return &beat;
            }
            at += beat.duration.length();
        }
    }
    return nullptr;
}

/// Sharps or flats for an alteration in semitones.
std::string alterText(double alter) {
    const long semitones = std::clamp(std::lround(alter), -2L, 2L);
    return std::string(static_cast<std::size_t>(std::abs(semitones)), semitones > 0 ? '#' : 'b');
}

/// A pitch of a <root> or <bass> as a chord name spells it, or the text the
/// file gives to print instead.
std::string chordStep(pugi::xml_node step, pugi::xml_node alter) {
    if (const pugi::xml_attribute text = step.attribute("text")) {
        return text.value();
    }
    return textOf(step) + alterText(alter.text().as_double(0.0));
}

/// The name of a <harmony> as a chord symbol writes it: root, kind, degrees
/// and bass, with the texts the file gives to print. None for a chord written
/// as a Roman numeral or a function.
std::optional<std::string> harmonyName(pugi::xml_node harmony) {
    const pugi::xml_node kind = harmony.child("kind");
    const std::string_view kindValue = textView(kind);
    const pugi::xml_attribute kindText = kind.attribute("text");
    if (kindValue == "none") {
        return kindText ? std::string(kindText.value()) : std::string("N.C.");
    }
    const pugi::xml_node root = harmony.child("root");
    if (!root) {
        return std::nullopt;
    }
    std::string name = chordStep(root.child("root-step"), root.child("root-alter"));
    if (kindText) {
        name += kindText.value();
    } else if (const auto known =
                   std::ranges::find(detail::kChordKinds, kindValue, &detail::ChordKind::kind);
               known != detail::kChordKinds.end()) {
        name += known->suffix;
    }
    for (const pugi::xml_node degree : harmony.children("degree")) {
        if (std::string_view(degree.attribute("print-object").value()) == "no") {
            continue;
        }
        const pugi::xml_node type = degree.child("degree-type");
        const std::string_view change = textView(type);
        if (const pugi::xml_attribute text = type.attribute("text")) {
            name += text.value();
        } else if (change == "add") {
            name += "add";
        } else if (change == "subtract") {
            name += "no";
        }
        name += alterText(degree.child("degree-alter").text().as_double(0.0));
        name += textView(degree.child("degree-value"));
    }
    if (const pugi::xml_node bass = harmony.child("bass")) {
        name += '/';
        name += chordStep(bass.child("bass-step"), bass.child("bass-alter"));
    }
    return name;
}

/// A <frame>'s diagram: string 1 is the highest, and strings without a
/// <frame-note> are not played. It has the staff's `strings`, or on a staff
/// without a tuning (0) as many as the frame has.
std::optional<ChordDiagram> frameDiagram(pugi::xml_node frame, int strings) {
    if (!frame) {
        return std::nullopt;
    }
    const int count =
        strings > 0 ? strings : intIn(frame.child("frame-strings"), 6, 1, Tuning::kMaxStrings);
    ChordDiagram diagram{
        .firstFret =
            static_cast<std::uint8_t>(intIn(frame.child("first-fret"), 1, 1, Tuning::kMaxFret)),
        .frets = std::vector<std::int8_t>(static_cast<std::size_t>(count), ChordDiagram::kMuted)};
    bool played = false;
    for (const pugi::xml_node note : frame.children("frame-note")) {
        const int string = note.child("string").text().as_int(0);
        const int fret = note.child("fret").text().as_int(-1);
        if (string >= 1 && string <= count && fret >= 0 && fret <= Tuning::kMaxFret) {
            diagram.frets[static_cast<std::size_t>(string - 1)] = static_cast<std::int8_t>(fret);
            played = true;
        }
    }
    return played ? std::optional(std::move(diagram)) : std::nullopt;
}

/// Where a wedge (ADR-0078) or an octave line (ADR-0089) starts, or stops
/// (None).
struct SpanMark {
    Fraction onset{};
    std::optional<std::size_t> staff{}; // nullopt: every staff
    std::variant<Hairpin, Ottava> value{};
};

struct Bend {
    int cents = 0;
    bool preBend = false;
    bool release = false;
};

BendCurve bendCurveOf(const std::vector<Bend>& bends) {
    const Bend& first = bends.front();
    const bool released = std::ranges::any_of(bends.begin() + 1, bends.end(),
                                              [](const Bend& bend) { return bend.release; });
    const int cents = std::min(std::abs(first.cents), BendCurve::kMaxCents);
    if (cents == 0) {
        return {};
    }
    if (first.preBend) {
        return released ? BendCurve::preBendRelease(cents) : BendCurve::preBend(cents);
    }
    if (first.release) { // released from a bend held over from the previous note
        return BendCurve::preBendRelease(cents);
    }
    return released ? BendCurve::bendRelease(cents) : BendCurve::bend(cents);
}

/// The signs and jumps one <direction>, <sound> or <barline> sets (ADR-0049).
struct Navigation {
    int segno = 0;  ///< 1: segno, 2: segno segno
    int coda = 0;   ///< 1: coda, 2: double coda
    int toCoda = 0; ///< 1: To Coda, 2: To Double Coda
    bool fine = false;
    JumpTo jump = JumpTo::None;
    std::optional<JumpThen> then{}; ///< empty: not said (a <sound> alone)

    /// Takes what `other` sets, keeping this one's ending of the same jump.
    void merge(const Navigation& other) {
        segno = other.segno != 0 ? other.segno : segno;
        coda = other.coda != 0 ? other.coda : coda;
        toCoda = other.toCoda != 0 ? other.toCoda : toCoda;
        fine = fine || other.fine;
        if (other.jump != JumpTo::None) {
            then = other.then ? other.then : (other.jump == jump ? then : std::nullopt);
            jump = other.jump;
        }
    }
};

/// 2 for the second sign of a kind: the `segno`, `coda`, `tocoda` and
/// `dalsegno` attributes name their sign, and Elara writes "segno-segno" and
/// "double-coda" for the second ones.
int signKind(std::string_view id, std::string_view second) {
    return id == second ? 2 : 1;
}

/// What the <sound> attributes of a direction say about the play order.
Navigation soundNavigation(pugi::xml_node sound) {
    Navigation navigation;
    if (const pugi::xml_attribute segno = sound.attribute("segno")) {
        navigation.segno = signKind(segno.value(), "segno-segno");
    }
    if (const pugi::xml_attribute coda = sound.attribute("coda")) {
        navigation.coda = signKind(coda.value(), "double-coda");
    }
    if (const pugi::xml_attribute toCoda = sound.attribute("tocoda")) {
        navigation.toCoda = signKind(toCoda.value(), "double-coda");
    }
    navigation.fine = static_cast<bool>(sound.attribute("fine"));
    if (sound.attribute("dacapo").as_bool()) {
        navigation.jump = JumpTo::Start;
    }
    if (const pugi::xml_attribute segno = sound.attribute("dalsegno")) {
        navigation.jump =
            signKind(segno.value(), "segno-segno") == 2 ? JumpTo::SegnoSegno : JumpTo::Segno;
    }
    return navigation;
}

/// The text's letters in lower case, without spaces, dots or other signs.
std::string lowerLetters(std::string_view text) {
    std::string letters;
    for (const char c : text) {
        const auto lower = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (lower >= 'a' && lower <= 'z') {
            letters += lower;
        }
    }
    return letters;
}

/// Whether the words ask for a gradual tempo change ("accel.", "rit.", "poco
/// rall."; ADR-0082), read letter by letter without case.
bool rampWords(std::string_view text) {
    std::string letters = lowerLetters(text);
    for (const std::string_view modifier : {"pocoapoco", "poco", "molto"}) {
        if (letters.starts_with(modifier)) {
            letters.erase(0, modifier.size());
            break;
        }
    }
    constexpr std::array<std::string_view, 10> kRamps{
        "accel",      "accelerando", "stringendo",  "rit",    "ritard",
        "ritardando", "rall",        "rallentando", "allarg", "allargando"};
    return std::ranges::find(kRamps, letters) != kRamps.end();
}

/// Words that end a ramp without a tempo of their own.
bool rampEndWords(std::string_view text) {
    const std::string letters = lowerLetters(text);
    return letters.starts_with("atempo") || letters == "tempoi" || letters == "tempoprimo" ||
           rampWords(text);
}

/// Whether a tempo is set after `direction` in its part, within a few measures and
/// before words that end the ramp: then words there start a ramp to it (ADR-0082).
bool tempoFollows(pugi::xml_node direction) {
    constexpr int kLookahead = 64; // measures; bounds the work on damaged files
    pugi::xml_node node = direction.next_sibling();
    pugi::xml_node measure = direction.parent();
    for (int measures = 0; measures <= kLookahead; ++measures) {
        for (; node; node = node.next_sibling()) {
            const std::string_view name = node.name();
            if (name == "sound" && node.attribute("tempo")) {
                return true;
            }
            if (name != "direction") {
                continue;
            }
            if (node.child("sound").attribute("tempo")) {
                return true;
            }
            std::string words;
            for (pugi::xml_node type : node.children("direction-type")) {
                if (type.child("metronome")) {
                    return true;
                }
                for (pugi::xml_node word : type.children("words")) {
                    words += word.child_value();
                }
            }
            if (rampEndWords(words)) {
                return false;
            }
        }
        measure = measure.next_sibling("measure");
        if (!measure) {
            return false;
        }
        node = measure.first_child();
    }
    return false;
}

/// The usual navigation texts ("D.S. al Coda", "Da Capo", "Fine", "To Coda"),
/// read letter by letter without case, or empty for any other text.
std::optional<Navigation> navigationWords(std::string_view text) {
    const std::string letters = lowerLetters(text);
    if (letters == "fine") {
        return Navigation{.fine = true};
    }
    if (letters == "tocoda" || letters == "todoublecoda") {
        return Navigation{.toCoda = letters == "tocoda" ? 1 : 2};
    }
    // The longer prefix first: "dss" also starts with "ds".
    constexpr std::array<std::pair<std::string_view, JumpTo>, 6> kTargets{{
        {"dalsegnosegno", JumpTo::SegnoSegno},
        {"dss", JumpTo::SegnoSegno},
        {"dalsegno", JumpTo::Segno},
        {"ds", JumpTo::Segno},
        {"dacapo", JumpTo::Start},
        {"dc", JumpTo::Start},
    }};
    constexpr std::array<std::pair<std::string_view, JumpThen>, 4> kEndings{{
        {"", JumpThen::End},
        {"alfine", JumpThen::Fine},
        {"alcoda", JumpThen::Coda},
        {"aldoublecoda", JumpThen::DoubleCoda},
    }};
    const std::string_view all = letters;
    for (const auto& [prefix, to] : kTargets) {
        if (!all.starts_with(prefix)) {
            continue;
        }
        for (const auto& [suffix, then] : kEndings) {
            if (all.substr(prefix.size()) == suffix) {
                return Navigation{.jump = to, .then = then};
            }
        }
    }
    return std::nullopt;
}

/// Song-wide results the first part writes and the other parts only read.
struct ScoreState {
    std::vector<MeasureHeader> headers{};
    std::optional<Tempo> startTempo{};
};

/// The children of a <note>, found in one pass: every pugixml child() lookup is
/// a linear scan, and a note has a dozen kinds of children.
struct NoteParts {
    pugi::xml_node pitch{};
    pugi::xml_node unpitched{};
    pugi::xml_node rest{};
    pugi::xml_node duration{};
    pugi::xml_node type{};
    pugi::xml_node timeModification{};
    pugi::xml_node staff{};
    pugi::xml_node voice{};
    pugi::xml_node notehead{};
    pugi::xml_node instrument{};
    pugi::xml_node notations{}; // the first; more may follow as siblings
    int dots = 0;
    pugi::xml_node grace{};
    bool chord = false;
    bool cue = false;
    bool tieStop = false;
    bool palmMute = false;
    pugi::xml_node lyric{}; // the first; more may follow as siblings
};

NoteParts scanNote(pugi::xml_node note) {
    NoteParts parts;
    for (pugi::xml_node child : note.children()) {
        const std::string_view name = child.name();
        if (name == "pitch") {
            parts.pitch = child;
        } else if (name == "duration") {
            parts.duration = child;
        } else if (name == "voice") {
            parts.voice = child;
        } else if (name == "type") {
            parts.type = child;
        } else if (name == "staff") {
            parts.staff = child;
        } else if (name == "notations") {
            if (!parts.notations) {
                parts.notations = child;
            }
        } else if (name == "chord") {
            parts.chord = true;
        } else if (name == "rest") {
            parts.rest = child;
        } else if (name == "dot") {
            ++parts.dots;
        } else if (name == "time-modification") {
            parts.timeModification = child;
        } else if (name == "tie") {
            parts.tieStop = parts.tieStop || isType(child, "stop");
        } else if (name == "unpitched") {
            parts.unpitched = child;
        } else if (name == "notehead") {
            parts.notehead = child;
        } else if (name == "instrument") {
            parts.instrument = child;
        } else if (name == "grace") {
            parts.grace = child;
        } else if (name == "cue") {
            parts.cue = true;
        } else if (name == "lyric") {
            if (!parts.lyric) {
                parts.lyric = child;
            }
        } else if (name == "play") {
            parts.palmMute = parts.palmMute || textView(child.child("mute")) == "palm";
        }
    }
    return parts;
}

/// Reads one <part> into the measures of one track.
class PartReader {
public:
    PartReader(const PartInfo& info, PartLayout layout, bool first, ScoreState& score,
               Warnings& warnings)
        : info_(info), layout_(std::move(layout)), first_(first), score_(score),
          warnings_(warnings), voiceIds_(layout_.staves.size()),
          dynamics_(layout_.staves.size(), Dynamic::MF),
          hairpins_(layout_.staves.size(), Hairpin::None),
          ottavas_(layout_.staves.size(), Ottava::None), measures_(layout_.staves.size()) {
        nextClefs_.resize(layout_.staves.size());
        clefs_.reserve(layout_.staves.size());
        for (const Staff& staff : layout_.staves) {
            clefs_.push_back(staff.clef());
        }
    }

    void read(pugi::xml_node part) {
        const auto measures = part.children("measure");
        const auto count =
            static_cast<std::size_t>(std::distance(measures.begin(), measures.end()));
        for (auto& staff : measures_) {
            staff.reserve(count);
        }
        std::size_t index = 0;
        for (pugi::xml_node measure : part.children("measure")) {
            if (!first_ && index >= score_.headers.size()) {
                warn("measures beyond the first part dropped", index);
                break;
            }
            readMeasure(measure, index);
            ++index;
        }
        if (first_) {
            endOpenJumps();
        }
        endSlurs();
    }

    [[nodiscard]] Track takeTrack() {
        Track track(info_.name, layout_.instrument, std::move(layout_.staves));
        track.setMix(info_.mix);
        return track;
    }

    /// Measures per staff, in order; fewer than the song's when the part is short.
    [[nodiscard]] std::vector<std::vector<Measure>>& measures() noexcept { return measures_; }

private:
    void warn(std::string_view what, std::size_t measure) {
        warnings_.add(info_.name, what, measure);
    }

    [[nodiscard]] Fraction divisionsToWholes(int divisions) const {
        return Fraction(divisions, 4 * static_cast<std::int64_t>(divisions_));
    }

    [[nodiscard]] StaffMap staffFor(int number) const {
        if (number < 1 || std::cmp_greater(number, layout_.map.size())) {
            return layout_.map.front();
        }
        return layout_.map[static_cast<std::size_t>(number - 1)];
    }

    [[nodiscard]] bool isPercussion() const noexcept {
        return layout_.instrument.kind == InstrumentKind::Percussion;
    }

    void readMeasure(pugi::xml_node measure, std::size_t index) {
        MeasureHeader header;
        if (first_) {
            header = index == 0 ? MeasureHeader{} : score_.headers.back().continuation();
            concertKey_ = header.keySignature;
        } else {
            concertKey_ = score_.headers[index].keySignature;
        }
        jumpEndOpen_ = false;

        // Buffers reused from measure to measure.
        std::vector<Event>& events = events_;
        std::vector<DynamicMark>& dynamics = dynamicMarks_;
        std::vector<SpanMark>& hairpins = hairpinMarks_;
        std::vector<TempoMark>& tempos = tempoMarks_;
        std::vector<ChordMark>& chords = chordMarks_;
        std::vector<TextMark>& texts = textMarks_;
        clefMarks_.clear();
        events.clear();
        dynamics.clear();
        hairpins.clear();
        chords.clear();
        texts.clear();
        tempos.clear();
        Fraction cursor;
        Fraction lastOnset;
        Fraction end;
        for (pugi::xml_node child : measure.children()) {
            const std::string_view name = child.name();
            if (name == "attributes") {
                readAttributes(child, index, cursor, first_ ? &header : nullptr);
            } else if (name == "note") {
                const NoteParts parts = scanNote(child);
                if (parts.grace) {
                    readGrace(parts, cursor, index, dynamics);
                    continue;
                }
                const Fraction length = divisionsToWholes(parts.duration.text().as_int(0));
                const bool chord = parts.chord;
                const Fraction onset = chord ? lastOnset : cursor;
                if (!chord) {
                    lastOnset = cursor;
                    cursor += length;
                    end = std::max(end, cursor);
                }
                if (parts.cue) {
                    warn("cue notes dropped", index);
                    continue;
                }
                if (auto event = readNote(parts, onset, length, index, dynamics)) {
                    if (!chord) {
                        takeGraces(*event);
                    }
                    events.push_back(std::move(*event));
                }
            } else if (name == "backup") {
                cursor -= divisionsToWholes(child.child("duration").text().as_int(0));
                cursor = std::max(cursor, Fraction(0));
            } else if (name == "forward") {
                cursor += divisionsToWholes(child.child("duration").text().as_int(0));
                end = std::max(end, cursor);
            } else if (name == "direction") {
                readDirection(child, cursor, index, dynamics, hairpins, tempos, texts,
                              first_ ? &header : nullptr);
            } else if (name == "sound") {
                if (first_) {
                    navigate(header, soundNavigation(child), index);
                }
                if (auto tempo = readSound(child, index)) {
                    tempos.push_back(TempoMark{.onset = cursor, .tempo = *tempo});
                }
            } else if (name == "harmony") {
                readHarmony(child, cursor, index, chords);
            } else if (name == "figured-bass") {
                warn("figured bass dropped", index);
            } else if (name == "barline") {
                readBarline(child, index, header);
            }
        }

        if (!pendingGraces_.empty()) {
            warn("grace notes after the last note of a voice dropped", index);
            pendingGraces_.clear();
        }
        const TimeSignature time =
            first_ ? header.timeSignature : score_.headers[index].timeSignature;
        const Fraction measureLength = time.measureLength();
        if (measure.attribute("implicit").as_bool() && end > Fraction(0) && end < measureLength) {
            if (index == 0) {
                // The model has no partial measures: the pickup ends the measure.
                const Fraction shift = measureLength - end;
                for (Event& event : events) {
                    event.onset += shift;
                }
                for (DynamicMark& mark : dynamics) {
                    mark.onset += shift;
                }
                for (SpanMark& mark : hairpins) {
                    mark.onset += shift;
                }
                for (ChordMark& mark : chords) {
                    mark.onset += shift;
                }
                for (TextMark& mark : texts) {
                    mark.onset += shift;
                }
                for (TempoMark& mark : tempos) {
                    mark.onset += shift;
                }
                if (first_) {
                    warnings_.add({}, "pickup measure filled with leading rests", index);
                }
            } else if (first_) {
                warnings_.add({}, "short measure filled with rests", index);
            }
        }

        if (first_) {
            std::ranges::stable_sort(tempos, std::less<>{}, &TempoMark::onset);
            auto& changes = header.tempoChanges;
            for (const TempoMark& mark : tempos) {
                if (mark.onset >= measureLength) {
                    continue;
                }
                if (index == 0 && mark.onset.isZero()) {
                    // The start tempo; a ramp from the start is a change there.
                    if (mark.tempo) {
                        score_.startTempo = *mark.tempo;
                    }
                    const bool atStart = !changes.empty() && changes.front().offset.isZero();
                    if (atStart) {
                        changes.front().tempo = score_.startTempo.value_or(Tempo{});
                        changes.front().gradual = changes.front().gradual || mark.gradual;
                    } else if (mark.gradual) {
                        changes.insert(changes.begin(),
                                       TempoChange{.tempo = score_.startTempo.value_or(Tempo{}),
                                                   .gradual = true});
                    }
                    continue;
                }
                if (!changes.empty() && changes.back().offset == mark.onset) {
                    if (mark.tempo) {
                        changes.back().tempo = *mark.tempo;
                    }
                    changes.back().gradual = changes.back().gradual || mark.gradual;
                } else {
                    changes.push_back(
                        TempoChange{.offset = mark.onset,
                                    .tempo = mark.tempo.value_or(tempoInForce(changes)),
                                    .gradual = mark.gradual});
                }
            }
            // An alternate ending spans the measures from its start to its stop.
            header.alternateEndings = ending_;
            if (endingStops_) {
                ending_ = 0;
                endingStops_ = false;
            }
            if (header.jump != JumpTo::None && jumpEndOpen_) {
                openJumps_.push_back(index);
            }
            score_.headers.push_back(std::move(header));
        }

        build(index, events, dynamics, hairpins, time);
        markChords(index, chords);
        markTexts(index, texts);
        markClefs(index, events, time.measureLength());
    }

    /// The clef changes read in measure `index` (ADR-0087): one at its start
    /// is its own; one later moves to the next measure, with a warning when
    /// notes of its staff follow it in this one.
    void markClefs(std::size_t index, const std::vector<Event>& events,
                   const Fraction& measureLength) {
        for (std::size_t s = 0; s < measures_.size(); ++s) {
            if (nextClefs_[s] && !measures_[s].empty()) {
                measures_[s].back().clef = std::exchange(nextClefs_[s], std::nullopt);
            }
        }
        for (const ClefMark& mark : clefMarks_) {
            if (mark.onset.isZero()) {
                measures_[mark.staff].back().clef = mark.clef;
                continue;
            }
            const bool notesAfter = std::ranges::any_of(events, [&](const Event& event) {
                return event.staff == mark.staff && !event.tab && event.onset >= mark.onset &&
                       event.onset < measureLength;
            });
            if (notesAfter) {
                warn("clef changes inside a measure moved to the next measure", index);
            }
            nextClefs_[mark.staff] = mark.clef;
        }
    }

    /// A chord symbol, with a diagram from its <frame> (ADR-0080).
    void readHarmony(pugi::xml_node harmony, const Fraction& cursor, std::size_t index,
                     std::vector<ChordMark>& chords) {
        const std::optional<std::string> name = harmonyName(harmony);
        if (!name) {
            warn("chord symbols written as functions dropped", index);
            return;
        }
        std::optional<std::size_t> staff;
        if (const pugi::xml_node number = harmony.child("staff")) {
            staff = staffFor(number.text().as_int(1)).staff;
        }
        const Staff& target = layout_.staves[staff.value_or(0)];
        ChordSymbol symbol{.name = *name, .diagram = {}};
        if (!isPercussion()) {
            symbol.diagram = frameDiagram(harmony.child("frame"),
                                          target.tuning() ? target.tuning()->stringCount() : 0);
        }
        if (symbol.name.empty() && !symbol.diagram) {
            return;
        }
        const Fraction offset = divisionsToWholes(harmony.child("offset").text().as_int(0));
        chords.push_back(ChordMark{.onset = std::max(Fraction(0), cursor + offset),
                                   .staff = staff,
                                   .symbol = std::move(symbol)});
    }

    /// Each chord symbol goes on the beat of its staff that starts where it
    /// does, the voices looked at in order.
    void markChords(std::size_t index, std::vector<ChordMark>& chords) {
        for (ChordMark& mark : chords) {
            Beat* target = beatStartingAt(measures_[mark.staff.value_or(0)].back(), mark.onset);
            if (target == nullptr) {
                warn("chord symbols between beats dropped", index);
                continue;
            }
            target->chord = chordPool_.share(std::move(mark.symbol));
        }
    }

    /// Each direction's words become the text of the beat of its staff that
    /// starts where it does; two on one beat join with a space (ADR-0081).
    void markTexts(std::size_t index, std::vector<TextMark>& texts) {
        for (TextMark& mark : texts) {
            Beat* target = beatStartingAt(measures_[mark.staff.value_or(0)].back(), mark.onset);
            if (target == nullptr) {
                warn("text directions dropped", index);
                continue;
            }
            if (target->text) {
                mark.text = *target->text + ' ' + mark.text;
            }
            target->text = SharedOptional(std::move(mark.text));
        }
    }

    void readAttributes(pugi::xml_node attributes, std::size_t index, const Fraction& cursor,
                        MeasureHeader* header) {
        if (const pugi::xml_node divisions = attributes.child("divisions")) {
            const int value = divisions.text().as_int(0);
            divisions_ = value > 0 ? value : 1;
            oneDivision_ = divisionsToWholes(1);
        }
        if (header != nullptr) {
            if (const pugi::xml_node key = attributes.child("key")) {
                readKey(key, index, *header);
            }
            if (const pugi::xml_node time = attributes.child("time")) {
                readTime(time, index, *header);
            }
            concertKey_ = header->keySignature;
        }
        for (pugi::xml_node clef : attributes.children("clef")) {
            const auto number =
                static_cast<std::size_t>(std::clamp(clef.attribute("number").as_int(1), 1, 16));
            if (lastClefs_.size() < number) {
                lastClefs_.resize(number);
            }
            const ClefInfo info = readClef(clef);
            const std::optional<ClefInfo> previous = std::exchange(lastClefs_[number - 1], info);
            const StaffMap map = staffFor(static_cast<int>(number));
            if (!previous || *previous == info || map.tab || info.sign == "TAB") {
                continue; // the staff's own clef, the same again, or a view of the notes
            }
            const ClefChange change = clefChange(info, layout_.staves[map.staff].clef());
            if (!change.clef) {
                warn("clef changes dropped", index);
                continue;
            }
            if (change.octaveDropped) {
                warn("clef octave changes dropped", index);
            }
            if (*change.clef != clefs_[map.staff]) {
                clefs_[map.staff] = *change.clef;
                clefMarks_.push_back(
                    ClefMark{.staff = map.staff, .onset = cursor, .clef = *change.clef});
            }
        }
        if (const pugi::xml_node transpose = attributes.child("transpose");
            transpose && readTranspose(transpose) != layout_.transposition) {
            warn("transposition changes dropped", index);
        }
    }

    void readKey(pugi::xml_node key, std::size_t index, MeasureHeader& header) {
        const pugi::xml_node fifths = key.child("fifths");
        if (!fifths) {
            warn("non-traditional key signatures dropped", index);
            return;
        }
        const std::string mode = textOf(key.child("mode"));
        if (!mode.empty() && mode != "major" && mode != "minor" && mode != "none") {
            warn("modes other than major and minor read as major", index);
        }
        const int written = fifths.text().as_int(0);
        if (written < -KeySignature::kMaxFifths || written > KeySignature::kMaxFifths) {
            warn("key signatures with more than seven accidentals dropped", index);
            return;
        }
        header.keySignature =
            KeySignature(transposedFifths(written, layout_.transposition.chromatic,
                                          layout_.transposition.diatonic),
                         mode == "minor" ? KeyMode::Minor : KeyMode::Major);
    }

    void readTime(pugi::xml_node time, std::size_t index, MeasureHeader& header) {
        if (time.child("senza-misura")) {
            warn("unmeasured time dropped", index);
            return;
        }
        const auto beatsElements = time.children("beats");
        if (std::distance(beatsElements.begin(), beatsElements.end()) > 1) {
            warn("composite time signatures read as their first part", index);
        }
        int numerator = 0;
        const std::string beats = textOf(time.child("beats"));
        for (std::size_t start = 0; start <= beats.size();) {
            const std::size_t plus = std::min(beats.find('+', start), beats.size());
            int value = 0;
            const std::string_view part = trim(std::string_view(beats).substr(start, plus - start));
            (void)std::from_chars(part.data(), part.data() + part.size(), value);
            numerator = std::min(numerator + std::clamp(value, 0, 1000), 1000);
            start = plus + 1;
        }
        try {
            header.timeSignature =
                TimeSignature(numerator, time.child("beat-type").text().as_int(0));
        } catch (const std::invalid_argument&) {
            warn("unsupported time signatures dropped", index);
        }
    }

    std::optional<Tempo> readSound(pugi::xml_node sound, std::size_t index) {
        if (const pugi::xml_attribute tempo = sound.attribute("tempo")) {
            return makeTempo(tempo.as_double(0.0), Duration{}, index);
        }
        return std::nullopt;
    }

    std::optional<Tempo> makeTempo(double bpm, Duration unit, std::size_t index) {
        try {
            return Tempo(bpm, unit);
        } catch (const std::invalid_argument&) {
            warn("tempos outside 10-999 BPM dropped", index);
            return std::nullopt;
        }
    }

    std::optional<Tempo> readMetronome(pugi::xml_node metronome, std::size_t index) {
        const auto value = noteValueOf(textOf(metronome.child("beat-unit")));
        const auto perMinute = leadingNumber(textOf(metronome.child("per-minute")));
        if (!value || !perMinute) {
            return std::nullopt; // metric modulation or text only
        }
        const auto dots = metronome.children("beat-unit-dot");
        const int dotCount =
            std::min(static_cast<int>(std::distance(dots.begin(), dots.end())), Duration::kMaxDots);
        return makeTempo(*perMinute, Duration(*value, dotCount), index);
    }

    /// The tempo at the end of the measure read so far: its last change, else
    /// the last change of the measures before, else the start tempo.
    [[nodiscard]] Tempo tempoInForce(const std::vector<TempoChange>& changes) const {
        if (!changes.empty()) {
            return changes.back().tempo;
        }
        for (auto header = score_.headers.rbegin(); header != score_.headers.rend(); ++header) {
            if (!header->tempoChanges.empty()) {
                return header->tempoChanges.back().tempo;
            }
        }
        return score_.startTempo.value_or(Tempo{});
    }

    // `header`: the first part's measure, which takes the signs and jumps.
    void readDirection(pugi::xml_node direction, const Fraction& cursor, std::size_t index,
                       std::vector<DynamicMark>& dynamics, std::vector<SpanMark>& hairpins,
                       std::vector<TempoMark>& tempos, std::vector<TextMark>& texts,
                       MeasureHeader* header) {
        const Fraction onset = std::max(
            Fraction(0), cursor + divisionsToWholes(direction.child("offset").text().as_int(0)));
        std::optional<std::size_t> staff;
        if (const pugi::xml_node number = direction.child("staff")) {
            staff = staffFor(number.text().as_int(1)).staff;
        }
        std::optional<Tempo> tempo;
        std::string words;
        std::string text; // the words as written, spaces between runs kept
        bool segnoGlyph = false;
        bool codaGlyph = false;
        for (pugi::xml_node type : direction.children("direction-type")) {
            for (pugi::xml_node mark : type.children()) {
                const std::string_view name = mark.name();
                if (name == "dynamics") {
                    if (const auto dynamic = dynamicOf(mark)) {
                        dynamics.push_back(
                            DynamicMark{.onset = onset, .staff = staff, .dynamic = *dynamic});
                    }
                } else if (name == "metronome") {
                    tempo = readMetronome(mark, index);
                } else if (name == "words") {
                    words += textView(mark);
                    text += mark.child_value();
                } else if (name == "rehearsal") {
                    // The section that starts here (ADR-0062); the first part's
                    // marks name them, as they hold the signs.
                    if (header != nullptr && header->section.empty()) {
                        header->section = textOf(mark);
                    }
                } else if (name == "segno") {
                    segnoGlyph = true;
                } else if (name == "coda") {
                    codaGlyph = true;
                } else if (name == "wedge") {
                    // From its start to its stop (ADR-0078); "continue" changes nothing.
                    const Hairpin hairpin = isType(mark, "crescendo")    ? Hairpin::Crescendo
                                            : isType(mark, "diminuendo") ? Hairpin::Diminuendo
                                                                         : Hairpin::None;
                    if (hairpin != Hairpin::None || isType(mark, "stop")) {
                        hairpins.push_back(
                            SpanMark{.onset = onset, .staff = staff, .value = hairpin});
                    }
                } else if (name == "octave-shift") {
                    // From its start to its stop (ADR-0089): "down" writes the
                    // notes an octave lower than they sound (8va), size 15 two.
                    const int size = mark.attribute("size").as_int(8);
                    Ottava ottava = Ottava::None;
                    if (isType(mark, "down")) {
                        ottava = size == 15 ? Ottava::QuindicesimaAlta : Ottava::Alta;
                    } else if (isType(mark, "up")) {
                        ottava = size == 15 ? Ottava::QuindicesimaBassa : Ottava::Bassa;
                    }
                    if (size != 8 && size != 15 && ottava != Ottava::None) {
                        warn("octave lines of three octaves dropped", index);
                    } else if (ottava != Ottava::None || isType(mark, "stop")) {
                        hairpins.push_back(
                            SpanMark{.onset = onset, .staff = staff, .value = ottava});
                    }
                } else if (name == "pedal" && isType(mark, "start")) {
                    warn("pedal marks dropped", index);
                }
            }
        }
        const std::optional<Navigation> fromWords = navigationWords(words);
        // Words of a ramp become the gradual change (ADR-0082); with no tempo to
        // go to, they stay the text of the beat.
        const bool ramp = !fromWords && rampWords(words) && tempoFollows(direction);
        if (!fromWords && !ramp && !trim(text).empty()) {
            texts.push_back(
                TextMark{.onset = onset, .staff = staff, .text = std::string(trim(text))});
        }
        Navigation navigation = fromWords.value_or(Navigation{});
        if (const pugi::xml_node sound = direction.child("sound")) {
            navigation.merge(soundNavigation(sound));
            const auto soundTempo = readSound(sound, index);
            if (!tempo) {
                tempo = soundTempo;
            }
        }
        if (tempo || ramp) {
            tempos.push_back(TempoMark{.onset = onset, .tempo = tempo, .gradual = ramp});
        }
        if (header != nullptr) {
            // A glyph alone is a sign; next to a jump or "To Coda" it decorates it.
            const bool jumps = navigation.jump != JumpTo::None || navigation.toCoda != 0;
            if (segnoGlyph && navigation.segno == 0 && !jumps) {
                navigation.segno = 1;
            }
            if (codaGlyph && navigation.coda == 0 && !jumps) {
                navigation.coda = 1;
            }
            navigate(*header, navigation, index);
        }
    }

    void navigate(MeasureHeader& header, const Navigation& navigation, std::size_t index) {
        header.segno = header.segno || navigation.segno == 1;
        header.segnoSegno = header.segnoSegno || navigation.segno == 2;
        header.coda = header.coda || navigation.coda == 1;
        header.doubleCoda = header.doubleCoda || navigation.coda == 2;
        header.toCoda = header.toCoda || navigation.toCoda == 1;
        header.toDoubleCoda = header.toDoubleCoda || navigation.toCoda == 2;
        header.fine = header.fine || navigation.fine;
        if (navigation.jump == JumpTo::None) {
            return;
        }
        if (navigation.then) {
            if (header.jump != JumpTo::None &&
                (header.jump != navigation.jump ||
                 (!jumpEndOpen_ && header.jumpThen != *navigation.then))) {
                warn("two jumps in one measure: the last one kept", index);
            }
            header.jumpThen = *navigation.then;
            jumpEndOpen_ = false;
        } else if (header.jump != navigation.jump) {
            if (header.jump != JumpTo::None) {
                warn("two jumps in one measure: the last one kept", index);
            }
            header.jumpThen = JumpThen::End;
            jumpEndOpen_ = true;
        }
        header.jump = navigation.jump;
    }

    // A jump read from <sound> alone says where it goes, not where it ends:
    // al Coda when the song has a To Coda, al Fine when it has a Fine.
    void endOpenJumps() {
        const auto has = [&](bool MeasureHeader::* flag) {
            return std::ranges::any_of(score_.headers,
                                       [&](const MeasureHeader& header) { return header.*flag; });
        };
        JumpThen then = JumpThen::End;
        if (has(&MeasureHeader::toCoda)) {
            then = JumpThen::Coda;
        } else if (has(&MeasureHeader::toDoubleCoda)) {
            then = JumpThen::DoubleCoda;
        } else if (has(&MeasureHeader::fine)) {
            then = JumpThen::Fine;
        }
        for (const std::size_t m : openJumps_) {
            score_.headers[m].jumpThen = then;
        }
    }

    // Repeats and alternate endings belong to the measure header, which the
    // first part writes; the other parts repeat them.
    void readBarline(pugi::xml_node barline, std::size_t index, MeasureHeader& header) {
        if (first_) {
            readRepeats(barline, index, header);
        }
        if (barline.child("fermata")) {
            warn("fermatas dropped", index);
        }
        if (first_) {
            // <segno/> and <coda/>, named for playback by the attributes.
            Navigation navigation;
            if (barline.child("segno") || barline.attribute("segno")) {
                navigation.segno = signKind(barline.attribute("segno").value(), "segno-segno");
            }
            if (barline.child("coda") || barline.attribute("coda")) {
                navigation.coda = signKind(barline.attribute("coda").value(), "double-coda");
            }
            navigate(header, navigation, index);
        }
    }

    void readRepeats(pugi::xml_node barline, std::size_t index, MeasureHeader& header) {
        if (const pugi::xml_node repeat = barline.child("repeat")) {
            if (repeat.attribute("direction").value() == std::string_view("backward")) {
                header.repeatTimes = static_cast<std::uint8_t>(
                    std::clamp(repeat.attribute("times").as_int(2), 2, 255));
            } else {
                header.repeatStart = true;
            }
        }
        if (const pugi::xml_node ending = barline.child("ending")) {
            // number="1, 2": the times through the repeat that play it.
            std::uint8_t times = 0;
            std::string_view numbers = ending.attribute("number").value();
            while (!numbers.empty()) {
                const std::size_t at = numbers.find_first_of(", ");
                const std::string_view number = numbers.substr(0, at);
                numbers =
                    at == std::string_view::npos ? std::string_view{} : numbers.substr(at + 1);
                int time = 0;
                const auto [end, error] =
                    std::from_chars(number.data(), number.data() + number.size(), time);
                if (number.empty()) {
                    continue;
                }
                if (error != std::errc{} || end != number.data() + number.size() || time < 1 ||
                    time > MeasureHeader::kMaxEnding) {
                    warn("alternate endings after the eighth time dropped", index);
                    continue;
                }
                times = static_cast<std::uint8_t>(times | (1U << (time - 1)));
            }
            if (isType(ending, "start")) {
                ending_ = times;
                endingStops_ = false;
            } else {
                ending_ = ending_ != 0 ? ending_ : times;
                endingStops_ = true;
            }
        }
    }

    // ---- Notes ----------------------------------------------------------------------

    std::optional<Event> readNote(const NoteParts& parts, const Fraction& onset,
                                  const Fraction& length, std::size_t index,
                                  std::vector<DynamicMark>& dynamics) {
        const bool chord = parts.chord;
        Event event;
        event.onset = onset;
        event.length = length;
        event.tolerance = oneDivision_;
        event.chord = chord;
        event.written = writtenDuration(parts);
        const StaffMap map = staffFor(parts.staff.text().as_int(1));
        event.staff = map.staff;
        event.tab = map.tab;
        event.voice = textView(parts.voice);
        if (event.voice.empty()) {
            event.voice = "1";
        }
        readLyrics(parts.lyric, event, index);
        readFermata(parts.notations, event);
        if (parts.rest) {
            event.measureRest = parts.rest.attribute("measure").as_bool();
            return event;
        }

        Note note;
        if (parts.pitch) {
            const auto sounding = soundingPitch(parts.pitch);
            if (!sounding) {
                warn("notes outside the MIDI range dropped", index);
                return chord ? std::nullopt : std::optional<Event>(std::move(event));
            }
            note.pitch = *sounding;
        } else if (parts.unpitched) {
            const auto key = drumKey(parts.instrument, parts.unpitched, index);
            if (!key) {
                return chord ? std::nullopt : std::optional<Event>(std::move(event));
            }
            note.pitch = Pitch::fromMidi(*key);
        } else {
            return event; // no pitch: keep the time as a rest
        }
        readMarks(parts, event, note, index, dynamics);
        event.note = std::move(note);
        return event;
    }

    /// The longest fermata of the note's or rest's notations (ADR-0085).
    static void readFermata(pugi::xml_node first, Event& event) {
        for (pugi::xml_node notations = first; notations;
             notations = notations.next_sibling("notations")) {
            for (pugi::xml_node fermata : notations.children("fermata")) {
                event.beatEffects.fermata = std::max(event.beatEffects.fermata, fermataOf(fermata));
            }
        }
    }

    // ---- Lyrics (ADR-0084) ----------------------------------------------------------

    static constexpr std::size_t kMaxVerses = core::kMaxVerses;

    /// The syllable of each verse sung on the note: the first printed <lyric>
    /// of the verse that has text.
    void readLyrics(pugi::xml_node first, Event& event, std::size_t index) {
        for (pugi::xml_node lyric = first; lyric; lyric = lyric.next_sibling("lyric")) {
            if (!lyric.attribute("print-object").as_bool(true)) {
                continue;
            }
            std::optional<Syllable> syllable = syllableOf(lyric);
            if (!syllable) {
                continue;
            }
            const std::optional<std::size_t> verse = verseOf(lyric.attribute("number").value());
            if (!verse) {
                warn("verses after the 16th dropped", index);
                continue;
            }
            if (*verse >= event.lyrics.size()) {
                event.lyrics.resize(*verse + 1);
            }
            if (event.lyrics[*verse].text.empty()) {
                event.lyrics[*verse] = std::move(*syllable);
            }
        }
    }

    /// A lyric's verse: number n from 1 to 16 is verse n (index n - 1), no
    /// number verse 1, a name its place among the names the part uses.
    std::optional<std::size_t> verseOf(std::string_view number) {
        if (number.empty()) {
            return 0;
        }
        std::size_t value = 0;
        const char* end = number.data() + number.size();
        if (const auto read = std::from_chars(number.data(), end, value);
            read.ec == std::errc{} && read.ptr == end) {
            if (value > kMaxVerses) {
                return std::nullopt;
            }
            return std::max<std::size_t>(value, 1) - 1;
        }
        const auto found = std::ranges::find(verseNames_, number);
        if (found != verseNames_.end()) {
            return static_cast<std::size_t>(found - verseNames_.begin());
        }
        if (verseNames_.size() == kMaxVerses) {
            return std::nullopt;
        }
        verseNames_.emplace_back(number);
        return verseNames_.size() - 1;
    }

    /// A <lyric>'s syllable: its texts joined by their elisions ("‿" when the
    /// file leaves the symbol to the reader), joined to the next syllable after
    /// a begin or middle one or when typed with a hyphen, held by <extend>;
    /// none without text.
    static std::optional<Syllable> syllableOf(pugi::xml_node lyric) {
        Syllable syllable;
        std::string_view syllabic;
        for (pugi::xml_node child : lyric.children()) {
            const std::string_view name = child.name();
            if (name == "syllabic") {
                syllabic = textView(child);
            } else if (name == "text") {
                syllable.text += textView(child);
            } else if (name == "elision") {
                const std::string_view symbol = textView(child);
                syllable.text += symbol.empty() ? "‿"sv : symbol;
            } else if (name == "extend") {
                const std::string_view type = child.attribute("type").value();
                syllable.extend = type.empty() || type == "start";
            }
        }
        // As in song.json, a syllable's text never ends with its own extender
        // or hyphen.
        std::string& text = syllable.text;
        if (text.size() > 1 && text.ends_with('_')) {
            text.pop_back();
            syllable.extend = true;
        }
        if (text.size() > 1 && text.ends_with('-')) {
            text.pop_back();
            syllable.hyphen = true;
        }
        syllable.hyphen = syllable.hyphen || syllabic == "begin" || syllabic == "middle";
        if (text.empty()) {
            return std::nullopt;
        }
        return syllable;
    }

    /// A chord note's syllables go on its beat where their verse sings nothing yet.
    static void addLyrics(Beat& beat, std::vector<Syllable>& lyrics) {
        if (lyrics.empty()) {
            return;
        }
        std::vector<Syllable> verses = beat.lyrics ? *beat.lyrics : std::vector<Syllable>{};
        if (verses.size() < lyrics.size()) {
            verses.resize(lyrics.size());
        }
        for (std::size_t v = 0; v < lyrics.size(); ++v) {
            if (verses[v].text.empty()) {
                verses[v] = std::move(lyrics[v]);
            }
        }
        beat.lyrics = SharedOptional(std::move(verses));
    }

    static std::optional<Duration> writtenDuration(const NoteParts& parts) {
        const auto value = noteValueOf(textView(parts.type));
        if (!value) {
            return std::nullopt;
        }
        const int dotCount = std::min(parts.dots, Duration::kMaxDots);
        Tuplet tuplet;
        if (const pugi::xml_node modification = parts.timeModification) {
            const int actual = modification.child("actual-notes").text().as_int(0);
            const int normal = modification.child("normal-notes").text().as_int(0);
            if (actual >= 1 && actual <= Tuplet::kMaxCount && normal >= 1 &&
                normal <= Tuplet::kMaxCount) {
                tuplet = Tuplet(actual, normal);
            }
        }
        return Duration(*value, dotCount, tuplet);
    }

    /// Written pitch + transposition, spelled with the transposition's steps.
    [[nodiscard]] std::optional<Pitch> soundingPitch(pugi::xml_node pitch) {
        const auto step = stepOf(textView(pitch.child("step")));
        if (!step) {
            return std::nullopt;
        }
        const int alter = roundedIn(pitch.child("alter"), -3, 3);
        const int octave = intIn(pitch.child("octave"), 4, -2, 10);
        const Transposition& transposition = layout_.transposition;
        const int midi = midiOf(*step, alter, octave) + transposition.semitones();
        if (midi < 0 || midi > 127) {
            return std::nullopt;
        }
        const int diatonic = octave * 7 + static_cast<int>(*step) + transposition.steps();
        int newOctave = diatonic / 7;
        int newStep = diatonic % 7;
        if (newStep < 0) {
            newStep += 7;
            --newOctave;
        }
        const auto spelledStep = static_cast<Step>(newStep);
        const int newAlter = midi - midiOf(spelledStep, 0, newOctave);
        if (newAlter >= Pitch::kMinAlter && newAlter <= Pitch::kMaxAlter) {
            return Pitch(spelledStep, newAlter, newOctave);
        }
        return concertKey_.spell(midi);
    }

    std::optional<int> drumKey(pugi::xml_node instrument, pugi::xml_node unpitched,
                               std::size_t index) {
        if (instrument) {
            const std::string_view id = instrument.attribute("id").value();
            for (const auto& [instrumentId, key] : info_.drumKeys) {
                if (instrumentId == id) {
                    return key;
                }
            }
        } else if (!info_.drumKeys.empty()) {
            return info_.drumKeys.front().second;
        }
        // No MIDI key: keep the staff position as the pitch.
        warn("drum notes without a MIDI key placed by their staff position", index);
        const auto step = stepOf(textOf(unpitched.child("display-step")));
        const int midi =
            midiOf(step.value_or(Step::C), 0, intIn(unpitched.child("display-octave"), 4, -1, 9));
        if (midi < 0 || midi > 127) {
            return std::nullopt;
        }
        return midi;
    }

    void readMarks(const NoteParts& parts, Event& event, Note& note, std::size_t index,
                   std::vector<DynamicMark>& dynamics) {
        NoteEffects& fx = note.effects;
        if (const pugi::xml_node head = parts.notehead) {
            if (!isPercussion() && textView(head) == "x") {
                fx.deadNote = true;
            }
            if (head.attribute("parentheses").as_bool()) {
                fx.ghost = true;
            }
        }
        fx.tiedFromPrevious = parts.tieStop;
        fx.palmMute = parts.palmMute;

        std::optional<int> string;
        std::optional<int> fret;
        std::vector<Bend> bends;
        for (pugi::xml_node notations = parts.notations; notations;
             notations = notations.next_sibling("notations")) {
            for (pugi::xml_node mark : notations.children()) {
                const std::string_view name = mark.name();
                if (name == "tied") {
                    if (isType(mark, "stop")) {
                        fx.tiedFromPrevious = true;
                    }
                } else if (name == "slur") {
                    // From its start to its stop (ADR-0086); "continue" changes nothing.
                    if (isType(mark, "start") || isType(mark, "stop")) {
                        event.slurs.push_back(SlurMark{.number = mark.attribute("number").as_int(1),
                                                       .start = isType(mark, "start")});
                    }
                } else if (name == "slide" || name == "glissando") {
                    if (isType(mark, "start")) {
                        fx.slideOut = SlideOut::Legato;
                    }
                } else if (name == "arpeggiate") {
                    // Rolled upwards is played like a downstroke (low strings first).
                    event.beatEffects.strum =
                        mark.attribute("direction").value() == "down"sv ? Strum::Up : Strum::Down;
                } else if (name == "dynamics") {
                    if (const auto dynamic = dynamicOf(mark)) {
                        dynamics.push_back(DynamicMark{
                            .onset = event.onset, .staff = event.staff, .dynamic = *dynamic});
                    }
                } else if (name == "technical") {
                    readTechnical(mark, event, fx, string, fret, bends);
                } else if (name == "articulations") {
                    readArticulations(mark, fx, index);
                } else if (name == "ornaments") {
                    readOrnaments(mark, event, note, index);
                }
            }
        }
        if (string || fret) {
            if (string && fret && *string >= 1 && *string <= Tuning::kMaxStrings && *fret >= 0 &&
                *fret <= Tuning::kMaxFret) {
                note.position = FretPosition{.string = static_cast<std::int8_t>(*string),
                                             .fret = static_cast<std::int8_t>(*fret)};
            } else if (!isPercussion()) {
                warn("string and fret off the neck dropped", index);
            }
        }
        if (fx.harmonic.kind == HarmonicKind::Natural) {
            fx.harmonic.fretTenths =
                static_cast<std::int16_t>(note.position ? note.position->fret * 10 : 120);
        }
        if (!bends.empty()) {
            fx.bend = bendCurveOf(bends);
        }
    }

    static void readTechnical(pugi::xml_node technical, Event& event, NoteEffects& fx,
                              std::optional<int>& string, std::optional<int>& fret,
                              std::vector<Bend>& bends) {
        for (pugi::xml_node mark : technical.children()) {
            const std::string_view name = mark.name();
            if (name == "string") {
                string = mark.text().as_int(0);
            } else if (name == "fret") {
                fret = mark.text().as_int(-1);
            } else if (name == "hammer-on" || name == "pull-off") {
                if (isType(mark, "stop")) {
                    fx.legatoFromPrevious = true;
                }
            } else if (name == "bend") {
                bends.push_back(Bend{.cents = static_cast<int>(std::lround(
                                         numberIn(mark.child("bend-alter"), -24.0, 24.0) * 100.0)),
                                     .preBend = static_cast<bool>(mark.child("pre-bend")),
                                     .release = static_cast<bool>(mark.child("release"))});
            } else if (name == "harmonic") {
                fx.harmonic = mark.child("artificial")
                                  ? Harmonic{.kind = HarmonicKind::Artificial, .fretTenths = 120}
                                  : Harmonic{.kind = HarmonicKind::Natural, .fretTenths = 0};
            } else if (name == "tap") {
                event.beatEffects.bassTechnique = BassTechnique::Tap; // the beat's (ADR-0060)
            } else if (name == "up-bow") {
                event.beatEffects.pickStroke = PickStroke::Up;
            } else if (name == "down-bow") {
                event.beatEffects.pickStroke = PickStroke::Down;
            }
        }
    }

    void readArticulations(pugi::xml_node articulations, NoteEffects& fx, std::size_t index) {
        for (pugi::xml_node mark : articulations.children()) {
            const std::string_view name = mark.name();
            if (name == "staccato" || name == "staccatissimo" || name == "spiccato") {
                fx.staccato = true;
            } else if (name == "tenuto") {
                fx.tenuto = true;
            } else if (name == "detached-legato") { // portato (ADR-0088)
                fx.tenuto = true;
                fx.staccato = true;
            } else if (name == "accent") {
                if (fx.accent == Accent::None) {
                    fx.accent = Accent::Accent;
                }
            } else if (name == "strong-accent") {
                fx.accent = Accent::Marcato;
            } else if (name == "scoop") {
                fx.slideIn = SlideIn::FromBelow;
            } else if (name == "plop") {
                fx.slideIn = SlideIn::FromAbove;
            } else if (name == "doit") {
                fx.slideOut = SlideOut::Upwards;
            } else if (name == "falloff") {
                fx.slideOut = SlideOut::Downwards;
            } else {
                warn("articulations other than staccato, tenuto and accents dropped", index);
            }
        }
    }

    void readOrnaments(pugi::xml_node ornaments, Event& event, Note& note, std::size_t index) {
        NoteEffects& fx = note.effects;
        // A trill's wavy line is the trill's, not a vibrato (ADR-0090).
        const bool trill = static_cast<bool>(ornaments.child("trill-mark"));
        std::optional<int> otherAlter; // from an accidental mark: the other note's
        for (pugi::xml_node mark : ornaments.children()) {
            const std::string_view name = mark.name();
            if (name == "trill-mark") {
                continue;
            }
            if (name == "accidental-mark") {
                if (trill) {
                    otherAlter = accidentalAlter(textView(mark));
                }
                continue;
            }
            if (name == "tremolo") {
                const std::string_view type = mark.attribute("type").value();
                if (type.empty() || type == "single") {
                    const int marks = mark.text().as_int(3);
                    event.beatEffects.tremoloPicking = marks <= 1   ? NoteValue::Eighth
                                                       : marks == 2 ? NoteValue::Sixteenth
                                                                    : NoteValue::ThirtySecond;
                } else {
                    warn("tremolos between notes dropped", index);
                }
            } else if (const auto ornament = ornamentOf(name)) {
                fx.ornament = *ornament; // ADR-0091
            } else if (name == "wavy-line") {
                if (isType(mark, "start") && !trill) {
                    const bool wide = mark.attribute("smufl").value() == detail::kWideVibratoGlyph;
                    fx.vibrato = wide ? Vibrato::Wide : Vibrato::Slight;
                }
            } else {
                warn("ornaments dropped", index);
            }
        }
        if (trill) {
            // To the next step of the key unless an accidental mark says
            // otherwise, at thirty-seconds.
            int semitones = core::semitonesToNextStep(note.pitch, concertKey_);
            // An accidental mark spells the step above the note as written,
            // so the interval holds for a transposing part as well.
            const pugi::xml_node pitch = ornaments.parent().parent().child("pitch");
            const auto step = stepOf(textView(pitch.child("step")));
            if (otherAlter && step) {
                const int alter = roundedIn(pitch.child("alter"), -3, 3);
                const int octave = intIn(pitch.child("octave"), 4, -2, 10);
                const int next = (static_cast<int>(*step) + 1) % 7;
                semitones = detail::midiOf(next, *otherAlter, next == 0 ? octave + 1 : octave) -
                            midiOf(*step, alter, octave);
            }
            if (semitones > 0 && semitones <= Trill::kMaxSemitones) {
                fx.trill = Trill{.semitones = static_cast<std::int8_t>(semitones)};
            } else {
                warn("trills to an unusual note dropped", index);
            }
        }
    }

    // A mordent or a turn by its MusicXML name (ADR-0091); delayed and vertical
    // turns are none of them.
    static std::optional<Ornament> ornamentOf(std::string_view name) {
        if (name == "mordent") {
            return Ornament::Mordent;
        }
        if (name == "inverted-mordent") {
            return Ornament::UpperMordent;
        }
        if (name == "turn") {
            return Ornament::Turn;
        }
        if (name == "inverted-turn") {
            return Ornament::InvertedTurn;
        }
        return std::nullopt;
    }

    // The alteration an accidental mark writes; nullopt for one it does not.
    static std::optional<int> accidentalAlter(std::string_view text) {
        if (text == "sharp") {
            return 1;
        }
        if (text == "natural") {
            return 0;
        }
        if (text == "flat") {
            return -1;
        }
        if (text == "double-sharp" || text == "sharp-sharp") {
            return 2;
        }
        if (text == "flat-flat") {
            return -2;
        }
        return std::nullopt;
    }

    // ---- Building voices ---------------------------------------------------------

    /// The model voice of a MusicXML voice on a staff, numbered in order of
    /// appearance; nullopt past the fourth.
    std::optional<std::size_t> voiceSlot(std::size_t staff, const std::string& voice,
                                         std::size_t index) {
        auto& ids = voiceIds_[staff];
        if (const auto found = std::ranges::find(ids, voice); found != ids.end()) {
            return static_cast<std::size_t>(std::distance(ids.begin(), found));
        }
        if (ids.size() == Measure::kMaxVoices) {
            warn("notes in a fifth voice on a staff dropped", index);
            return std::nullopt;
        }
        ids.push_back(voice);
        return ids.size() - 1;
    }

    struct Placed {
        std::size_t staff = 0;
        std::size_t voice = 0;
        std::size_t beat = 0;
        std::size_t note = 0;
        Fraction onset{};
        int midi = 0;
        bool merged = false;
    };

    /// A grace note: a new grace of its voice, or a note of the one before it
    /// when it is a chord. A slashed grace (an acciaccatura) or one taking time
    /// from the note before is played before the beat; the others on it.
    void readGrace(const NoteParts& parts, const Fraction& cursor, std::size_t index,
                   std::vector<DynamicMark>& dynamics) {
        auto event = readNote(parts, cursor, Fraction(0), index, dynamics);
        if (!event || !event->note) {
            return; // a grace rest means nothing
        }
        if (!event->lyrics.empty()) {
            warn("lyrics on grace notes dropped", index);
        }
        for (SlurMark& slur : event->slurs) {
            slur.grace = true;
        }
        auto& pending = pendingGraces_;
        if (parts.chord && !pending.empty() && pending.back().staff == event->staff &&
            pending.back().tab == event->tab && pending.back().voice == event->voice) {
            pending.back().grace.notes.push_back(std::move(*event->note));
            pending.back().slurs.insert(pending.back().slurs.end(), event->slurs.begin(),
                                        event->slurs.end());
            return;
        }
        const bool before = parts.grace.attribute("slash").as_string() == "yes"sv ||
                            parts.grace.attribute("steal-time-previous");
        pending.push_back(PendingGrace{
            .staff = event->staff,
            .tab = event->tab,
            .voice = std::move(event->voice),
            .grace = Grace{.duration = event->written.value_or(Duration(NoteValue::Eighth)),
                           .notes = {std::move(*event->note)},
                           .onBeat = !before},
            .slurs = std::move(event->slurs)});
    }

    /// Moves the grace notes waiting in the event's voice onto it.
    void takeGraces(Event& event) {
        auto& pending = pendingGraces_;
        if (pending.empty()) {
            return;
        }
        const auto mine = [&event](const PendingGrace& grace) {
            return grace.staff == event.staff && grace.tab == event.tab &&
                   grace.voice == event.voice;
        };
        std::vector<SlurMark> slurs;
        for (PendingGrace& grace : pending) {
            if (mine(grace)) {
                event.graces.push_back(std::move(grace.grace));
                slurs.insert(slurs.end(), grace.slurs.begin(), grace.slurs.end());
            }
        }
        std::erase_if(pending, mine);
        if (!slurs.empty()) {
            slurs.insert(slurs.end(), event.slurs.begin(), event.slurs.end());
            event.slurs = std::move(slurs);
        }
    }

    /// Opens and closes the slurs marked on an event placed on `beat` of voice
    /// `voice` of `staff` in measure `measure` (ADR-0086). The marks of its
    /// grace notes come first, and stops before starts, so a note that ends
    /// one slur and starts the next joins them. A slur goes on one voice, and
    /// one from a grace note to its own beat is dropped.
    void slurAt(const Event& event, std::size_t staff, std::size_t voice, std::size_t beat,
                std::size_t measure) {
        if (event.slurs.empty()) {
            return;
        }
        for (const bool grace : {true, false}) {
            for (const bool start : {false, true}) {
                for (const SlurMark& mark : event.slurs) {
                    if (mark.grace != grace || mark.start != start) {
                        continue;
                    }
                    const SlurEnd here{.measure = measure,
                                       .staff = staff,
                                       .voice = voice,
                                       .beat = beat,
                                       .grace = grace};
                    if (start) {
                        openSlurs_.insert_or_assign(mark.number, here);
                        continue;
                    }
                    const auto open = openSlurs_.find(mark.number);
                    if (open == openSlurs_.end()) {
                        continue; // its start was dropped with its note
                    }
                    const SlurEnd from = open->second;
                    openSlurs_.erase(open);
                    if (from.staff != staff || from.voice != voice) {
                        warn("slurs between voices dropped", measure);
                    } else if (from.measure == measure && from.beat == beat) {
                        if (from.grace || grace) {
                            warn("slurs on grace notes dropped", measure);
                        }
                    } else {
                        slurs_.emplace_back(from, here);
                    }
                }
            }
        }
    }

    /// Slurs every beat from each slur's start to the beat before its stop,
    /// across barlines, once the part is read (ADR-0086).
    void endSlurs() {
        for (const auto& [from, to] : slurs_) {
            std::vector<Measure>& staff = measures_[from.staff];
            for (std::size_t m = from.measure; m <= to.measure && m < staff.size(); ++m) {
                std::vector<Beat>& beats = staff[m].voices[from.voice].beats;
                const std::size_t first = m == from.measure ? from.beat : 0;
                const std::size_t last =
                    m == to.measure ? std::min(to.beat, beats.size()) : beats.size();
                for (std::size_t b = first; b < last; ++b) {
                    beats[b].effects.slur = true;
                }
            }
        }
        for (const auto& [number, open] : openSlurs_) {
            warn("slurs without an end dropped", open.measure);
        }
        slurs_.clear();
        openSlurs_.clear();
    }

    void build(std::size_t index, std::vector<Event>& events, std::vector<DynamicMark>& dynamics,
               std::vector<SpanMark>& hairpins, const TimeSignature& time) {
        const std::size_t staffCount = layout_.staves.size();
        std::vector<Measure> built(staffCount);
        auto& ends = ends_;
        auto& dropped = dropped_;
        auto& placed = placed_;
        ends.assign(staffCount, {});
        dropped.assign(staffCount, {});
        placed.clear();

        // First pass: the voice of each event, then storage for every beat and
        // chord is reserved once instead of grown note by note.
        auto& slots = slots_;
        auto& chordSizes = chordSizes_;
        slots.assign(events.size(), kNoSlot);
        chordSizes.assign(events.size(), 1);
        std::vector<std::array<std::size_t, Measure::kMaxVoices>> starts(staffCount);
        std::vector<std::array<std::size_t, Measure::kMaxVoices>> beatCounts(staffCount);
        for (auto& start : starts) {
            start.fill(kNoSlot);
        }
        for (std::size_t e = 0; e < events.size(); ++e) {
            const Event& event = events[e];
            if (event.tab) {
                continue;
            }
            const auto slot = voiceSlot(event.staff, event.voice, index);
            if (!slot) {
                continue;
            }
            slots[e] = *slot;
            std::size_t& start = starts[event.staff][*slot];
            if (!event.chord) {
                start = e;
                ++beatCounts[event.staff][*slot];
            } else if (start != kNoSlot) {
                ++chordSizes[start];
            }
        }
        for (std::size_t staff = 0; staff < staffCount; ++staff) {
            for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
                built[staff].voices[v].beats.reserve(beatCounts[staff][v]);
            }
        }

        std::ranges::stable_sort(dynamics, std::less<>{}, &DynamicMark::onset);
        const auto dynamicAt = [&](std::size_t staff, const Fraction& onset) {
            Dynamic dynamic = dynamics_[staff];
            for (const DynamicMark& mark : dynamics) {
                if (mark.onset > onset) {
                    break;
                }
                if (!mark.staff || *mark.staff == staff) {
                    dynamic = mark.dynamic;
                }
            }
            return dynamic;
        };

        for (std::size_t e = 0; e < events.size(); ++e) {
            Event& event = events[e];
            const std::optional<std::size_t> slot =
                slots[e] == kNoSlot ? std::nullopt : std::optional<std::size_t>(slots[e]);
            if (!slot) {
                continue;
            }
            Voice& voice = built[event.staff].voices[*slot];
            Fraction& end = ends[event.staff][*slot];
            bool& lastDropped = dropped[event.staff][*slot];
            if (event.note) {
                event.note->effects.dynamic = dynamicAt(event.staff, event.onset);
            }
            if (event.chord) {
                if (event.note && !lastDropped && !voice.beats.empty()) {
                    slurAt(event, event.staff, *slot, voice.beats.size() - 1, index);
                    Beat& beat = voice.beats.back();
                    addLyrics(beat, event.lyrics);
                    beat.effects.fermata =
                        std::max(beat.effects.fermata, event.beatEffects.fermata);
                    placed.push_back(Placed{.staff = event.staff,
                                            .voice = *slot,
                                            .beat = voice.beats.size() - 1,
                                            .note = beat.notes.size(),
                                            .onset = event.onset,
                                            .midi = event.note->pitch.midiNumber()});
                    beat.notes.push_back(std::move(*event.note));
                }
                continue;
            }
            lastDropped = false;
            if (!event.note && !event.written && event.onset.isZero() &&
                event.length == time.measureLength()) {
                event.measureRest = true; // a whole-measure rest without the attribute
            }
            if (event.measureRest && event.beatEffects.fermata == Fermata::None) {
                continue; // an empty voice is a measure rest; a held one is written out
            }

            const Fraction gap = event.onset - end;
            if (gap > Fraction(0) && gap >= event.tolerance) {
                if (const auto rests = restsFor(gap, event.written)) {
                    for (const Duration& rest : *rests) {
                        voice.beats.push_back(Beat{.duration = rest});
                    }
                    end += gap;
                } else {
                    warn("gaps off the 1/128 grid ignored", index);
                }
            } else if (gap < Fraction(0) && -gap >= event.tolerance) {
                warn("overlapping notes dropped", index);
                lastDropped = true;
                continue;
            }

            std::vector<Duration> pieces;
            if (event.written) {
                pieces.push_back(*event.written);
            } else if (auto split = splitLength(event.length); split && !split->empty()) {
                pieces = std::move(*split);
            } else {
                warn("notes without a readable length dropped", index);
                lastDropped = true;
                continue;
            }
            for (std::size_t p = 0; p < pieces.size(); ++p) {
                Beat beat{.duration = pieces[p]};
                if (p == 0) {
                    slurAt(event, event.staff, *slot, voice.beats.size(), index);
                    beat.effects = event.beatEffects;
                    beat.graces = std::move(event.graces);
                    if (!event.lyrics.empty()) {
                        beat.lyrics = SharedOptional(std::move(event.lyrics));
                    }
                    for (Grace& grace : beat.graces) {
                        for (Note& note : grace.notes) {
                            note.effects.dynamic = dynamicAt(event.staff, event.onset);
                        }
                    }
                }
                if (event.note) {
                    beat.notes.reserve(p == 0 ? chordSizes[e] : 1);
                    Note note = p + 1 < pieces.size() ? *event.note : std::move(*event.note);
                    if (p == 0) {
                        placed.push_back(Placed{.staff = event.staff,
                                                .voice = *slot,
                                                .beat = voice.beats.size(),
                                                .note = 0,
                                                .onset = event.onset,
                                                .midi = note.pitch.midiNumber()});
                    } else { // a length written as tied notes
                        note.effects =
                            NoteEffects{.tiedFromPrevious = true, .dynamic = note.effects.dynamic};
                    }
                    beat.notes.push_back(std::move(note));
                }
                end += pieces[p].length();
                voice.beats.push_back(std::move(beat));
            }
        }

        mergeTab(index, events, built, placed);
        std::ranges::stable_sort(hairpins, std::less<>{}, &SpanMark::onset);
        for (std::size_t staff = 0; staff < staffCount; ++staff) {
            if (!isPercussion()) {
                checkPositions(layout_.staves[staff], built[staff], index);
            }
            for (Voice& voice : built[staff].voices) {
                const VoiceFill fill = voiceFill(voice, time);
                if (fill == VoiceFill::Incomplete) {
                    (void)fillWithRests(voice, time);
                } else if (fill == VoiceFill::Overfull) {
                    warn("voices longer than their measure kept as written", index);
                }
            }
            markHairpins(built[staff], staff, hairpins);
            markOttavas(built[staff], staff, hairpins);
            measures_[staff].push_back(std::move(built[staff]));
        }
        for (const DynamicMark& mark : dynamics) {
            for (std::size_t staff = 0; staff < staffCount; ++staff) {
                if (!mark.staff || *mark.staff == staff) {
                    dynamics_[staff] = mark.dynamic;
                }
            }
        }
    }

    /// The wedges of a measure go on the beats of the staff's first voice that
    /// start under them; the hairpin in force carries over to the next measure.
    /// `marks` are in time order.
    void markHairpins(Measure& measure, std::size_t staff, const std::vector<SpanMark>& marks) {
        Hairpin& state = hairpins_[staff];
        auto mark = marks.begin();
        const auto passUntil = [&](const std::optional<Fraction>& at) {
            for (; mark != marks.end() && (!at || mark->onset <= *at); ++mark) {
                const auto* hairpin = std::get_if<Hairpin>(&mark->value);
                if (hairpin != nullptr && (!mark->staff || *mark->staff == staff)) {
                    state = *hairpin;
                }
            }
        };
        const auto voice =
            std::ranges::find_if(measure.voices, [](const Voice& v) { return !v.isEmpty(); });
        if (voice != measure.voices.end()) {
            Fraction at;
            for (Beat& beat : voice->beats) {
                passUntil(at);
                beat.effects.hairpin = state;
                at += beat.duration.length();
            }
        }
        passUntil(std::nullopt);
    }

    /// The octave lines of a measure (ADR-0089) go on the beats of every voice
    /// that start under them, rests too; the line in force carries over to the
    /// next measure. `marks` are in time order.
    void markOttavas(Measure& measure, std::size_t staff, const std::vector<SpanMark>& marks) {
        const auto pass = [staff](const SpanMark& mark, Ottava& state) {
            const auto* ottava = std::get_if<Ottava>(&mark.value);
            if (ottava != nullptr && (!mark.staff || *mark.staff == staff)) {
                state = *ottava;
            }
        };
        for (Voice& voice : measure.voices) {
            Ottava state = ottavas_[staff];
            auto mark = marks.begin();
            Fraction at;
            for (Beat& beat : voice.beats) {
                for (; mark != marks.end() && mark->onset <= at; ++mark) {
                    pass(*mark, state);
                }
                beat.effects.ottava = state;
                at += beat.duration.length();
            }
        }
        for (const SpanMark& mark : marks) {
            pass(mark, ottavas_[staff]);
        }
    }

    /// A TAB staff repeats the notes of its standard staff: its string, fret and
    /// techniques go to the note with the same onset and pitch.
    void mergeTab(std::size_t index, const std::vector<Event>& events, std::vector<Measure>& built,
                  std::vector<Placed>& placed) {
        for (const Event& event : events) {
            if (!event.tab || !event.note) {
                continue;
            }
            const int midi = event.note->pitch.midiNumber();
            const auto match = std::ranges::find_if(placed, [&](const Placed& candidate) {
                return !candidate.merged && candidate.staff == event.staff &&
                       candidate.onset == event.onset && candidate.midi == midi;
            });
            if (match == placed.end()) {
                warn("TAB notes without a matching note dropped", index);
                continue;
            }
            match->merged = true;
            Note& target =
                built[match->staff].voices[match->voice].beats[match->beat].notes[match->note];
            if (!target.position) {
                target.position = event.note->position;
            }
            mergeEffects(target.effects, event.note->effects);
        }
    }

    /// String and fret win over the written pitch on a fretted staff: tablature
    /// is what the player reads. Natural harmonics are stored at their fret.
    void checkPositions(const Staff& staff, Measure& measure, std::size_t index) {
        for (Voice& voice : measure.voices) {
            for (Beat& beat : voice.beats) {
                for (Note& note : beat.notes) {
                    if (!note.position) {
                        continue;
                    }
                    const auto& tuning = staff.tuning();
                    if (!tuning) {
                        note.position.reset();
                        warn("string and fret without a tuning dropped", index);
                        continue;
                    }
                    const int string = note.position->string;
                    const int fret = note.position->fret;
                    if (string > tuning->stringCount() || staff.capo() + fret > Tuning::kMaxFret) {
                        note.position.reset();
                        warn("string and fret off the neck dropped", index);
                        continue;
                    }
                    const int fretted = tuning->midiAt(string, fret, staff.capo());
                    if (fretted != note.pitch.midiNumber()) {
                        note.pitch = respell(note.pitch, fretted, concertKey_);
                        if (note.effects.harmonic.kind == HarmonicKind::None) {
                            warn("pitches corrected to match string and fret", index);
                        }
                    }
                }
            }
        }
    }

    const PartInfo& info_;
    PartLayout layout_;
    bool first_;
    std::uint8_t ending_ = 0;              // the alternate ending being read, as times
    bool endingStops_ = false;             // it stops at the end of this measure
    bool jumpEndOpen_ = false;             // this measure's jump came without its ending
    std::vector<std::size_t> openJumps_{}; // the measures whose jump ending is inferred
    ScoreState& score_;                    // written by the first part only
    Warnings& warnings_;
    int divisions_ = 1;
    Fraction oneDivision_{1, 4};
    KeySignature concertKey_{};
    std::vector<std::optional<ClefInfo>> lastClefs_{}; // per MusicXML staff number
    std::vector<Clef> clefs_{};                        // per staff, in force when read
    std::vector<std::optional<Clef>> nextClefs_{};     // per staff, for the next measure
    std::vector<std::vector<std::string>> voiceIds_;   // per staff, MusicXML voice ids
    std::vector<Dynamic> dynamics_;                    // per staff, at the end of the last measure
    std::vector<Hairpin> hairpins_;                    // per staff, at the end of the last measure
    std::vector<Ottava> ottavas_;                      // per staff, at the end of the last measure
    std::map<int, SlurEnd> openSlurs_{};               // by number: where each open slur starts
    std::vector<std::pair<SlurEnd, SlurEnd>> slurs_{}; // closed slurs, from start to stop
    std::vector<std::vector<Measure>> measures_;       // per staff
    detail::ChordPool chordPool_;                      // one symbol for equal chords

    // Per-measure buffers, kept to reuse their storage.
    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
    std::vector<Event> events_{};
    std::vector<DynamicMark> dynamicMarks_{};
    std::vector<SpanMark> hairpinMarks_{};
    std::vector<ChordMark> chordMarks_{};
    std::vector<TextMark> textMarks_{};
    std::vector<TempoMark> tempoMarks_{};
    std::vector<ClefMark> clefMarks_{};
    std::vector<PendingGrace> pendingGraces_{};
    std::vector<std::string> verseNames_{}; // lyric numbers that are names, in order
    std::vector<Placed> placed_{};
    std::vector<std::array<Fraction, Measure::kMaxVoices>> ends_{};
    std::vector<std::array<bool, Measure::kMaxVoices>> dropped_{};
    std::vector<std::size_t> slots_{};
    std::vector<std::size_t> chordSizes_{};
};

// ---- Score ------------------------------------------------------------------------

SongInfo readInfo(pugi::xml_node score) {
    SongInfo info;
    const std::string work = textOf(score.child("work").child("work-title"));
    const std::string movement = textOf(score.child("movement-title"));
    info.title = work.empty() ? movement : work;
    if (!work.empty() && !movement.empty() && movement != work) {
        info.subtitle = movement;
    }
    const pugi::xml_node identification = score.child("identification");
    for (pugi::xml_node creator : identification.children("creator")) {
        const std::string_view type = creator.attribute("type").value();
        if (type == "composer" && info.music.empty()) {
            info.music = textOf(creator);
        } else if ((type == "lyricist" || type == "poet") && info.words.empty()) {
            info.words = textOf(creator);
        } else if (type == "artist" && info.artist.empty()) {
            info.artist = textOf(creator);
        } else if (type == "transcriber" && info.transcriber.empty()) {
            info.transcriber = textOf(creator);
        }
    }
    for (pugi::xml_node field :
         identification.child("miscellaneous").children("miscellaneous-field")) {
        const std::string_view name = field.attribute("name").value();
        if (name == "album") {
            info.album = textOf(field);
        } else if (name == "notes") {
            info.notes = textOf(field);
        }
    }
    info.copyright = textOf(identification.child("rights"));
    // Credits (the printed page) fill what the metadata leaves empty.
    for (pugi::xml_node credit : score.children("credit")) {
        const std::string type = textOf(credit.child("credit-type"));
        const std::string words = textOf(credit.child("credit-words"));
        const auto fill = [&words](std::string& field) {
            if (field.empty()) {
                field = words;
            }
        };
        if (type == "title") {
            fill(info.title);
        } else if (type == "subtitle") {
            fill(info.subtitle);
        } else if (type == "composer") {
            fill(info.music);
        } else if (type == "lyricist") {
            fill(info.words);
        } else if (type == "rights") {
            fill(info.copyright);
        }
    }
    return info;
}

std::expected<ImportResult, FormatError> importScore(pugi::xml_node score, std::size_t bytes,
                                                     const MusicXmlOptions& options) {
    const std::vector<PartInfo> partList = readPartList(score);
    const auto partRange = score.children("part");
    const std::vector<pugi::xml_node> parts(partRange.begin(), partRange.end());
    std::vector<PartInfo> infos(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string_view id = parts[i].attribute("id").value();
        const auto known =
            std::ranges::find_if(partList, [&](const PartInfo& info) { return info.id == id; });
        infos[i] = known != partList.end() ? *known : PartInfo{.id = std::string(id)};
        if (infos[i].name.empty()) {
            infos[i].name = std::format("Part {}", i + 1);
        }
    }

    // The first part writes the measure headers every other part reads, so it
    // goes first; the others only read the shared state and the document, so
    // they can be read on several threads. Warnings are kept per part and
    // merged in score order: the result does not depend on the threads.
    ScoreState state;
    std::vector<Warnings> warnings(parts.size());
    std::vector<std::optional<PartReader>> readers(parts.size());
    const auto readPart = [&](std::size_t i) {
        PartLayout layout = layoutPart(parts[i], infos[i], warnings[i]);
        readers[i].emplace(infos[i], std::move(layout), i == 0, state, warnings[i]);
        readers[i]->read(parts[i]);
    };
    if (!parts.empty()) {
        readPart(0);
    }
    if (parts.size() > 1) {
        // Threads only for large documents (measured on the benchmark score).
        constexpr std::size_t kParallelBytes = std::size_t{1} << 20;
        detail::forEachIndex(
            1, parts.size(),
            detail::threadsFor(options.threads, bytes, kParallelBytes, parts.size() - 1), readPart);
    }
    Warnings merged;
    for (const Warnings& part : warnings) {
        merged.append(part);
    }

    Song song;
    song.info() = readInfo(score);
    if (state.startTempo) {
        song.setStartTempo(*state.startTempo);
    }
    song.appendMeasures(state.headers.size());
    for (std::size_t m = 0; m < state.headers.size(); ++m) {
        song.measureHeader(m) = std::move(state.headers[m]);
    }
    for (std::optional<PartReader>& reader : readers) {
        auto measures = std::move(reader->measures());
        const std::size_t track = song.addTrack(reader->takeTrack());
        for (std::size_t staff = 0; staff < measures.size(); ++staff) {
            for (std::size_t m = 0; m < measures[staff].size(); ++m) {
                song.track(track).staff(staff).measure(m) = std::move(measures[staff][m]);
            }
        }
    }
    return ImportResult{.song = std::move(song), .warnings = merged.lines()};
}

/// The score inside a compressed .mxl archive.
std::expected<std::string, FormatError> mxlScore(std::span<const std::uint8_t> bytes) {
    detail::ZipReader zip(bytes);
    if (!zip.ok()) {
        return std::unexpected(FormatError{"damaged .mxl archive"});
    }
    std::string root;
    if (const auto container = zip.entry("META-INF/container.xml", std::size_t{1} << 20)) {
        pugi::xml_document document;
        if (!document.load_buffer(container->data(), container->size())) {
            return std::unexpected(FormatError{"damaged .mxl container"});
        }
        for (pugi::xml_node file :
             document.child("container").child("rootfiles").children("rootfile")) {
            const std::string_view type = file.attribute("media-type").value();
            if (type.empty() || type.find("musicxml") != std::string_view::npos) {
                root = file.attribute("full-path").value();
                break;
            }
        }
        if (root.empty()) {
            return std::unexpected(FormatError{"the .mxl container names no score"});
        }
    } else {
        for (const std::string& name : zip.names()) {
            const bool score = name.ends_with(".musicxml") || name.ends_with(".xml");
            if (score && !name.starts_with("META-INF/")) {
                root = name;
                break;
            }
        }
        if (root.empty()) {
            return std::unexpected(FormatError{"no MusicXML score in the .mxl archive"});
        }
    }
    auto text = zip.entry(root);
    if (!text) {
        return std::unexpected(FormatError{std::format("the .mxl archive has no {}", root)});
    }
    return std::move(*text);
}

} // namespace

std::expected<ImportResult, FormatError> importMusicXml(std::string_view xml,
                                                        const MusicXmlOptions& options) {
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
    if (!parsed) {
        return std::unexpected(FormatError{
            std::format("invalid XML at byte {}: {}", parsed.offset, parsed.description())});
    }
    const pugi::xml_node root = document.document_element();
    const std::string_view name = root.name();
    if (name == "score-timewise") {
        return std::unexpected(
            FormatError{"score-timewise documents are not supported (only score-partwise)"});
    }
    if (name != "score-partwise") {
        return std::unexpected(FormatError{
            std::format("not a MusicXML score (root element <{}>)", name.empty() ? "none" : name)});
    }
    try {
        return importScore(root, xml.size(), options);
    } catch (const FormatError& error) {
        return std::unexpected(error);
    } catch (const std::exception& error) {
        return std::unexpected(FormatError{std::format("unsupported content: {}", error.what())});
    }
}

std::expected<ImportResult, FormatError> importMusicXmlBytes(std::span<const std::uint8_t> bytes,
                                                             const MusicXmlOptions& options) {
    if (!detail::looksLikeZip(bytes)) {
        return importMusicXml(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), options);
    }
    try {
        return mxlScore(bytes).and_then(
            [&options](const std::string& xml) { return importMusicXml(xml, options); });
    } catch (const FormatError& error) {
        return std::unexpected(error);
    }
}

std::expected<ImportResult, FormatError> importMusicXmlFile(const std::filesystem::path& path,
                                                            const MusicXmlOptions& options) {
    return detail::readFile(path).and_then([&options](const std::vector<std::uint8_t>& bytes) {
        return importMusicXmlBytes(bytes, options);
    });
}

} // namespace elara::io
