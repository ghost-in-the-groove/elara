// SPDX-License-Identifier: AGPL-3.0-or-later
// The steps of `elara-cli edit` (edit_steps.hpp): each parsed and run against
// a document as an undoable edit.
#include "edit_steps.hpp"

#include <elara/core/clipboard.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/edit_session.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
#include <elara/core/names.hpp>
#include <elara/core/refusal.hpp>
#include <elara/core/retune.hpp>
#include <elara/core/transpose.hpp>
#include <elara/core/validation.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

// Every form of step, in the order the help lists them.
constexpr auto kStepForms = std::to_array<std::string_view>({
    "add-track <preset> [at]",
    "remove-track <i>",
    "rename-track <i> [name]",
    "insert-measures <at> [n]",
    "append-measures <n>",
    "remove-measures <at> [n]",
    "time <m> <N/D>",
    "key <m> <key>",
    "mix <track> <volume 0..1> <pan -1..1> [mute] [solo]",
    "tempo <bpm>",
    "tempo-from <m> <bpm|none>",
    "tempo-gradual <m> <on|off>",
    "feel <m> <none|eighth|sixteenth|dotted-eighth|dotted-sixteenth|scottish-eighth|"
    "scottish-sixteenth>",
    "sign <m> <segno|segno-segno|coda|double-coda|fine|to-coda|to-double-coda>...",
    "sign <m> none",
    "jump <m> <da-capo|dal-segno|dal-segno-segno|none> [al-fine|al-coda|al-double-coda]",
    "repeat <m> [start] [end <times>]",
    "repeat <m> none",
    "ending <m> <time>...",
    "ending <m> none",
    "barline <m> <double|single>",
    "section <m> [name]",
    "break <m> <system|page|keep|auto> [track <t>]",
    "title <text>",
    "credit <title|subtitle|artist|album|words|music|copyright|transcriber|notes> [text]",
    "tuning <track> [<preset>|<notes>] [capo <n>] [staff <s>] [frets]",
    "note <track> <m> <string> <fret> <duration>",
    "rest <track> <m> <duration>",
    "fill <track> <m>",
    "copy <m> <n> [track <t> [<count>]] [from <song>]",
    "paste <m> [track <t>] [insert]",
    "transpose <semitones> [measures <m> <n>] [tracks <t>...]",
    "caret <track> <m> <beat> [string]",
    "voice <1-4>",
    "right",
    "left",
    "up",
    "down",
    "next-measure",
    "previous-measure",
    "fret <n>",
    "pitch <C..B> [add]",
    "shift <semitones>",
    "duration <d>",
    "delete",
    "delete-beat",
    "insert-rest",
    "drum [key]",
    "effect <name>",
    "bend <form> [cents]",
    "strum <down|up|none>",
    "pick <down|up|none>",
    "tremolo <8|16|32|none>",
    "whammy <dive|dip|pre-dive|return|none> [cents]",
    "bass <tap|slap|pop|none>",
    "grace <semitones> [duration] [on]",
    "grace none",
    "dynamic <ppp..fff>",
    "hairpin <crescendo|diminuendo|none>",
    "fermata <short|normal|long|none>",
    "slur <on|none>",
    "ottava <8va|8vb|15ma|15mb|none>",
    "pedal <down|change|none>",
    "breath <comma|caesura|none>",
    "chord <name|none>",
    "text <words|none>",
    "lyrics <verse> <measure> <text>",
    "lyrics <verse> none",
    "clef <treble|treble-8vb|bass|bass-8vb|alto|tenor>",
    "simile <1|2|none>",
    "slide-in <from-below|from-above|none>",
    "slide-out <shift|legato|downwards|upwards|none>",
    "vibrato <slight|wide|none>",
    "accent <accent|marcato|none>",
    "trill <on|semitones|none> [16|32|64]",
    "ornament <mordent|upper-mordent|turn|inverted-turn|none>",
    "fingering <1-5|t|none>...",
    "pluck <p|i|m|a|c|none>",
    "harmonic <natural|artificial|pinch|tapped|semi|none>",
    "undo",
    "redo",
    "save",
});

// Whether a form of step starts with `verb`.
bool knownVerb(std::string_view verb) {
    return std::ranges::any_of(kStepForms, [verb](std::string_view form) {
        return form.substr(0, form.find(' ')) == verb;
    });
}

using core::Document;
using core::Song;

std::vector<std::string_view> splitWords(std::string_view text) {
    std::vector<std::string_view> words;
    while (!text.empty()) {
        const auto start = text.find_first_not_of(' ');
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        const auto end = std::min(text.find(' '), text.size());
        words.push_back(text.substr(0, end));
        text.remove_prefix(end);
    }
    return words;
}

class Step {
public:
    explicit Step(std::string_view text) : words_(splitWords(text)) {
        if (words_.empty()) {
            throw StepSyntaxError("empty step");
        }
    }

    [[nodiscard]] std::string_view verb() const { return words_.front(); }
    [[nodiscard]] bool has(std::size_t i) const { return i < words_.size(); }

    [[nodiscard]] std::string_view word(std::size_t i) const {
        if (!has(i)) {
            throw StepSyntaxError(std::format("{}: missing argument {}", verb(), i));
        }
        return words_[i];
    }

