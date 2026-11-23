// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro import (ADR-0010). Guitar Pro 7 and 8 keep the score as GPIF XML
// inside a zip archive; this reader follows the structure of files saved by
// Guitar Pro (the format has no public specification).
#include <elara/core/validation.hpp>
#include <elara/io/guitar_pro.hpp>

#include "archive.hpp"
#include "midi_shared.hpp"
#include "warnings.hpp"
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elara::io {

namespace {

using core::Accent;
using core::Beat;
using core::BendCurve;
using core::BendPoint;
using core::Clef;
using core::Duration;
using core::Dynamic;
using core::Fraction;
using core::HarmonicKind;
using core::Instrument;
using core::InstrumentKind;
using core::KeyMode;
using core::KeySignature;
using core::Measure;
using core::Note;
using core::NoteValue;
using core::Pitch;
using core::SlideIn;
using core::SlideOut;
using core::Song;
using core::Staff;
using core::Step;
using core::Tempo;
using core::TimeSignature;
using core::Tuning;
using core::Tuplet;
using core::Vibrato;
using core::Voice;
using detail::Warnings;

// ---- XML helpers ---------------------------------------------------------------

std::string_view textOf(pugi::xml_node node) {
    return node.text().get();
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::optional<int> intOf(std::string_view text) {
    const std::string clean = trimmed(text);
    int value = 0;
    const auto [end, error] = std::from_chars(clean.data(), clean.data() + clean.size(), value);
    if (error != std::errc{} || end != clean.data() + clean.size() || clean.empty()) {
        return std::nullopt;
    }
    return value;
}

int intOf(pugi::xml_node node, int fallback) {
    return intOf(textOf(node)).value_or(fallback);
}

/// Space-separated integers ("0 1 -1 -1").
std::vector<int> intsOf(std::string_view text) {
    std::vector<int> values;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto start = text.find_first_not_of(" \t\r\n", at);
        if (start == std::string_view::npos) {
            break;
        }
        auto end = text.find_first_of(" \t\r\n", start);
        end = end == std::string_view::npos ? text.size() : end;
        if (const auto value = intOf(text.substr(start, end - start))) {
            values.push_back(*value);
        }
        at = end;
    }
    return values;
}

/// The `<Property name="...">` of a node's `<Properties>`.
pugi::xml_node property(pugi::xml_node owner, std::string_view name) {
    for (const pugi::xml_node node : owner.child("Properties").children("Property")) {
        if (name == node.attribute("name").value()) {
            return node;
        }
    }
    return {};
}

// ---- Instruments ---------------------------------------------------------------

/// GPIF clef names: the sign and the staff line it sits on.
Clef clefFor(std::string_view name, int octave) {
    if (name == "F4") {
        return octave == -1 ? Clef::Bass8vb : Clef::Bass;
    }
    if (name == "C3") {
        return Clef::Alto;
    }
    if (name == "C4") {
        return Clef::Tenor;
    }
    if (name == "Neutral") {
        return Clef::Percussion;
    }
    return octave == -1 ? Clef::Treble8vb : Clef::Treble;
}

struct TrackLayout {
    std::string name;
    Instrument instrument;
    std::vector<Staff> staves;
    std::vector<std::vector<int>> lowToHigh; // per staff: open strings, lowest first
    std::vector<int> drumKeys;               // per instrument articulation
    bool percussion = false;
};

std::optional<std::vector<int>> tuningOf(pugi::xml_node staff) {
    const pugi::xml_node tuning = property(staff, "Tuning");
    std::vector<int> pitches = intsOf(textOf(tuning.child("Pitches")));
    if (pitches.empty() || pitches.size() > static_cast<std::size_t>(Tuning::kMaxStrings) ||
        std::ranges::all_of(pitches, [](int pitch) { return pitch == 0; }) ||
        std::ranges::any_of(pitches, [](int pitch) { return pitch < 0 || pitch > 127; })) {
        return std::nullopt;
    }
    return pitches;
}

TrackLayout readTrack(pugi::xml_node track, std::size_t index,
                      const std::vector<std::string>& clefs, Warnings& warnings) {
    TrackLayout layout;
    const pugi::xml_node set = track.child("InstrumentSet");
    const std::string setName = trimmed(textOf(set.child("Name")));
    layout.name = trimmed(textOf(track.child("Name")));
    if (layout.name.empty()) {
        layout.name = setName.empty() ? std::format("Track {}", index + 1) : setName;
    }
    layout.percussion = textOf(set.child("Type")) == std::string_view("drumKit");

    // Drum notes name an articulation of the kit, counted across its elements.
    for (const pugi::xml_node element : set.child("Elements").children("Element")) {
        for (const pugi::xml_node articulation :
             element.child("Articulations").children("Articulation")) {
            layout.drumKeys.push_back(
                std::clamp(intOf(articulation.child("OutputMidiNumber"), 38), 0, 127));
        }
    }

    int program = 0;
    if (const pugi::xml_node sound = track.child("Sounds").child("Sound")) {
        program = intOf(sound.child("MIDI").child("Program"), 0);
    } else if (const pugi::xml_node midi = track.child("GeneralMidi")) {
        program = intOf(midi.child("Program"), 0);
    }
    program = std::clamp(program, 0, 127);

    const int chromatic = intOf(track.child("Transpose").child("Chromatic"), 0);
    const int octave = intOf(track.child("Transpose").child("Octave"), 0);

    std::vector<pugi::xml_node> staffNodes;
    for (const pugi::xml_node staff : track.child("Staves").children("Staff")) {
        staffNodes.push_back(staff);
    }
    if (staffNodes.empty()) {
        staffNodes.emplace_back(); // no <Staves>: one plain staff
    }
    bool fretted = false;
    for (std::size_t s = 0; s < staffNodes.size(); ++s) {
        const std::string_view clefName = s < clefs.size() ? clefs[s] : std::string_view("G2");
        const Clef clef = layout.percussion ? Clef::Percussion : clefFor(clefName, octave);
        const auto pitches = layout.percussion ? std::nullopt : tuningOf(staffNodes[s]);
        if (!pitches) {
            layout.staves.emplace_back(clef);
            layout.lowToHigh.emplace_back();
            continue;
        }
        fretted = true;
        const auto preference = property(staffNodes[s], "TuningFlat")
                                    ? core::SpellingPreference::Flats
                                    : core::SpellingPreference::Sharps;
        std::vector<Pitch> strings; // string 1 (highest) first
        for (auto it = pitches->rbegin(); it != pitches->rend(); ++it) {
            strings.push_back(Pitch::fromMidi(*it, preference));
        }
        int capo = intOf(property(staffNodes[s], "CapoFret").child("Fret"), 0);
        if (capo < 0 || capo > Tuning::kMaxFret) {
            warnings.add(layout.name, "capo outside the neck dropped");
            capo = 0;
        }
        layout.staves.emplace_back(clef, Tuning(strings), capo);
        layout.lowToHigh.push_back(*pitches);
    }

    InstrumentKind kind = InstrumentKind::Pitched;
    if (layout.percussion) {
        kind = InstrumentKind::Percussion;
    } else if (fretted) {
        kind = InstrumentKind::Fretted;
    } else if (layout.staves.size() >= 2 ||
               detail::kindForProgram(program, false) == InstrumentKind::Keyboard) {
        kind = InstrumentKind::Keyboard;
    }
    layout.instrument = Instrument{
        .name = setName.empty() ? layout.name : setName,
        .kind = kind,
        .midiProgram = layout.percussion ? 0 : program,
        .writtenOffset = layout.percussion ? 0 : -(chromatic + 12 * octave),
    };
    return layout;
}

// ---- Rhythms and notes -----------------------------------------------------------

std::optional<NoteValue> noteValueFor(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, NoteValue>, 9> kValues{{
        {"DoubleWhole", NoteValue::DoubleWhole},
        {"Whole", NoteValue::Whole},
        {"Half", NoteValue::Half},
        {"Quarter", NoteValue::Quarter},
        {"Eighth", NoteValue::Eighth},
        {"16th", NoteValue::Sixteenth},
        {"32nd", NoteValue::ThirtySecond},
        {"64th", NoteValue::SixtyFourth},
        {"128th", NoteValue::HundredTwentyEighth},
    }};
    for (const auto& [text, value] : kValues) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<Dynamic> dynamicFor(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, Dynamic>, 8> kDynamics{{
        {"PPP", Dynamic::PPP},
        {"PP", Dynamic::PP},
        {"P", Dynamic::P},
        {"MP", Dynamic::MP},
        {"MF", Dynamic::MF},
        {"F", Dynamic::F},
        {"FF", Dynamic::FF},
        {"FFF", Dynamic::FFF},
    }};
    for (const auto& [text, value] : kDynamics) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

/// GPIF pitches: a step, an accidental ("#", "b", "##", "bb" or nothing) and an
/// octave counted from MIDI 0 (C0 is MIDI 0, so middle C is C5).
std::optional<Pitch> pitchOf(pugi::xml_node pitch) {
    const std::string step = trimmed(textOf(pitch.child("Step")));
    static constexpr std::string_view kSteps = "CDEFGAB";
    const auto index = step.size() == 1 ? kSteps.find(step[0]) : std::string_view::npos;
    const auto octave = intOf(textOf(pitch.child("Octave")));
    if (index == std::string_view::npos || !octave) {
        return std::nullopt;
    }
    const std::string accidental = trimmed(textOf(pitch.child("Accidental")));
    int alter = 0;
    for (const char c : accidental) {
        alter += c == '#' ? 1 : c == 'b' ? -1 : 0;
    }
    alter += accidental == "x" ? 2 : 0;
    try {
        return Pitch(static_cast<Step>(index), alter, *octave - 1);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

/// Bends: values in hundredths of a tone (100 = a full step), offsets in
/// percent of the note.
BendCurve bendOf(pugi::xml_node note) {
    const auto value = [&](std::string_view name) -> std::optional<double> {
        const pugi::xml_node node = property(note, name);
        if (!node) {
            return std::nullopt;
        }
        return node.child("Float").text().as_double(0.0);
    };
    std::vector<std::pair<double, double>> raw; // (offset, value)
    raw.emplace_back(value("BendOriginOffset").value_or(0.0),
                     value("BendOriginValue").value_or(0.0));
    if (const auto middle = value("BendMiddleValue")) {
        if (const auto first = value("BendMiddleOffset1")) {
            raw.emplace_back(*first, *middle);
        }
        if (const auto second = value("BendMiddleOffset2")) {
            raw.emplace_back(*second, *middle);
        }
    }
    raw.emplace_back(value("BendDestinationOffset").value_or(100.0),
                     value("BendDestinationValue").value_or(0.0));
    std::vector<BendPoint> points;
    for (const auto& [offset, amount] : raw) {
        const int position = std::clamp(static_cast<int>(std::lround(offset)), 0, 100);
        const int cents = std::clamp(static_cast<int>(std::lround(amount * 2.0)),
                                     -BendCurve::kMaxCents, BendCurve::kMaxCents);
        if (points.empty() || position > points.back().position) {
            points.push_back(BendPoint{.position = position, .cents = cents});
        }
    }
    if (std::ranges::all_of(points, [](const BendPoint& point) { return point.cents == 0; })) {
        return {};
    }
    return BendCurve(std::move(points));
}

// ---- The score -----------------------------------------------------------------

/// Elements by id, for one GPIF collection (<Bars>, <Voices>...).
class ById {
public:
    ById(pugi::xml_node collection, const char* element) {
        for (const pugi::xml_node node : collection.children(element)) {
            if (const auto id = intOf(node.attribute("id").value())) {
                nodes_.emplace(*id, node);
            }
        }
    }

    [[nodiscard]] pugi::xml_node find(int id) const {
        const auto it = nodes_.find(id);
        return it == nodes_.end() ? pugi::xml_node{} : it->second;
    }

private:
    std::unordered_map<int, pugi::xml_node> nodes_;
};

class GpifReader {
public:
    GpifReader(pugi::xml_node root, Warnings& warnings)
        : root_(root), bars_(root.child("Bars"), "Bar"), voices_(root.child("Voices"), "Voice"),
          beats_(root.child("Beats"), "Beat"), notes_(root.child("Notes"), "Note"),
          rhythms_(root.child("Rhythms"), "Rhythm"), warnings_(warnings) {}

    Song read() {
        Song song;
        readInfo(song);
        for (const pugi::xml_node masterBar : root_.child("MasterBars").children("MasterBar")) {
            masterBars_.push_back(masterBar);
        }
        readTracks(song);
        song.appendMeasures(masterBars_.size());
        readHeaders(song);
        readTempos(song);
        for (std::size_t m = 0; m < masterBars_.size(); ++m) {
            readMeasure(song, m);
        }
        return song;
    }

private:
    void readInfo(Song& song) const {
        const pugi::xml_node score = root_.child("Score");
        core::SongInfo& info = song.info();
        const auto field = [&](const char* name) {
            return trimmed(textOf(score.child(name)));
        };
        info.title = field("Title");
        info.subtitle = field("SubTitle");
        info.artist = field("Artist");
        info.album = field("Album");
        info.words = field("Words");
        info.music = field("Music");
        if (const std::string both = field("WordsAndMusic"); !both.empty()) {
            info.words = info.words.empty() ? both : info.words;
            info.music = info.music.empty() ? both : info.music;
        }
        info.copyright = field("Copyright");
        info.transcriber = field("Tabber");
        info.notes = field("Notices");
    }

    void readTracks(Song& song) {
        std::size_t index = 0;
        for (const pugi::xml_node track : root_.child("Tracks").children("Track")) {
            // Each staff's clef comes from its first bar.
            const std::size_t staffCount =
                std::max<std::size_t>(1, static_cast<std::size_t>(std::distance(
                                             track.child("Staves").children("Staff").begin(),
                                             track.child("Staves").children("Staff").end())));
            std::vector<std::string> clefs;
            for (std::size_t s = 0; s < staffCount; ++s) {
                clefs.push_back(trimmed(textOf(firstBar(staffOffset_ + s).child("Clef"))));
            }
            TrackLayout layout = readTrack(track, index, clefs, warnings_);
            if (hasLyrics(track)) {
                warnings_.add(layout.name, "lyrics dropped");
            }
            song.addTrack(core::Track(layout.name, layout.instrument, layout.staves));
            for (std::size_t s = 0; s < layout.staves.size(); ++s) {
                staves_.push_back(StaffRef{.track = index, .staff = s});
            }
            staffOffset_ += layout.staves.size();
            layouts_.push_back(std::move(layout));
            ++index;
        }
    }

    [[nodiscard]] pugi::xml_node firstBar(std::size_t flatStaff) const {
        if (masterBars_.empty()) {
            return {};
        }
        const std::vector<int> ids = intsOf(textOf(masterBars_.front().child("Bars")));
        return flatStaff < ids.size() ? bars_.find(ids[flatStaff]) : pugi::xml_node{};
    }

    static bool hasLyrics(pugi::xml_node track) {
        for (const pugi::xml_node line : track.child("Lyrics").children("Line")) {
            if (!trimmed(textOf(line.child("Text"))).empty()) {
                return true;
            }
        }
        return false;
    }

    void readHeaders(Song& song) {
        TimeSignature time;
        KeySignature key;
        for (std::size_t m = 0; m < masterBars_.size(); ++m) {
            const pugi::xml_node bar = masterBars_[m];
            if (const pugi::xml_node node = bar.child("Time")) {
                if (const auto parsed = TimeSignature::parse(trimmed(textOf(node)))) {
                    time = *parsed;
                } else {
                    warnings_.add({}, "unsupported time signatures replaced by the previous one",
                                  m);
                }
            }
            if (const pugi::xml_node node = bar.child("Key")) {
                const int fifths = intOf(node.child("AccidentalCount"), 0);
                const KeyMode mode = textOf(node.child("Mode")) == std::string_view("Minor")
                                         ? KeyMode::Minor
                                         : KeyMode::Major;
                if (std::abs(fifths) <= KeySignature::kMaxFifths) {
                    key = KeySignature(fifths, mode);
                }
            }
            song.measureHeader(m).timeSignature = time;
            song.measureHeader(m).keySignature = key;
            warnMasterBar(bar, m);
        }
    }

    void warnMasterBar(pugi::xml_node bar, std::size_t m) {
        if (bar.child("Repeat")) {
            warnings_.add({}, "repeats dropped (written once)", m);
        }
        if (bar.child("AlternateEndings")) {
            warnings_.add({}, "alternate endings dropped", m);
        }
        if (bar.child("Section")) {
            warnings_.add({}, "section marks dropped", m);
        }
        if (bar.child("Fermatas")) {
            warnings_.add({}, "fermatas dropped", m);
        }
        if (bar.child("Directions")) {
            warnings_.add({}, "jumps (coda, segno) dropped", m);
        }
        if (const pugi::xml_node feel = bar.child("TripletFeel");
            feel && textOf(feel) != std::string_view("NoTripletFeel")) {
            warnings_.add({}, "triplet feel dropped", m);
        }
    }

    /// Tempo automations: "bpm unit" at a bar and a position inside it (a share
    /// of the bar).
    void readTempos(Song& song) {
        for (const pugi::xml_node automation :
             root_.child("MasterTrack").child("Automations").children("Automation")) {
            if (textOf(automation.child("Type")) != std::string_view("Tempo")) {
                continue;
            }
            // "120 2": the bpm (it may have decimals), then the beat unit.
            const std::string value = trimmed(textOf(automation.child("Value")));
            const double bpm = automation.child("Value").text().as_double(0.0);
            if (!(bpm >= Tempo::kMinBpm && bpm <= Tempo::kMaxBpm)) {
                warnings_.add({}, "tempos outside 10-999 bpm dropped");
                continue;
            }
            const auto space = value.find_first_of(" 	");
            const int unit = space == std::string::npos
                                 ? 2
                                 : intOf(std::string_view(value).substr(space)).value_or(2);
            const Tempo tempo(bpm, beatUnitFor(unit));
            const int bar = intOf(automation.child("Bar"), 0);
            const double position =
                std::clamp(automation.child("Position").text().as_double(0.0), 0.0, 1.0);
            if (automation.child("Linear").text().as_bool(false)) {
                warnings_.add({}, "gradual tempo changes written as sudden ones");
            }
            if (bar == 0 && position == 0.0) {
                song.setStartTempo(tempo);
                continue;
            }
            if (bar < 0 || static_cast<std::size_t>(bar) >= song.measureCount()) {
                continue;
            }
            core::MeasureHeader& header = song.measureHeader(static_cast<std::size_t>(bar));
            constexpr std::int64_t kSteps = 960;
            const Fraction offset =
                header.timeSignature.measureLength() *
                Fraction(static_cast<std::int64_t>(std::llround(position * kSteps)), kSteps);
            if (offset >= header.timeSignature.measureLength()) {
                continue;
            }
            auto& changes = header.tempoChanges;
            const auto at =
                std::ranges::find_if(changes, [&offset](const core::TempoChange& change) {
                    return change.offset >= offset;
                });
            if (at != changes.end() && at->offset == offset) {
                at->tempo = tempo;
            } else {
                changes.insert(at, core::TempoChange{.offset = offset, .tempo = tempo});
            }
        }
    }

    /// The beat unit of a tempo automation's second number.
    static Duration beatUnitFor(int code) {
        switch (code) {
        case 1:
            return Duration(NoteValue::Eighth);
        case 3:
            return Duration(NoteValue::Quarter, 1);
        case 4:
            return Duration(NoteValue::Half);
        case 5:
            return Duration(NoteValue::Half, 1);
        default:
            return Duration(NoteValue::Quarter);
        }
    }

    void readMeasure(Song& song, std::size_t m) {
        const std::vector<int> ids = intsOf(textOf(masterBars_[m].child("Bars")));
        if (ids.size() < staves_.size()) {
            warnings_.add({}, "bars missing from the score left empty", m);
        }
        const KeySignature& key = song.measureHeader(m).keySignature;
        const TimeSignature& time = song.measureHeader(m).timeSignature;
        for (std::size_t flat = 0; flat < staves_.size() && flat < ids.size(); ++flat) {
            const pugi::xml_node bar = bars_.find(ids[flat]);
            if (!bar) {
                throw FormatError{std::format("Guitar Pro: measure {} refers to missing bar {}",
                                              m + 1, ids[flat])};
            }
            const StaffRef ref = staves_[flat];
            const TrackLayout& layout = layouts_[ref.track];
            Staff& staff = song.track(ref.track).staff(ref.staff);
            if (m > 0 && !layout.percussion) {
                const std::string clef = trimmed(textOf(bar.child("Clef")));
                if (!clef.empty() && clefFor(clef, -1) != clefFor(clefNameOf(staff.clef()), -1)) {
                    warnings_.add(layout.name, "clef changes dropped", m);
                }
            }
            if (bar.child("SimileMark")) {
                warnings_.add(layout.name, "repeat signs (simile) dropped", m);
            }
            Measure& measure = staff.measure(m);
            const std::vector<int> voiceIds = intsOf(textOf(bar.child("Voices")));
            for (std::size_t v = 0; v < voiceIds.size(); ++v) {
                if (voiceIds[v] < 0) {
                    continue;
                }
                if (v >= Measure::kMaxVoices) {
                    warnings_.add(layout.name, "voices beyond the fourth dropped", m);
                    break;
                }
                const pugi::xml_node voiceNode = voices_.find(voiceIds[v]);
                if (!voiceNode) {
                    throw FormatError{std::format(
                        "Guitar Pro: measure {} refers to missing voice {}", m + 1, voiceIds[v])};
                }
                Voice& voice = measure.voices[v];
                for (const int beatId : intsOf(textOf(voiceNode.child("Beats")))) {
                    if (auto beat = readBeat(beatId, layout, ref.staff, staff, key, m)) {
                        voice.beats.push_back(std::move(*beat));
                    }
                }
                const core::VoiceFill fill = core::voiceFill(voice, time);
                if (fill == core::VoiceFill::Incomplete && !voice.isEmpty()) {
                    (void)core::fillWithRests(voice, time);
                    warnings_.add(layout.name, "short voices filled with rests", m);
                } else if (fill == core::VoiceFill::Overfull) {
                    warnings_.add(layout.name, "voices longer than their measure kept as written",
                                  m);
                }
            }
        }
    }

    static std::string_view clefNameOf(Clef clef) {
        switch (clef) {
        case Clef::Bass:
        case Clef::Bass8vb:
            return "F4";
        case Clef::Alto:
            return "C3";
        case Clef::Tenor:
            return "C4";
        case Clef::Percussion:
            return "Neutral";
        case Clef::Treble:
        case Clef::Treble8vb:
            return "G2";
        }
        return "G2";
    }

    Duration durationOf(int rhythmId, const std::string& who, std::size_t m) {
        if (const auto known = durations_.find(rhythmId); known != durations_.end()) {
            return known->second;
        }
        const pugi::xml_node rhythm = rhythms_.find(rhythmId);
        if (!rhythm) {
            throw FormatError{
                std::format("Guitar Pro: measure {} refers to missing rhythm {}", m + 1, rhythmId)};
        }
        const auto value = noteValueFor(trimmed(textOf(rhythm.child("NoteValue"))));
        if (!value) {
            warnings_.add(who, "note values shorter than a 128th written as 128ths", m);
        }
        const int dots = std::clamp(rhythm.child("AugmentationDot").attribute("count").as_int(0), 0,
                                    Duration::kMaxDots);
        Tuplet tuplet;
        if (const pugi::xml_node primary = rhythm.child("PrimaryTuplet")) {
            const int actual = primary.attribute("num").as_int(1);
            const int normal = primary.attribute("den").as_int(1);
            if (actual >= 1 && normal >= 1 && actual <= Tuplet::kMaxCount &&
                normal <= Tuplet::kMaxCount) {
                tuplet = Tuplet(actual, normal);
            }
        }
        if (rhythm.child("SecondaryTuplet")) {
            warnings_.add(who, "nested tuplets written as their outer tuplet", m);
        }
        const Duration duration(value.value_or(NoteValue::HundredTwentyEighth), dots, tuplet);
        durations_.emplace(rhythmId, duration);
        return duration;
    }

    std::optional<Beat> readBeat(int beatId, const TrackLayout& layout, std::size_t staffIndex,
                                 const Staff& staff, const KeySignature& key, std::size_t m) {
        const pugi::xml_node node = beats_.find(beatId);
        if (!node) {
            throw FormatError{
                std::format("Guitar Pro: measure {} refers to missing beat {}", m + 1, beatId)};
        }
        const std::string& who = layout.name;
        if (node.child("GraceNotes")) {
            warnings_.add(who, "grace notes dropped", m);
            return std::nullopt;
        }
        Beat beat{.duration = durationOf(node.child("Rhythm").attribute("ref").as_int(-1), who, m)};
        warnBeat(node, who, m);
        readBeatEffects(node, beat.effects, who, m);
        const Dynamic dynamic =
            dynamicFor(trimmed(textOf(node.child("Dynamic")))).value_or(Dynamic::MF);
        for (const int noteId : intsOf(textOf(node.child("Notes")))) {
            const pugi::xml_node noteNode = notes_.find(noteId);
            if (!noteNode) {
                throw FormatError{
                    std::format("Guitar Pro: measure {} refers to missing note {}", m + 1, noteId)};
            }
            if (auto note = readNote(noteNode, layout, staffIndex, staff, key, m)) {
                note->effects.dynamic = dynamic;
                beat.notes.push_back(std::move(*note));
            }
        }
        return beat;
    }

    void warnBeat(pugi::xml_node node, const std::string& who, std::size_t m) {
        if (node.child("FreeText")) {
            warnings_.add(who, "text dropped", m);
        }
        if (node.child("Chord")) {
            warnings_.add(who, "chord names and diagrams dropped", m);
        }
        if (node.child("Fadding")) {
            warnings_.add(who, "volume swells dropped", m);
        }
        if (node.child("Wah")) {
            warnings_.add(who, "wah dropped", m);
        }
        if (node.child("Arpeggio")) {
            warnings_.add(who, "arpeggios dropped", m);
        }
        if (node.child("Hairpin")) {
            warnings_.add(who, "crescendos and diminuendos dropped", m);
        }
        if (node.child("Ottavia")) {
            warnings_.add(who, "octave signs dropped", m);
        }
        if (property(node, "WhammyBar")) {
            warnings_.add(who, "whammy bar dropped", m);
        }
    }

    void readBeatEffects(pugi::xml_node node, core::BeatEffects& effects, const std::string& who,
                         std::size_t m) {
        const auto direction = [&](std::string_view name) -> std::string_view {
            return textOf(property(node, name).child("Direction"));
        };
        if (const std::string_view pick = direction("PickStroke"); !pick.empty()) {
            effects.pickStroke = pick == "Up" ? core::PickStroke::Up : core::PickStroke::Down;
        }
        if (const std::string_view brush = direction("Brush"); !brush.empty()) {
            effects.strum = brush == "Up" ? core::Strum::Up : core::Strum::Down;
        }
        if (property(node, "Slapped")) {
            effects.bassTechnique = core::BassTechnique::Slap;
        } else if (property(node, "Popped")) {
            effects.bassTechnique = core::BassTechnique::Pop;
        }
        if (const pugi::xml_node tremolo = node.child("Tremolo")) {
            const std::string speed = trimmed(textOf(tremolo));
            if (speed == "1/2") {
                effects.tremoloPicking = NoteValue::Eighth;
            } else if (speed == "1/4") {
                effects.tremoloPicking = NoteValue::Sixteenth;
            } else if (speed == "1/8") {
                effects.tremoloPicking = NoteValue::ThirtySecond;
            } else {
                warnings_.add(who, "unknown tremolo picking speeds dropped", m);
            }
        }
    }

    std::optional<Note> readNote(pugi::xml_node node, const TrackLayout& layout,
                                 std::size_t staffIndex, const Staff& staff,
                                 const KeySignature& key, std::size_t m) {
        const std::string& who = layout.name;
        const auto midiProperty = [&]() -> std::optional<int> {
            if (const pugi::xml_node midi = property(node, "Midi")) {
                return intOf(textOf(midi.child("Number")));
            }
            return std::nullopt;
        };
        Note note;
        if (layout.percussion) {
            std::optional<int> drum = midiProperty();
            if (!drum) {
                const auto articulation = intOf(textOf(node.child("InstrumentArticulation")));
                if (articulation && *articulation >= 0 &&
                    static_cast<std::size_t>(*articulation) < layout.drumKeys.size()) {
                    drum = layout.drumKeys[static_cast<std::size_t>(*articulation)];
                }
            }
            note.pitch = Pitch::fromMidi(std::clamp(drum.value_or(38), 0, 127));
        } else {
            const std::vector<int>& strings = layout.lowToHigh[staffIndex];
            // Strings count from 0, the lowest; frets from the capo.
            const auto stringIndex = intOf(textOf(property(node, "String").child("String")));
            const auto fret = intOf(textOf(property(node, "Fret").child("Fret")));
            std::optional<int> midi = midiProperty();
            std::optional<core::FretPosition> position;
            if (!strings.empty() && stringIndex && fret && *stringIndex >= 0 &&
                static_cast<std::size_t>(*stringIndex) < strings.size()) {
                const int open = strings[static_cast<std::size_t>(*stringIndex)];
                int relative = *fret;
                // Frets count from the capo; a sounding pitch that says
                // otherwise wins.
                if (midi && open + staff.capo() + relative != *midi && open + relative == *midi) {
                    relative -= staff.capo();
                }
                if (relative >= 0 && staff.capo() + relative <= Tuning::kMaxFret) {
                    position =
                        core::FretPosition{.string = static_cast<std::int8_t>(
                                               static_cast<int>(strings.size()) - *stringIndex),
                                           .fret = static_cast<std::int8_t>(relative)};
                    midi = midi.value_or(open + staff.capo() + relative);
                    if (*midi != open + staff.capo() + relative) {
                        position.reset(); // inconsistent: keep the sound
                        warnings_.add(who, "fret positions that disagree with the pitch dropped",
                                      m);
                    }
                }
            }
            const auto concert = pitchOf(property(node, "ConcertPitch").child("Pitch"));
            if (!midi && concert) {
                midi = concert->midiNumber();
            }
            if (!midi || *midi < 0 || *midi > 127) {
                warnings_.add(who, "notes without a pitch dropped", m);
                return std::nullopt;
            }
            note.pitch = concert && concert->midiNumber() == *midi ? *concert : key.spell(*midi);
            note.position = position;
        }
        readNoteEffects(node, note.effects, who, m);
        return note;
    }

    void readNoteEffects(pugi::xml_node node, core::NoteEffects& effects, const std::string& who,
                         std::size_t m) {
        effects.tiedFromPrevious = node.child("Tie").attribute("destination").as_bool(false);
        effects.letRing = static_cast<bool>(node.child("LetRing"));
        if (const pugi::xml_node vibrato = node.child("Vibrato")) {
            effects.vibrato =
                textOf(vibrato) == std::string_view("Wide") ? Vibrato::Wide : Vibrato::Slight;
        }
        if (const pugi::xml_node accent = node.child("Accent")) {
            const int flags = intOf(accent, 0);
            effects.staccato = (flags & 1) != 0;
            if ((flags & 4) != 0) {
                effects.accent = Accent::Marcato;
            } else if ((flags & 8) != 0) {
                effects.accent = Accent::Accent;
            }
            if ((flags & 16) != 0) {
                warnings_.add(who, "tenuto marks dropped", m);
            }
        }
        effects.ghost = static_cast<bool>(node.child("AntiAccent"));
        effects.palmMute = static_cast<bool>(property(node, "PalmMuted"));
        effects.deadNote = static_cast<bool>(property(node, "Muted"));
        effects.legatoFromPrevious = static_cast<bool>(property(node, "HopoDestination"));
        if (const pugi::xml_node slide = property(node, "Slide")) {
            const int flags = intOf(slide.child("Flags"), 0);
            if ((flags & 1) != 0) {
                effects.slideOut = SlideOut::Shift;
            } else if ((flags & 2) != 0) {
                effects.slideOut = SlideOut::Legato;
            } else if ((flags & 4) != 0) {
                effects.slideOut = SlideOut::Downwards;
            } else if ((flags & 8) != 0) {
                effects.slideOut = SlideOut::Upwards;
            }
            if ((flags & 16) != 0) {
                effects.slideIn = SlideIn::FromBelow;
            } else if ((flags & 32) != 0) {
                effects.slideIn = SlideIn::FromAbove;
            }
        }
        if (property(node, "Bended")) {
            effects.bend = bendOf(node);
        }
        if (const pugi::xml_node type = property(node, "HarmonicType")) {
            const std::string kind = trimmed(textOf(type.child("HType")));
            const double fret =
                property(node, "HarmonicFret").child("HFret").text().as_double(12.0);
            const auto tenths =
                static_cast<std::int16_t>(std::clamp(std::lround(fret * 10.0), 0L, 300L));
            if (kind == "Natural") {
                effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = tenths};
            } else if (kind == "Artificial") {
                effects.harmonic = {.kind = HarmonicKind::Artificial, .fretTenths = tenths};
            } else if (kind == "Pinch") {
                effects.harmonic = {.kind = HarmonicKind::Pinch, .fretTenths = tenths};
            } else if (kind == "Tap") {
                effects.harmonic = {.kind = HarmonicKind::Tapped, .fretTenths = tenths};
            } else if (kind == "Semi") {
                effects.harmonic = {.kind = HarmonicKind::Semi, .fretTenths = tenths};
            } else if (!kind.empty() && kind != "NoHarmonic") {
                warnings_.add(who, "feedback harmonics dropped", m);
            }
        }
        if (property(node, "Tapped") || property(node, "LeftHandTapped")) {
            warnings_.add(who, "tapping dropped", m);
        }
        if (node.child("Trill")) {
            warnings_.add(who, "trills dropped", m);
        }
        if (node.child("Ornament")) {
            warnings_.add(who, "ornaments dropped", m);
        }
    }

    struct StaffRef {
        std::size_t track = 0;
        std::size_t staff = 0;
    };

    pugi::xml_node root_;
    ById bars_;
    ById voices_;
    ById beats_;
    ById notes_;
    ById rhythms_;
    Warnings& warnings_;
    std::vector<pugi::xml_node> masterBars_;
    std::vector<TrackLayout> layouts_;
    std::vector<StaffRef> staves_; // every staff of every track, in score order
    std::size_t staffOffset_ = 0;
    std::unordered_map<int, Duration> durations_;
};

bool startsWith(std::span<const std::uint8_t> bytes, std::string_view prefix, std::size_t at = 0) {
    if (bytes.size() < at + prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at),
                      [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
}

} // namespace

std::expected<ImportResult, FormatError> importGpif(std::string_view xml) {
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
    if (!parsed) {
        return std::unexpected(FormatError{std::format("Guitar Pro: damaged score: {} at byte {}",
                                                       parsed.description(), parsed.offset)});
    }
    const pugi::xml_node root = document.child("GPIF");
    if (!root) {
        return std::unexpected(FormatError{"Guitar Pro: the score has no <GPIF> element"});
    }
    Warnings warnings;
    try {
        ImportResult result;
        result.song = GpifReader(root, warnings).read();
        result.warnings = warnings.lines();
        return result;
    } catch (const FormatError& error) {
        return std::unexpected(error);
    } catch (const std::invalid_argument& error) {
        return std::unexpected(FormatError{std::format("Guitar Pro: {}", error.what())});
    } catch (const std::out_of_range& error) {
        return std::unexpected(FormatError{std::format("Guitar Pro: {}", error.what())});
    }
}

std::expected<ImportResult, FormatError> importGuitarPro(std::span<const std::uint8_t> bytes) {
    if (detail::looksLikeZip(bytes)) {
        detail::ZipReader zip(bytes);
        if (!zip.ok()) {
            return std::unexpected(FormatError{"Guitar Pro: damaged zip archive"});
        }
        std::optional<std::string> score;
        try {
            score = zip.entry("Content/score.gpif");
        } catch (const FormatError& error) {
            return std::unexpected(error);
        }
        if (!score) {
            return std::unexpected(
                FormatError{"Guitar Pro: no Content/score.gpif in the archive (not a Guitar Pro 7 "
                            "or 8 file)"});
        }
        return importGpif(*score);
    }
    if (startsWith(bytes, "BCFZ") || startsWith(bytes, "BCFS")) {
        return std::unexpected(
            FormatError{"Guitar Pro: Guitar Pro 6 files (.gpx) are not supported yet"});
    }
    if (startsWith(bytes, "FICHIER GUITAR PRO", 1) || startsWith(bytes, "FICHIER GUITARE PRO", 1)) {
        return std::unexpected(
            FormatError{"Guitar Pro: Guitar Pro 3-5 files are not supported yet"});
    }
    return std::unexpected(FormatError{"Guitar Pro: not a Guitar Pro file"});
}

std::expected<ImportResult, FormatError> importGuitarProFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(FormatError{std::format("cannot open {}", path.string())});
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    return importGuitarPro(bytes);
}

} // namespace elara::io