    template <typename T>
    [[nodiscard]] T number(std::size_t i) const {
        std::string_view text = word(i);
        if (text.size() > 1 && text.front() == '+') {
            text.remove_prefix(1); // "+2" as well as "2"
        }
        T value{};
        const auto* end = text.data() + text.size();
        const auto [ptr, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc{} || ptr != end) {
            throw StepSyntaxError(std::format("{}: not a number: {}", verb(), text));
        }
        return value;
    }

    [[nodiscard]] std::size_t index(std::size_t i) const { return number<std::size_t>(i); }

    /// A decimal such as "0.75" or "-1" (strtod: from_chars for floating point
    /// is not on every toolchain yet).
    [[nodiscard]] double decimal(std::size_t i) const {
        const std::string text(word(i));
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size()) {
            throw StepSyntaxError(std::format("{}: not a number: {}", verb(), text));
        }
        return value;
    }

    /// Words from `i` to the end, joined by single spaces.
    [[nodiscard]] std::string rest(std::size_t i) const {
        std::string text(word(i));
        for (std::size_t j = i + 1; j < words_.size(); ++j) {
            (text += ' ') += words_[j];
        }
        return text;
    }

    /// The step's text from word `i` to its end as written, spaces between
    /// words kept.
    [[nodiscard]] std::string rawRest(std::size_t i) const {
        const std::string_view from = word(i);
        const std::string_view last = words_.back();
        return {from.data(), static_cast<std::size_t>(last.data() + last.size() - from.data())};
    }

    /// A note name from C to B.
    [[nodiscard]] core::Step noteName(std::size_t i) const {
        static constexpr std::string_view kNames = "CDEFGAB";
        const std::string_view text = word(i);
        const auto found = text.size() == 1 ? kNames.find(text.front()) : std::string_view::npos;
        if (found == std::string_view::npos) {
            throw StepSyntaxError(std::format("{}: not a note name (C to B): {}", verb(), text));
        }
        return static_cast<core::Step>(found);
    }

    [[nodiscard]] core::Duration duration(std::size_t i) const {
        const auto parsed = core::Duration::parse(word(i));
        if (!parsed) {
            throw StepSyntaxError(std::format("{}: not a duration: {}", verb(), word(i)));
        }
        return *parsed;
    }

private:
    std::vector<std::string_view> words_;
};

// Note-level edits replace the first staff's measure with an edited copy (ADR-0005).
template <typename Change>
std::unique_ptr<core::Command> editMeasure(const Song& song, std::size_t track, std::size_t measure,
                                           Change change) {
    core::Measure content = song.track(track).staff(0).measure(measure);
    change(content);
    return std::make_unique<core::ReplaceMeasure>(track, 0, measure, std::move(content));
}

// The forms `bend <form> [cents]` takes (ADR-0055).
constexpr std::array<std::pair<std::string_view, core::BendForm>, 5> kBendForms{{
    {"none", core::BendForm::None},
    {"bend", core::BendForm::Bend},
    {"release", core::BendForm::BendRelease},
    {"pre-bend", core::BendForm::PreBend},
    {"pre-release", core::BendForm::PreBendRelease},
}};

// Whammy-bar forms (ADR-0060), named for a dive: positive cents pull the bar
// up instead.
constexpr std::array<std::pair<std::string_view, core::BendForm>, 5> kWhammyForms{{
    {"none", core::BendForm::None},
    {"dive", core::BendForm::Bend},
    {"dip", core::BendForm::BendRelease},
    {"pre-dive", core::BendForm::PreBend},
    {"return", core::BendForm::PreBendRelease},
}};

// The repeated notes' value `tremolo` takes, or none.
std::optional<core::NoteValue> tremoloSpeed(std::string_view word) {
    if (word == "none") {
        return std::nullopt;
    }
    static constexpr std::array<std::pair<std::string_view, core::NoteValue>, 3> kSpeeds{{
        {"8", core::NoteValue::Eighth},
        {"16", core::NoteValue::Sixteenth},
        {"32", core::NoteValue::ThirtySecond},
    }};
    const auto found =
        std::ranges::find_if(kSpeeds, [word](const auto& speed) { return speed.first == word; });
    if (found == kSpeeds.end()) {
        throw StepSyntaxError(std::format("tremolo: 8, 16, 32 or none, not {}", word));
    }
    return found->second;
}

// A direction `strum` and `pick` take (ADR-0056): down, up or none.
template <core::NamedEnum Direction>
Direction strokeDirection(std::string_view verb, std::string_view word) {
    const std::optional<Direction> direction = core::parseName<Direction>(word);
    if (!direction) {
        throw StepSyntaxError(std::format("{}: down, up or none, not {}", verb, word));
    }
    return *direction;
}

// The speed a trill alternates its notes at (ADR-0090).
core::NoteValue trillSpeed(std::string_view word) {
    static constexpr std::array<std::pair<std::string_view, core::NoteValue>, 3> kSpeeds{{
        {"16", core::NoteValue::Sixteenth},
        {"32", core::NoteValue::ThirtySecond},
        {"64", core::NoteValue::SixtyFourth},
    }};
    const auto found =
        std::ranges::find_if(kSpeeds, [word](const auto& speed) { return speed.first == word; });
    if (found == kSpeeds.end()) {
        throw StepSyntaxError(std::format("trill: 16, 32 or 64, not {}", word));
    }
    return found->second;
}

// The kind `word` names among those `verb` takes: the values of `Kind` by
// their names in the model (core::EnumNames), but `leftOut`, which the step
// does not take.
template <core::NamedEnum Kind>
Kind kindOf(std::string_view verb, std::string_view word,
            std::optional<Kind> leftOut = std::nullopt) {
    const std::optional<Kind> kind = core::parseName<Kind>(word);
    if (!kind || kind == leftOut) {
        std::string all;
        for (const Kind value : core::enumValues<Kind>()) {
            if (value != leftOut) {
                all += std::format("{}{}", all.empty() ? "" : ", ", core::nameOf(value));
            }
        }
        throw StepSyntaxError(std::format("{}: {}, not {}", verb, all, word));
    }
    return *kind;
}

// The names of `Kind` as a choice: "a, b or c".
template <core::NamedEnum Kind>
std::string choiceOf() {
    std::string all;
    const auto values = core::enumValues<Kind>();
    for (std::size_t i = 0; i < values.size(); ++i) {
        all += i == 0 ? "" : i + 1 == values.size() ? " or " : ", ";
        all += core::nameOf(values[i]);
    }
    return all;
}

/// Runs a keyboard entry step through the session; false when `step` is not
/// one.
bool runEntryStep(Document& doc, const Step& step, core::EditSession& session) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();
    // A song without tracks or measures has no caret, and core says so.
    const auto requireCaret = [&song, &session] {
        (void)clampCaret(song, session.caret());
    };
    static constexpr std::array<std::pair<std::string_view, CaretMove>, 6> kMoves{{
        {"right", CaretMove::NextBeat},
        {"left", CaretMove::PreviousBeat},
        {"next-measure", CaretMove::NextMeasure},
        {"previous-measure", CaretMove::PreviousMeasure},
        {"up", CaretMove::StringUp},
        {"down", CaretMove::StringDown},
    }};
    for (const auto& [name, move] : kMoves) {
        if (verb == name) {
            requireCaret();
            session.move(song, move);
            return true;
        }
    }
    Entry edit;
    if (verb == "caret") { // caret <track> <measure> <beat> [string]
        const Caret wanted{.track = step.index(1),
                           .measure = step.index(2),
                           .beat = step.index(3),
                           .string = step.has(4) ? step.number<int>(4) : 1};
        if (clampCaret(song, wanted) != wanted) {
            throw std::out_of_range("the song has no such caret position");
        }
        session.place(song, wanted);
        return true;
    } else if (verb == "voice") { // voice <1..4>: where typing goes from now on
        const int voice = step.number<int>(1);
        if (voice < 1 || voice > static_cast<int>(core::Measure::kMaxVoices)) {
            throw StepSyntaxError(
                std::format("voice: 1 to {}, not {}", core::Measure::kMaxVoices, voice));
        }
        requireCaret();
        session.setVoice(song, static_cast<std::size_t>(voice - 1));
        return true;
    } else if (verb == "fret") {
        edit = enterFret(song, session.caret(), step.number<int>(1), session.input());
    } else if (verb == "pitch") { // pitch <C..B> [add]
        if (step.has(2) && step.word(2) != "add") {
            throw StepSyntaxError(std::format("pitch: expected add, got {}", step.word(2)));
        }
        edit = enterPitch(song, session.caret(), step.noteName(1), session.input(),
                          step.has(2) ? PitchMode::AddToChord : PitchMode::Replace);
    } else if (verb == "shift") { // shift <semitones>
        edit = shiftPitch(song, session.caret(), step.number<int>(1));
    } else if (verb == "duration") {
        const Duration duration = step.duration(1);
        requireCaret();
        (void)session.setInput(doc, duration); // a beat takes any duration typing offers
        return true;
    } else if (verb == "delete") {
        edit = deleteAtCaret(song, session.caret());
    } else if (verb == "delete-beat") {
        edit = deleteBeat(song, session.caret());
    } else if (verb == "insert-rest") {
        edit = insertRest(song, session.caret(), session.input());
    } else if (verb == "drum") { // drum [key]: the caret's lane, or that piece's
        if (step.has(1)) {
            const int key = step.number<int>(1);
            const auto pieces = drumPieces();
            const auto found = std::ranges::find(pieces, key, &DrumPiece::key);
            if (found == pieces.end()) {
                throw std::out_of_range(std::format("drum: no lane for key {}", key));
            }
            Caret lane = session.caret();
            lane.string = static_cast<int>(found - pieces.begin()) + 1;
            (void)session.enter(doc, Entry{.command = nullptr, .caret = lane});
        }
        edit = toggleDrum(song, session.caret(), session.input());
    } else if (verb == "effect") { // effect <name>: toggled on the caret's notes
        const std::string_view name = step.word(1);
        const std::optional<NoteEffect> effect = parseName<NoteEffect>(name);
        if (!effect) {
            throw StepSyntaxError(std::format("not an effect: {}", name));
        }
        edit = toggleEffect(song, session.caret(), *effect);
    } else if (verb == "bend") { // bend <form> [cents]: on the caret's notes, a step by default
        constexpr int kFullStep = 200;
        const std::string_view name = step.word(1);
        const auto found = std::ranges::find_if(
            kBendForms, [name](const auto& form) { return form.first == name; });
        if (found == kBendForms.end()) {
            throw StepSyntaxError(std::format("not a bend form: {}", name));
        }
        const int cents = step.has(2) ? step.number<int>(2) : kFullStep;
        edit = setBend(song, session.caret(), bendCurve(found->second, cents));
    } else if (verb == "strum" || verb == "pick" || verb == "tremolo" || verb == "whammy" ||
               verb == "bass" || verb == "hairpin" || verb == "fermata" || verb == "slur" ||
               verb == "ottava" || verb == "pedal" || verb == "breath") {
        // strum|pick <down|up|none>, tremolo <8|16|32|none>,
        // whammy <dive|dip|pre-dive|return|none> [cents], bass <tap|slap|pop|none>,
        // hairpin <crescendo|diminuendo|none>, fermata <short|normal|long|none>,
        // slur <on|none> (to the next beat of its voice),
        // ottava <8va|8vb|15ma|15mb|none> (ADR-0089), pedal <down|change|none>
        // (ADR-0092), breath <comma|caesura|none> (after it, ADR-0095): that
        // effect of the caret's beat, its others kept
        const Caret& at = session.caret();
        if (verb == "strum") {
            edit = setBeatEffect(song, at, &BeatEffects::strum,
                                 strokeDirection<Strum>(verb, step.word(1)));
        } else if (verb == "pick") {
            edit = setBeatEffect(song, at, &BeatEffects::pickStroke,
                                 strokeDirection<PickStroke>(verb, step.word(1)));
        } else if (verb == "tremolo") {
            edit =
                setBeatEffect(song, at, &BeatEffects::tremoloPicking, tremoloSpeed(step.word(1)));
        } else if (verb == "whammy") {
            constexpr int kStepDown = -200;
            const std::string_view name = step.word(1);
            const auto found = std::ranges::find_if(
                kWhammyForms, [name](const auto& form) { return form.first == name; });
            if (found == kWhammyForms.end()) {
                throw StepSyntaxError(std::format("not a whammy-bar form: {}", name));
            }
            edit = setBeatEffect(
                song, at, &BeatEffects::whammyBar,
                whammyCurve(found->second, step.has(2) ? step.number<int>(2) : kStepDown));
        } else if (verb == "bass") {
            edit = setBeatEffect(song, at, &BeatEffects::bassTechnique,
                                 kindOf<BassTechnique>(verb, step.word(1)));
        } else if (verb == "hairpin") {
            edit =
                setBeatEffect(song, at, &BeatEffects::hairpin, kindOf<Hairpin>(verb, step.word(1)));
        } else if (verb == "fermata") {
            edit =
                setBeatEffect(song, at, &BeatEffects::fermata, kindOf<Fermata>(verb, step.word(1)));
        } else if (verb == "ottava") {
            edit =
                setBeatEffect(song, at, &BeatEffects::ottava, kindOf<Ottava>(verb, step.word(1)));
        } else if (verb == "pedal") {
            edit = setBeatEffect(song, at, &BeatEffects::pedal, kindOf<Pedal>(verb, step.word(1)));
        } else if (verb == "breath") {
            edit =
                setBeatEffect(song, at, &BeatEffects::breath, kindOf<Breath>(verb, step.word(1)));
        } else {
            const std::string_view word = step.word(1);
            if (word != "none" && word != "on") {
                throw StepSyntaxError(std::format("{}: none, on, not {}", verb, word));
            }
            edit = setBeatEffect(song, at, &BeatEffects::slur, word == "on");
        }
    } else if (verb == "slide-in") { // slide-in <from-below|from-above|none>: the caret's notes
        edit = setSlideIn(song, session.caret(), kindOf<SlideIn>(verb, step.word(1)));
    } else if (verb == "slide-out") { // slide-out <shift|legato|downwards|upwards|none>
        edit = setSlideOut(song, session.caret(), kindOf<SlideOut>(verb, step.word(1)));
    } else if (verb == "vibrato") { // vibrato <slight|wide|none>
        edit = setVibrato(song, session.caret(), kindOf<Vibrato>(verb, step.word(1)));
    } else if (verb == "accent") { // accent <accent|marcato|none>
        edit = setAccent(song, session.caret(), kindOf<Accent>(verb, step.word(1)));
    } else if (verb == "ornament") { // ornament <mordent|upper-mordent|turn|inverted-turn|none>
        edit = setOrnament(song, session.caret(), kindOf<Ornament>(verb, step.word(1)));
    } else if (verb == "fingering") {
        // fingering <finger...> (ADR-0093): one per note from the lowest up, or
        // one for all; 1-5 from the thumb, or t and 1-4 on a fretted instrument.
        const bool fretted =
            song.track(session.caret().track).instrument().kind == InstrumentKind::Fretted;
        std::vector<Finger> fingers;
        for (std::size_t i = 1; i == 1 || step.has(i); ++i) {
            const std::string_view word = step.word(i);
            const std::optional<Finger> finger =
                word == "none" ? std::optional(Finger::None) : fingerOfText(word, fretted);
            if (!finger) {
                throw StepSyntaxError(std::format("not a finger: {}", word));
            }
            fingers.push_back(*finger);
        }
        edit = setFingering(song, session.caret(), fingers);
    } else if (verb == "pluck") { // pluck <p|i|m|a|c|none> (ADR-0093)
        const std::string_view word = step.word(1);
        const std::optional<Finger> finger =
            word == "none" ? std::optional(Finger::None) : pluckOfText(word);
        if (!finger) {
            throw StepSyntaxError(std::format("not a plucking finger: {}", word));
        }
        edit = setPluck(song, session.caret(), *finger);
    } else if (verb == "trill") { // trill <on|semitones|none> [16|32|64] (ADR-0090)
        const std::string_view what = step.word(1);
        const NoteValue speed = step.has(2) ? trillSpeed(step.word(2)) : NoteValue::ThirtySecond;
        if (what == "none") {
            edit = setTrill(song, session.caret(), Trill{});
        } else if (what == "on") { // to the next step of the key
            edit = setTrillToNextStep(song, session.caret(), speed);
        } else {
            const int semitones = step.number<int>(1);
            if (semitones == 0 || std::abs(semitones) > Trill::kMaxSemitones) {
                throw StepSyntaxError(
                    std::format("trill: on, none or 1 to {} semitones up or down, not {}",
                                Trill::kMaxSemitones, what));
            }
            edit =
                setTrill(song, session.caret(),
                         Trill{.semitones = static_cast<std::int8_t>(semitones), .speed = speed});
        }
    } else if (verb == "harmonic") { // harmonic <natural|artificial|pinch|tapped|semi|none>
        edit = setHarmonic(song, session.caret(), kindOf<HarmonicKind>(verb, step.word(1)));
    } else if (verb == "chord") { // chord <name> | chord none: the caret's beat (ADR-0080)
        const std::string name = step.rest(1);
        edit = setChordSymbol(song, session.caret(), name == "none" ? std::string() : name);
    } else if (verb == "clef") { // clef <name>: from the caret's measure on (ADR-0087)
        // Drums keep their clef.
        edit = setClef(song, session.caret(), kindOf<Clef>(verb, step.word(1), Clef::Percussion));
    } else if (verb == "simile") { // simile 1 | simile 2 | simile none: measure repeat (ADR-0102)
        edit = setMeasureRepeat(song, session.caret(), step.word(1) == "none" ? 0 : step.index(1));
    } else if (verb == "text") { // text <words> | text none: above the caret's beat (ADR-0081)
        const std::string text = step.rest(1);
        edit = setBeatText(song, session.caret(), text == "none" ? std::string() : text);
    } else if (verb == "lyrics") { // lyrics <verse> <measure> <text> | lyrics <verse> none
        const std::size_t verse = step.index(1);
        if (verse >= kMaxVerses) {
            throw StepSyntaxError(std::format("lyrics: verse {} past the {}th", verse, kMaxVerses));
        }
        const bool none = step.word(2) == "none" && !step.has(3);
        VerseEntry typed = setVerse(song, session.caret(), verse, none ? 0 : step.index(2),
                                    none ? std::string() : step.rawRest(3));
        if (typed.leftover > 0) {
            std::cerr << std::format(
                "warning: lyrics: {} syllable(s) after the last note dropped\n", typed.leftover);
        }
        edit = std::move(typed.entry);
    } else if (verb == "grace") { // grace <semitones> [duration] [on] | grace none
        if (step.word(1) == "none") {
            edit = removeGraceNotes(song, session.caret());
        } else {
            Duration duration(NoteValue::Eighth);
            bool onBeat = false;
            for (std::size_t i = 2; step.has(i); ++i) {
                if (step.word(i) == "on") {
                    onBeat = true;
                } else {
                    duration = step.duration(i);
                }
            }
            edit = addGraceNote(song, session.caret(), step.number<int>(1), duration, onBeat);
        }
    } else if (verb == "dynamic") { // dynamic <ppp..fff>: from the caret to the next change
        const std::string_view name = step.word(1);
        const std::optional<Dynamic> dynamic = parseName<Dynamic>(name);
        if (!dynamic) {
            throw StepSyntaxError(std::format("not a dynamic: {}", name));
        }
        edit = setDynamic(song, session.caret(), *dynamic);
    } else {
        return false;
    }
    const EntryResult entered = session.enter(doc, std::move(edit));
    if (entered.refused == Refusal::NothingAtCaret) {
        // As the app's keys: nothing happens, and the step goes on (ADR-0123).
        std::cerr << std::format("warning: {}: {}\n", verb, nameOf(*entered.refused));
    } else if (entered.refused) {
        throw std::runtime_error(std::format("refused: {}", nameOf(*entered.refused)));
    }
    return true;
}

// A step's number as an int, the largest int for any larger one, which core
// then rejects as out of range.
int countOf(std::size_t number) {
    return static_cast<int>(std::min<std::size_t>(number, std::numeric_limits<int>::max()));
}

// Runs `check`, a check core makes of what a step names: what it rejects makes
// the step malformed, core's message following the step's verb.
template <typename Check>
auto checked(std::string_view verb, Check check) {
    try {
        return check();
    } catch (const std::invalid_argument& error) {
        throw StepSyntaxError(std::format("{}: {}", verb, error.what()));
    }
}

// The marks of a measure header (ADR-0123): sign <m> <segno|coda|fine|to-coda|
// ...>... | none, jump <m> <da-capo|dal-segno|dal-segno-segno|none> [al-...],
// repeat <m> [start] [end <times>] | none, ending <m> <time>... | none and
// barline <m> double|single. Each gives the measure exactly what it names of
// its group of marks, keeping the rest of the group; core checks the group and
// names the change. Null when the measure has those marks already.
std::unique_ptr<core::Command> markStep(const Song& song, const Step& step) {
    using namespace core;
    const std::string_view verb = step.verb();
    const std::size_t measure = step.index(1);
    const MeasureHeader& header = song.measureHeader(measure); // a missing measure first
    if (verb == "sign") {
        NavigationMarks marks = navigationOf(header);
        marks.signs = {};
        if (!step.has(2)) {
            throw StepSyntaxError("sign: expected signs or none");
        }
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                const std::optional<NavigationSign> sign = parseName<NavigationSign>(step.word(i));
                if (!sign) {
                    throw StepSyntaxError(std::format("not a sign: {}", step.word(i)));
                }
                marks.set(*sign, true);
            }
        }
        return changeNavigation(song, measure, marks);
    }
    if (verb == "jump") {
        NavigationMarks marks = navigationOf(header);
        const std::optional<JumpTo> to = parseName<JumpTo>(step.word(2));
        if (!to) {
            throw StepSyntaxError(std::format("not a jump: {}", step.word(2)));
        }
        // The step names the whole jump: without al-... it plays to the end.
        marks.jump = *to;
        marks.jumpThen = JumpThen::End;
        if (step.has(3)) {
            const std::optional<JumpThen> then = parseName<JumpThen>(step.word(3));
            if (!then || marks.jump == JumpTo::None || step.has(4)) {
                throw StepSyntaxError(std::format("jump: not al-fine, al-coda or "
                                                  "al-double-coda: {}",
                                                  step.word(3)));
            }
            marks.jumpThen = *then;
        }
        return changeNavigation(song, measure, marks);
    }
    if (verb == "repeat") {
        RepeatMarks marks = repeatsOf(header);
        marks.start = false;
        marks.times = 0;
        if (!step.has(2)) {
            throw StepSyntaxError("repeat: expected start, end <times> or none");
        }
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                if (step.word(i) == "start") {
                    marks.start = true;
                } else if (step.word(i) == "end") {
                    const int times = countOf(step.index(++i));
                    checked(verb, [times] { checkRepeatTimes(times); });
                    marks.times = times;
                } else {
                    throw StepSyntaxError(std::format("not a repeat: {}", step.word(i)));
                }
            }
        }
        return changeRepeats(song, measure, marks);
    }
    if (verb == "ending") {
        RepeatMarks marks = repeatsOf(header);
        marks.endings = 0;
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                const int time = countOf(step.index(i));
                const std::uint8_t bit = checked(verb, [time] { return endingBit(time); });
                marks.endings = static_cast<std::uint8_t>(marks.endings | bit);
            }
        }
        return changeRepeats(song, measure, marks);
    }
    const std::string_view kind = step.word(2);
    if ((kind != "double" && kind != "single") || step.has(3)) {
        throw StepSyntaxError(std::format("barline: expected double or single, not {}", kind));
    }
    return changeBarline(song, measure, kind == "double");
}

// `clipboard` holds what the last copy step copied, from the song edited or
// one of `sources`.
void runParsedStep(Document& doc, const Step& step, core::MeasureClip& clipboard,
                   core::EditSession& session, std::span<const core::Song> sources) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();
    if (!knownVerb(verb)) {
        throw StepSyntaxError(std::format("unknown step: {}", verb));
    }

    if (runEntryStep(doc, step, session)) {
        return;
    }
    if (verb == "undo") {
        doc.undo();
    } else if (verb == "redo") {
        doc.redo();
    } else if (verb == "save") {
        doc.markSaved();
    } else if (verb == "add-track") {
        const auto* preset = findInstrumentPreset(step.word(1));
        if (preset == nullptr) {
            throw StepSyntaxError(std::format("unknown instrument preset: {}", step.word(1)));
        }
        const std::size_t at = step.has(2) ? step.index(2) : song.trackCount();
        doc.execute(std::make_unique<InsertTrack>(at, Track::fromPreset(*preset)));
    } else if (verb == "remove-track") {
        doc.execute(std::make_unique<RemoveTrack>(step.index(1)));
    } else if (verb == "insert-measures") {
        const std::size_t count = step.has(2) ? step.index(2) : 1;
        doc.execute(std::make_unique<InsertMeasures>(step.index(1), count));
    } else if (verb == "append-measures") {
        doc.execute(std::make_unique<InsertMeasures>(song.measureCount(), step.index(1)));
    } else if (verb == "remove-measures") {
        const std::size_t count = step.has(2) ? step.index(2) : 1;
        doc.execute(std::make_unique<RemoveMeasures>(step.index(1), count));
    } else if (verb == "time" || verb == "key") {
        // From the measure until the next change of that signature.
        const std::size_t measure = step.index(1);
        std::unique_ptr<Command> change;
        if (verb == "time") {
            const auto time = TimeSignature::parse(step.word(2));
            if (!time) {
                throw StepSyntaxError(std::format("not a time signature: {}", step.word(2)));
            }
            change = changeTimeSignature(song, measure, *time);
        } else {
            const auto key = KeySignature::parse(step.rest(2));
            if (!key) {
                throw StepSyntaxError(std::format("not a key: {}", step.rest(2)));
            }
            change = changeKeySignature(song, measure, *key);
        }
        if (change) {
            doc.execute(std::move(change));
        }
    } else if (verb == "feel") { // feel <m> <none|eighth|sixteenth|dotted-...|scottish-...>
        // From the measure until the next change of the feel (ADR-0104).
        const std::size_t measure = step.index(1);
        const std::optional<TripletFeel> feel = parseName<TripletFeel>(step.word(2));
        if (!feel || step.has(3)) {
            throw StepSyntaxError(std::format("not a triplet feel: {}", step.word(2)));
        }
        if (auto change = core::changeTripletFeel(song, measure, *feel)) {
            doc.execute(std::move(change));
        }
    } else if (verb == "rename-track") { // rename-track <track> [name]
        const std::size_t track = step.index(1);
        std::string name = step.has(2) ? step.rest(2) : std::string();
        if (name != song.track(track).name()) {
            doc.execute(std::make_unique<core::SetTrackName>(track, std::move(name)));
        }
    } else if (verb == "mix") { // mix <track> <volume> <pan> [mute] [solo]
        core::TrackMix mix{.volume = step.decimal(2), .pan = step.decimal(3)};
        for (std::size_t i = 4; step.has(i); ++i) {
            if (step.word(i) == "mute") {
                mix.mute = true;
            } else if (step.word(i) == "solo") {
                mix.solo = true;
            } else {
                throw StepSyntaxError(std::format("mix: not mute or solo: {}", step.word(i)));
            }
        }
        doc.execute(std::make_unique<core::SetTrackMix>(step.index(1), mix));
    } else if (verb == "tempo") {
        doc.execute(std::make_unique<SetStartTempo>(Tempo(step.number<int>(1))));
    } else if (verb == "tempo-from") { // tempo-from <m> <bpm> | none
        const std::size_t measure = step.index(1);
        if (measure == 0) {
            throw StepSyntaxError("tempo-from: the first measure plays at the song's tempo "
                                  "(\"tempo <bpm>\")");
        }
        std::optional<double> bpm;
        if (step.word(2) != "none") {
            const int value = step.number<int>(2);
            if (static_cast<double>(value) < Tempo::kMinBpm ||
                static_cast<double>(value) > Tempo::kMaxBpm) {
                throw StepSyntaxError(std::format("tempo-from: {} to {} beats per minute, not {}",
                                                  Tempo::kMinBpm, Tempo::kMaxBpm, value));
            }
            bpm = value;
        }
        if (auto change = core::changeTempo(song, measure, bpm)) {
            doc.execute(std::move(change));
        }
    } else if (verb == "tempo-gradual") { // tempo-gradual <m> <on|off> (ADR-0082)
        const std::size_t measure = step.index(1);
        if (measure == 0) {
            throw StepSyntaxError("tempo-gradual: the first measure plays at the song's tempo");
        }
        const std::string_view value = step.word(2);
        if (value != "on" && value != "off") {
            throw StepSyntaxError(std::format("tempo-gradual: on or off, not {}", value));
        }
        if (auto change = core::changeTempoGradual(song, measure, value == "on")) {
            doc.execute(std::move(change));
        }
    } else if (verb == "section") { // section <m> [name]: no name takes it off
        const std::size_t measure = step.index(1);
        if (measure >= song.measureCount()) {
            throw std::out_of_range(std::format("section: no measure {}", measure));
        }
        if (auto change = core::changeSection(song, measure, step.has(2) ? step.rest(2) : "")) {
            doc.execute(std::move(change));
        }
    } else if (verb == "break") { // break <m> <system|page|keep|auto> [track <t>] (ADR-0152)
        const std::size_t measure = step.index(1);
        const std::string_view name = step.word(2);
        const std::optional<core::Break> kind = parseName<core::Break>(name);
        if (!kind) {
            throw StepSyntaxError(std::format("break: {}, not {}", choiceOf<core::Break>(), name));
        }
        core::LayoutScope scope = core::LayoutScope::song();
        if (step.has(3)) {
            if (step.word(3) != "track") {
                throw StepSyntaxError(std::format("break: expected track: {}", step.word(3)));
            }
            scope = core::LayoutScope::ofTrack(step.index(4));
        }
        if (measure >= song.measureCount()) {
            throw std::out_of_range(std::format("break: no measure {}", measure));
        }
        const std::pair<std::size_t, core::Break> one{measure, *kind};
        if (auto change = core::changeBreaks(song, scope, std::span(&one, 1))) {
            doc.execute(std::move(change));
        }
    } else if (verb == "title") {
        SongInfo info = song.info();
        info.title = step.rest(1);
        doc.execute(std::make_unique<SetSongInfo>(std::move(info)));
    } else if (verb == "credit") { // credit <line> [text]: no text clears the line
        const std::string_view line = step.word(1);
        const std::optional<Credit> credit = parseName<Credit>(line);
        if (!credit) {
            throw StepSyntaxError(std::format("credit: {}, not {}", choiceOf<Credit>(), line));
        }
        if (auto change = core::changeCredit(song, *credit, step.has(2) ? step.rest(2) : "")) {
            doc.execute(std::move(change));
        }
    } else if (verb == "note") { // note <track> <measure> <string> <fret> <duration>
        const std::size_t track = step.index(1);
        const Note note =
            song.track(track).staff(0).frettedNote(step.number<int>(3), step.number<int>(4));
        const Duration duration = step.duration(5);
        doc.execute(editMeasure(song, track, step.index(2), [&](Measure& measure) {
            measure.voices[0].beats.push_back(Beat{.duration = duration, .notes = {note}});
        }));
    } else if (verb == "rest") { // rest <track> <measure> <duration>
        const Duration duration = step.duration(3);
        doc.execute(editMeasure(song, step.index(1), step.index(2), [&](Measure& measure) {
            measure.voices[0].beats.push_back(Beat{.duration = duration, .notes = {}});
        }));
    } else if (verb == "fill") { // fill <track> <measure>
        const std::size_t measure = step.index(2);
        const TimeSignature time = song.measureHeader(measure).timeSignature;
        doc.execute(editMeasure(song, step.index(1), measure, [&](Measure& content) {
            if (!fillWithRests(content.voices[0], time)) {
                throw std::runtime_error("the voice cannot be completed with rests");
            }
        }));
    } else if (verb == "copy") { // copy <m> <n> [track <t> [<count>]] [from <song>]
        std::size_t track = 0;
        std::size_t tracks = 0; // every track
        const Song* source = &song;
        for (std::size_t i = 3; step.has(i);) {
            if (step.word(i) == "track") {
                track = step.index(i + 1);
                tracks = 1;
                i += 2;
                if (step.has(i) && step.word(i) != "from") {
                    tracks = step.index(i++);
                }
            } else if (step.word(i) == "from") {
                const std::size_t number = step.index(i + 1);
                if (number == 0 || number > sources.size()) {
                    throw std::out_of_range(
                        std::format("copy: no song {} open beside it ({} with --from)", number,
                                    sources.size()));
                }
                source = &sources[number - 1];
                i += 2;
            } else {
                throw StepSyntaxError(
                    std::format("copy: expected track or from: {}", step.word(i)));
            }
        }
        clipboard = copyMeasures(*source, step.index(1), step.index(2), track, tracks);
    } else if (verb == "paste") { // paste <m> [track <t>] [insert]
        std::size_t track = 0;
        PasteMode mode = PasteMode::Replace;
        for (std::size_t i = 2; step.has(i); ++i) {
            if (step.word(i) == "track") {
                track = step.index(++i);
            } else if (step.word(i) == "insert") {
                mode = PasteMode::Insert;
            } else {
                throw StepSyntaxError(
                    std::format("paste: expected track or insert: {}", step.word(i)));
            }
        }
        doc.execute(makePaste(song, clipboard, step.index(1), track, mode));
    } else if (verb == "transpose") { // transpose <semitones> [measures <m> <n>] [tracks <t>...]
        Transposition what{.semitones = step.number<int>(1)};
        for (std::size_t i = 2; step.has(i);) {
            if (step.word(i) == "measures") {
                what.first = step.index(i + 1);
                what.count = step.index(i + 2);
                i += 3;
            } else if (step.word(i) == "tracks") {
                for (++i; step.has(i) && step.word(i) != "measures"; ++i) {
                    what.tracks.push_back(step.index(i));
                }
                if (what.tracks.empty()) {
                    throw StepSyntaxError("transpose: tracks needs at least one track");
                }
            } else {
                throw StepSyntaxError(
                    std::format("transpose: expected measures or tracks: {}", step.word(i)));
            }
        }
        doc.execute(makeTranspose(song, what));
    } else if (verb ==
               "tuning") { // tuning <track> [<preset>|<notes>] [capo <n>] [staff <s>] [frets]
        Retuning what{.track = step.index(1)};
        std::string notes;
        std::optional<int> capo;
        for (std::size_t i = 2; step.has(i); ++i) {
            const std::string_view word = step.word(i);
            if (word == "capo") {
                capo = step.number<int>(++i);
            } else if (word == "staff") {
                what.staff = step.index(++i);
            } else if (word == "frets") {
                what.keep = Retune::Frets;
            } else {
                notes += notes.empty() ? "" : " ";
                notes += word;
            }
        }
        const Staff& staff = song.track(what.track).staff(what.staff);
        if (!notes.empty()) {
            auto tuning = Tuning::preset(notes);
            if (!tuning) {
                tuning = Tuning::parse(notes);
            }
            if (!tuning) {
                throw StepSyntaxError(std::format("not a tuning: {}", notes));
            }
            what.tuning = *tuning;
        } else if (staff.tuning()) {
            what.tuning = *staff.tuning(); // only the capo changes
        } else {
            throw std::invalid_argument("the staff has no strings");
        }
        what.capo = capo.value_or(staff.capo());
        if (auto change = makeRetune(song, what)) {
            doc.execute(std::move(change));
        }
    } else if (verb == "sign" || verb == "jump" || verb == "repeat" || verb == "ending" ||
               verb == "barline") {
        if (auto change = markStep(song, step)) {
            doc.execute(std::move(change));
        }
    } else {
        throw StepSyntaxError(std::format("unknown step: {}", verb));
    }
}

} // namespace

std::span<const std::string_view> stepForms() noexcept {
    return kStepForms;
}

void runStep(core::Document& doc, std::string_view text, StepState& state) {
    runParsedStep(doc, Step(text), state.clipboard, state.session, state.sources);
    // Undo, removals and other edits can take the caret's beat away.
    state.session.songChanged(doc.song());
}

} // namespace elara::cli
