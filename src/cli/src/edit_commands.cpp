// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing subcommands: edit (a script of undoable edits applied to a new song or
// a .elara file).
#include <elara/core/clipboard.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
#include <elara/core/retune.hpp>
#include <elara/core/transpose.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using core::Document;
using core::Song;

/// A step that is not well formed (as opposed to one the song rejects).
class StepSyntaxError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

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

// The lines of the credits, in the order of core::Credit (ADR-0059).
constexpr std::array<std::string_view, 9> kCreditNames{
    "title", "subtitle", "artist", "album", "words", "music", "copyright", "transcriber", "notes"};

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

/// Keyboard entry across steps (ADR-0021): the caret and the input duration.
struct EntryState {
    core::Caret caret{};
    core::Duration input{core::NoteValue::Quarter};
};

// The names `effect <name>` takes (ADR-0037).
constexpr std::array<std::pair<std::string_view, core::NoteEffect>, 14> kEffectNames{{
    {"tie", core::NoteEffect::Tie},
    {"legato", core::NoteEffect::Legato},
    {"slide", core::NoteEffect::Slide},
    {"bend", core::NoteEffect::Bend},
    {"vibrato", core::NoteEffect::Vibrato},
    {"palm-mute", core::NoteEffect::PalmMute},
    {"dead-note", core::NoteEffect::DeadNote},
    {"let-ring", core::NoteEffect::LetRing},
    {"harmonic", core::NoteEffect::Harmonic},
    {"ghost", core::NoteEffect::Ghost},
    {"accent", core::NoteEffect::Accent},
    {"staccato", core::NoteEffect::Staccato},
    {"tenuto", core::NoteEffect::Tenuto},
    {"staccatissimo", core::NoteEffect::Staccatissimo}, // ADR-0094
}};

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
constexpr std::array<std::string_view, 4> kBassTechniques{"none", "tap", "slap", "pop"};
constexpr std::array<std::string_view, 3> kHairpins{"none", "crescendo", "diminuendo"};
constexpr std::array<std::string_view, 4> kFermatas{"none", "short", "normal", "long"};
constexpr std::array<std::string_view, 5> kOttavas{"none", "8va", "8vb", "15ma", "15mb"};
// The clefs `clef` takes, in the order of core::Clef; drums keep theirs (ADR-0087).
constexpr std::array<std::string_view, 6> kClefNames{"treble",   "treble-8vb", "bass",
                                                     "bass-8vb", "alto",       "tenor"};
constexpr std::array<std::string_view, 2> kSlurs{"none", "on"};
constexpr std::array<std::string_view, 3> kPedals{"none", "down", "change"};    // ADR-0092
constexpr std::array<std::string_view, 3> kBreaths{"none", "comma", "caesura"}; // ADR-0095

// Directions `strum` and `pick` take (ADR-0056).
constexpr std::array<std::string_view, 3> kStrokeDirections{"none", "down", "up"};

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

// The index of a stroke direction: 0 none, 1 down, 2 up.
std::uint8_t strokeDirection(std::string_view verb, std::string_view word) {
    const auto found = std::ranges::find(kStrokeDirections, word);
    if (found == kStrokeDirections.end()) {
        throw StepSyntaxError(std::format("{}: down, up or none, not {}", verb, word));
    }
    return static_cast<std::uint8_t>(found - kStrokeDirections.begin());
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

// The kinds of note effects their steps take, in the order of their enums
// (ADR-0058): slide-in, slide-out, vibrato, accent and harmonic.
constexpr std::array<std::string_view, 3> kSlideInKinds{"none", "from-below", "from-above"};
constexpr std::array<std::string_view, 5> kSlideOutKinds{"none", "shift", "legato", "downwards",
                                                         "upwards"};
constexpr std::array<std::string_view, 3> kVibratoKinds{"none", "slight", "wide"};
constexpr std::array<std::string_view, 3> kAccentKinds{"none", "accent", "marcato"};
constexpr std::array<std::string_view, 5> kOrnamentKinds{"none", "mordent", "upper-mordent", "turn",
                                                         "inverted-turn"}; // ADR-0091
constexpr std::array<std::string_view, 6> kHarmonicKinds{"none",  "natural", "artificial",
                                                         "pinch", "tapped",  "semi"};

// The index of `word` among the kinds `verb` takes.
template <std::size_t N>
std::uint8_t kindOf(std::string_view verb, std::string_view word,
                    const std::array<std::string_view, N>& kinds) {
    const auto found = std::ranges::find(kinds, word);
    if (found == kinds.end()) {
        std::string all;
        for (const std::string_view kind : kinds) {
            all += std::format("{}{}", all.empty() ? "" : ", ", kind);
        }
        throw StepSyntaxError(std::format("{}: {}, not {}", verb, all, word));
    }
    return static_cast<std::uint8_t>(found - kinds.begin());
}

constexpr std::array<std::pair<std::string_view, core::Dynamic>, 8> kDynamicNames{{
    {"ppp", core::Dynamic::PPP},
    {"pp", core::Dynamic::PP},
    {"p", core::Dynamic::P},
    {"mp", core::Dynamic::MP},
    {"mf", core::Dynamic::MF},
    {"f", core::Dynamic::F},
    {"ff", core::Dynamic::FF},
    {"fff", core::Dynamic::FFF},
}};

/// Runs a keyboard entry step; false when `step` is not one.
bool runEntryStep(Document& doc, const Step& step, EntryState& entry) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();
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
            entry.caret = moveCaret(song, entry.caret, move);
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
        entry.caret = wanted;
        return true;
    } else if (verb == "voice") { // voice <1..4>: where typing goes from now on
        const int voice = step.number<int>(1);
        if (voice < 1 || voice > static_cast<int>(core::Measure::kMaxVoices)) {
            throw StepSyntaxError(
                std::format("voice: 1 to {}, not {}", core::Measure::kMaxVoices, voice));
        }
        entry.caret.voice = static_cast<std::size_t>(voice - 1);
        entry.caret = clampCaret(song, entry.caret);
        return true;
    } else if (verb == "fret") {
        edit = enterFret(song, entry.caret, step.number<int>(1), entry.input);
    } else if (verb == "pitch") { // pitch <C..B> [add]
        if (step.has(2) && step.word(2) != "add") {
            throw StepSyntaxError(std::format("pitch: expected add, got {}", step.word(2)));
        }
        edit = enterPitch(song, entry.caret, step.noteName(1), entry.input,
                          step.has(2) ? PitchMode::AddToChord : PitchMode::Replace);
    } else if (verb == "shift") { // shift <semitones>
        edit = shiftPitch(song, entry.caret, step.number<int>(1));
    } else if (verb == "duration") {
        entry.input = step.duration(1);
        edit = setBeatDuration(song, entry.caret, entry.input);
    } else if (verb == "delete") {
        edit = deleteAtCaret(song, entry.caret);
    } else if (verb == "delete-beat") {
        edit = deleteBeat(song, entry.caret);
    } else if (verb == "insert-rest") {
        edit = insertRest(song, entry.caret, entry.input);
    } else if (verb == "drum") { // drum [key]: the caret's lane, or that piece's
        if (step.has(1)) {
            const int key = step.number<int>(1);
            const auto pieces = drumPieces();
            const auto found = std::ranges::find(pieces, key, &DrumPiece::key);
            if (found == pieces.end()) {
                throw std::out_of_range(std::format("drum: no lane for key {}", key));
            }
            entry.caret.string = static_cast<int>(found - pieces.begin()) + 1;
        }
        edit = toggleDrum(song, entry.caret, entry.input);
    } else if (verb == "effect") { // effect <name>: toggled on the caret's notes
        const std::string_view name = step.word(1);
        const auto found = std::ranges::find_if(
            kEffectNames, [name](const auto& effect) { return effect.first == name; });
        if (found == kEffectNames.end()) {
            throw StepSyntaxError(std::format("not an effect: {}", name));
        }
        edit = toggleEffect(song, entry.caret, found->second);
    } else if (verb == "bend") { // bend <form> [cents]: on the caret's notes, a step by default
        constexpr int kFullStep = 200;
        const std::string_view name = step.word(1);
        const auto found = std::ranges::find_if(
            kBendForms, [name](const auto& form) { return form.first == name; });
        if (found == kBendForms.end()) {
            throw StepSyntaxError(std::format("not a bend form: {}", name));
        }
        const int cents = step.has(2) ? step.number<int>(2) : kFullStep;
        edit = setBend(song, entry.caret, bendCurve(found->second, cents));
    } else if (verb == "strum" || verb == "pick" || verb == "tremolo" || verb == "whammy" ||
               verb == "bass" || verb == "hairpin" || verb == "fermata" || verb == "slur" ||
               verb == "ottava" || verb == "pedal" || verb == "breath") {
        // strum|pick <down|up|none>, tremolo <8|16|32|none>,
        // whammy <dive|dip|pre-dive|return|none> [cents], bass <tap|slap|pop|none>,
        // hairpin <crescendo|diminuendo|none>, fermata <short|normal|long|none>,
        // slur <on|none> (to the next beat of its voice),
        // ottava <8va|8vb|15ma|15mb|none> (ADR-0089), pedal <down|change|none>
        // (ADR-0092), breath <comma|caesura|none> (after it, ADR-0095): the
        // caret's beat
        BeatEffects effects = beatEffectsAt(song, entry.caret);
        if (verb == "strum") {
            effects.strum = static_cast<Strum>(strokeDirection(verb, step.word(1)));
        } else if (verb == "pick") {
            effects.pickStroke = static_cast<PickStroke>(strokeDirection(verb, step.word(1)));
        } else if (verb == "tremolo") {
            effects.tremoloPicking = tremoloSpeed(step.word(1));
        } else if (verb == "whammy") {
            constexpr int kStepDown = -200;
            const std::string_view name = step.word(1);
            const auto found = std::ranges::find_if(
                kWhammyForms, [name](const auto& form) { return form.first == name; });
            if (found == kWhammyForms.end()) {
                throw StepSyntaxError(std::format("not a whammy-bar form: {}", name));
            }
            effects.whammyBar =
                whammyCurve(found->second, step.has(2) ? step.number<int>(2) : kStepDown);
        } else if (verb == "bass") {
            effects.bassTechnique =
                static_cast<BassTechnique>(kindOf(verb, step.word(1), kBassTechniques));
        } else if (verb == "hairpin") {
            effects.hairpin = static_cast<Hairpin>(kindOf(verb, step.word(1), kHairpins));
        } else if (verb == "fermata") {
            effects.fermata = static_cast<Fermata>(kindOf(verb, step.word(1), kFermatas));
        } else if (verb == "ottava") {
            effects.ottava = static_cast<Ottava>(kindOf(verb, step.word(1), kOttavas));
        } else if (verb == "pedal") {
            effects.pedal = static_cast<Pedal>(kindOf(verb, step.word(1), kPedals));
        } else if (verb == "breath") {
            effects.breath = static_cast<Breath>(kindOf(verb, step.word(1), kBreaths));
        } else {
            effects.slur = kindOf(verb, step.word(1), kSlurs) != 0;
        }
        edit = setBeatEffects(song, entry.caret, effects);
    } else if (verb == "slide-in" || verb == "slide-out") {
        // slide-in <from-below|from-above|none>,
        // slide-out <shift|legato|downwards|upwards|none>: the caret's notes
        const NoteEffects now = noteEffectsAt(song, entry.caret);
        SlideIn in = now.slideIn;
        SlideOut out = now.slideOut;
        if (verb == "slide-in") {
            in = static_cast<SlideIn>(kindOf(verb, step.word(1), kSlideInKinds));
        } else {
            out = static_cast<SlideOut>(kindOf(verb, step.word(1), kSlideOutKinds));
        }
        edit = setSlides(song, entry.caret, in, out);
    } else if (verb == "vibrato") { // vibrato <slight|wide|none>
        edit = setVibrato(song, entry.caret,
                          static_cast<Vibrato>(kindOf(verb, step.word(1), kVibratoKinds)));
    } else if (verb == "accent") { // accent <accent|marcato|none>
        edit = setAccent(song, entry.caret,
                         static_cast<Accent>(kindOf(verb, step.word(1), kAccentKinds)));
    } else if (verb == "ornament") { // ornament <mordent|upper-mordent|turn|inverted-turn|none>
        edit = setOrnament(song, entry.caret,
                           static_cast<Ornament>(kindOf(verb, step.word(1), kOrnamentKinds)));
    } else if (verb == "fingering") {
        // fingering <finger...> (ADR-0093): one per note from the lowest up, or
        // one for all; 1-5 from the thumb, or t and 1-4 on a fretted instrument.
        const bool fretted =
            song.track(entry.caret.track).instrument().kind == InstrumentKind::Fretted;
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
        edit = setFingering(song, entry.caret, fingers);
    } else if (verb == "pluck") { // pluck <p|i|m|a|c|none> (ADR-0093)
        const std::string_view word = step.word(1);
        const std::optional<Finger> finger =
            word == "none" ? std::optional(Finger::None) : pluckOfText(word);
        if (!finger) {
            throw StepSyntaxError(std::format("not a plucking finger: {}", word));
        }
        edit = setPluck(song, entry.caret, *finger);
    } else if (verb == "trill") { // trill <on|semitones|none> [16|32|64] (ADR-0090)
        const std::string_view what = step.word(1);
        const NoteValue speed = step.has(2) ? trillSpeed(step.word(2)) : NoteValue::ThirtySecond;
        if (what == "none") {
            edit = setTrill(song, entry.caret, Trill{});
        } else if (what == "on") { // to the next step of the key
            edit = setTrillToNextStep(song, entry.caret, speed);
        } else {
            const int semitones = step.number<int>(1);
            if (semitones == 0 || std::abs(semitones) > Trill::kMaxSemitones) {
                throw StepSyntaxError(
                    std::format("trill: on, none or 1 to {} semitones up or down, not {}",
                                Trill::kMaxSemitones, what));
            }
            edit =
                setTrill(song, entry.caret,
                         Trill{.semitones = static_cast<std::int8_t>(semitones), .speed = speed});
        }
    } else if (verb == "harmonic") { // harmonic <natural|artificial|pinch|tapped|semi|none>
        edit = setHarmonic(song, entry.caret,
                           static_cast<HarmonicKind>(kindOf(verb, step.word(1), kHarmonicKinds)));
    } else if (verb == "chord") { // chord <name> | chord none: the caret's beat (ADR-0080)
        const std::string name = step.rest(1);
        edit = setChordSymbol(song, entry.caret, name == "none" ? std::string() : name);
    } else if (verb == "clef") { // clef <name>: from the caret's measure on (ADR-0087)
        edit = setClef(song, entry.caret,
                       static_cast<core::Clef>(kindOf(verb, step.word(1), kClefNames)));
    } else if (verb == "text") { // text <words> | text none: above the caret's beat (ADR-0081)
        const std::string text = step.rest(1);
        edit = setBeatText(song, entry.caret, text == "none" ? std::string() : text);
    } else if (verb == "lyrics") { // lyrics <verse> <measure> <text> | lyrics <verse> none
        const std::size_t verse = step.index(1);
        if (verse >= kMaxVerses) {
            throw StepSyntaxError(std::format("lyrics: verse {} past the {}th", verse, kMaxVerses));
        }
        const bool none = step.word(2) == "none" && !step.has(3);
        VerseEntry typed = setVerse(song, entry.caret, verse, none ? 0 : step.index(2),
                                    none ? std::string() : step.rawRest(3));
        if (typed.leftover > 0) {
            std::cerr << std::format(
                "warning: lyrics: {} syllable(s) after the last note dropped\n", typed.leftover);
        }
        edit = std::move(typed.entry);
    } else if (verb == "grace") { // grace <semitones> [duration] [on] | grace none
        if (step.word(1) == "none") {
            edit = removeGraceNotes(song, entry.caret);
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
            edit = addGraceNote(song, entry.caret, step.number<int>(1), duration, onBeat);
        }
    } else if (verb == "dynamic") { // dynamic <ppp..fff>: from the caret to the next change
        const std::string_view name = step.word(1);
        const auto found = std::ranges::find_if(
            kDynamicNames, [name](const auto& dynamic) { return dynamic.first == name; });
        if (found == kDynamicNames.end()) {
            throw StepSyntaxError(std::format("not a dynamic: {}", name));
        }
        edit = setDynamic(song, entry.caret, found->second);
    } else {
        return false;
    }
    if (edit.command) {
        doc.execute(std::move(edit.command));
    }
    entry.caret = edit.caret;
    return true;
}

// `clipboard` holds what the last copy step copied.
void runStep(Document& doc, const Step& step, core::MeasureClip& clipboard, EntryState& entry) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();

    if (runEntryStep(doc, step, entry)) {
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
        const auto found = std::ranges::find(kTripletFeelNames, step.word(2));
        if (found == kTripletFeelNames.end() || step.has(3)) {
            throw StepSyntaxError(std::format("not a triplet feel: {}", step.word(2)));
        }
        const auto feel = static_cast<core::TripletFeel>(found - kTripletFeelNames.begin());
        if (auto change = core::changeTripletFeel(song, measure, feel)) {
            doc.execute(std::move(change));
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
    } else if (verb == "title") {
        SongInfo info = song.info();
        info.title = step.rest(1);
        doc.execute(std::make_unique<SetSongInfo>(std::move(info)));
    } else if (verb == "credit") { // credit <line> [text]: no text clears the line
        const std::string_view line = step.word(1);
        const auto found = std::ranges::find(kCreditNames, line);
        if (found == kCreditNames.end()) {
            throw StepSyntaxError(std::format(
                "credit: title, subtitle, artist, album, words, music, copyright, transcriber "
                "or notes, not {}",
                line));
        }
        const auto credit = static_cast<core::Credit>(found - kCreditNames.begin());
        if (auto change = core::changeCredit(song, credit, step.has(2) ? step.rest(2) : "")) {
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
    } else if (verb == "copy") { // copy <m> <n> [track <t> [<count>]]
        std::size_t track = 0;
        std::size_t tracks = 0; // every track
        if (step.has(3)) {
            if (step.word(3) != "track") {
                throw StepSyntaxError(std::format("copy: expected track: {}", step.word(3)));
            }
            track = step.index(4);
            tracks = step.has(5) ? step.index(5) : 1;
        }
        clipboard = copyMeasures(song, step.index(1), step.index(2), track, tracks);
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
    } else if (verb == "sign") { // sign <m> <segno|coda|fine|to-coda|...>... | none
        const std::size_t measure = step.index(1);
        MeasureHeader header = song.measureHeader(measure);
        for (const SignName& sign : kSignNames) {
            header.*sign.flag = false;
        }
        if (!step.has(2)) {
            throw StepSyntaxError("sign: expected signs or none");
        }
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                const auto found = std::ranges::find(kSignNames, step.word(i), &SignName::name);
                if (found == kSignNames.end()) {
                    throw StepSyntaxError(std::format("not a sign: {}", step.word(i)));
                }
                header.*found->flag = true;
            }
        }
        if (header != song.measureHeader(measure)) {
            doc.execute(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
        }
    } else if (verb == "repeat") { // repeat <m> [start] [end <times>] | none
        const std::size_t measure = step.index(1);
        MeasureHeader header = song.measureHeader(measure);
        header.repeatStart = false;
        header.repeatTimes = 0;
        if (!step.has(2)) {
            throw StepSyntaxError("repeat: expected start, end <times> or none");
        }
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                if (step.word(i) == "start") {
                    header.repeatStart = true;
                } else if (step.word(i) == "end") {
                    const std::size_t times = step.index(++i);
                    if (times < 2 || times > std::numeric_limits<std::uint8_t>::max()) {
                        throw StepSyntaxError(
                            std::format("repeat: a section plays 2 to 255 times, not {}", times));
                    }
                    header.repeatTimes = static_cast<std::uint8_t>(times);
                } else {
                    throw StepSyntaxError(std::format("not a repeat: {}", step.word(i)));
                }
            }
        }
        if (header != song.measureHeader(measure)) {
            doc.execute(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
        }
    } else if (verb == "ending") { // ending <m> <time>... | none
        const std::size_t measure = step.index(1);
        MeasureHeader header = song.measureHeader(measure);
        header.alternateEndings = 0;
        if (step.word(2) != "none" || step.has(3)) {
            for (std::size_t i = 2; step.has(i); ++i) {
                const std::size_t time = step.index(i);
                if (time < 1 || time > MeasureHeader::kMaxEnding) {
                    throw StepSyntaxError(std::format("ending: times through are 1 to {}, not {}",
                                                      MeasureHeader::kMaxEnding, time));
                }
                header.alternateEndings =
                    static_cast<std::uint8_t>(header.alternateEndings | (1U << (time - 1)));
            }
        }
        if (header != song.measureHeader(measure)) {
            doc.execute(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
        }
    } else if (verb == "jump") { // jump <m> <da-capo|dal-segno|dal-segno-segno|none> [al-...]
        const std::size_t measure = step.index(1);
        MeasureHeader header = song.measureHeader(measure);
        const auto to = std::ranges::find(kJumpToNames, step.word(2));
        if (to == kJumpToNames.end()) {
            throw StepSyntaxError(std::format("not a jump: {}", step.word(2)));
        }
        header.jump = static_cast<JumpTo>(to - kJumpToNames.begin());
        header.jumpThen = JumpThen::End;
        if (step.has(3)) {
            const auto then = std::ranges::find(kJumpThenNames, step.word(3));
            if (then == kJumpThenNames.end() || header.jump == JumpTo::None || step.has(4)) {
                throw StepSyntaxError(std::format("jump: not al-fine, al-coda or "
                                                  "al-double-coda: {}",
                                                  step.word(3)));
            }
            header.jumpThen = static_cast<JumpThen>(then - kJumpThenNames.begin());
        }
        if (header != song.measureHeader(measure)) {
            doc.execute(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
        }
    } else {
        throw StepSyntaxError(std::format("unknown step: {}", verb));
    }
}

std::string jsonName(std::string_view name) {
    return name.empty() ? std::string("null") : std::format("\"{}\"", jsonEscape(name));
}

bool hasCaret(const Song& song) {
    return song.trackCount() > 0 && song.measureCount() > 0;
}

std::string caretJson(const Song& song, const core::Caret& caret) {
    if (!hasCaret(song)) {
        return "null";
    }
    return std::format(
        R"({{"track":{},"staff":{},"measure":{},"voice":{},"beat":{},"string":{},"freeSlot":{}}})",
        caret.track, caret.staff, caret.measure, caret.voice, caret.beat, caret.string,
        core::onFreeSlot(song, caret));
}

void printJson(const Document& doc, const EntryState& entry) {
    const Song& song = doc.song();
    std::cout
        << std::format(
               R"({{"title":"{}","tempo":{},"tracks":{},"measureCount":{},"measures":{},"issues":{},"caret":{},"input":"{}","undo":{{"count":{},"next":{}}},"redo":{{"count":{},"next":{}}},"modified":{}}})",
               jsonEscape(song.info().title), song.startTempo().bpm(), tracksJson(song),
               song.measureCount(), measuresJson(song), issuesJson(core::validate(song)),
               caretJson(song, entry.caret), entry.input.toString(), doc.undoCount(),
               jsonName(doc.undoName()), doc.redoCount(), jsonName(doc.redoName()),
               doc.isModified())
        << '\n';
}

void printText(const Document& doc, const EntryState& entry) {
    printSongText(doc.song());
    if (const core::Caret& caret = entry.caret; hasCaret(doc.song())) {
        std::cout << std::format(
            "caret: track {}, staff {}, measure {}, voice {}, beat {}{}, string {}; input {}\n",
            caret.track, caret.staff, caret.measure, caret.voice, caret.beat,
            core::onFreeSlot(doc.song(), caret) ? " (free slot)" : "", caret.string,
            entry.input.toString());
    }
    std::cout << std::format("undo: {} ({} step(s)), redo: {} ({} step(s)){}\n",
                             doc.canUndo() ? doc.undoName() : "-", doc.undoCount(),
                             doc.canRedo() ? doc.redoName() : "-", doc.redoCount(),
                             doc.isModified() ? ", modified" : "");
}

struct EditOptions {
    std::string in;  // start from this file instead of a new song
    std::string out; // save the result here
    std::vector<std::string> steps;
};

int runEdit(bool json, const EditOptions& options) {
    Document doc;
    if (!options.in.empty()) {
        auto loaded = io::loadNative(options.in);
        if (!loaded) {
            return fail(std::format("{}: {}", options.in, loaded.error().message));
        }
        doc = Document(std::move(*loaded));
    }
    const auto& steps = options.steps;
    core::MeasureClip clipboard;
    EntryState entry;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        try {
            runStep(doc, Step(steps[i]), clipboard, entry);
            // Undo, removals and other edits can take the caret's beat away.
            if (hasCaret(doc.song())) {
                entry.caret = core::clampCaret(doc.song(), entry.caret);
            }
        } catch (const StepSyntaxError& error) {
            return usageError(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        } catch (const std::exception& error) {
            return fail(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        }
    }
    if (!options.out.empty()) {
        try {
            io::saveNative(doc.song(), options.out);
        } catch (const std::exception& error) {
            return fail(error.what());
        }
        doc.markSaved();
    }
    if (json) {
        printJson(doc, entry);
    } else {
        printText(doc, entry);
    }
    return kExitOk;
}

} // namespace

void addEditCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<EditOptions>();
    auto* editCommand = app.add_subcommand(
        "edit", "Apply undoable edits to a song and print the result and its history");
    editCommand->add_option("--in", options->in, "Start from this .elara file (default: new song)");
    editCommand->add_option("--out", options->out, "Save the result to this .elara file");
    editCommand->add_option(
        "steps", options->steps,
        "Steps in order, each quoted: \"add-track <preset> [at]\", "
        "\"remove-track <i>\", \"insert-measures <at> [n]\", \"append-measures <n>\", "
        "\"remove-measures <at> [n]\", \"time <m> <N/D>\", \"key <m> <key>\", "
        "\"mix <track> <volume 0..1> <pan -1..1> [mute] [solo]\", "
        "\"tempo <bpm>\", \"tempo-from <m> <bpm|none>\", \"tempo-gradual <m> <on|off>\", "
        "\"section <m> [name]\", "
        "\"title <text>\", "
        "\"credit <title|subtitle|artist|album|words|music|copyright|transcriber|notes> "
        "[text]\", "
        "\"note <track> <m> <string> <fret> "
        "<duration>\", \"rest <track> <m> <duration>\", \"fill <track> <m>\", "
        "\"copy <m> <n> [track <t> [<count>]]\", \"paste <m> [track <t>] [insert]\", "
        "\"transpose <semitones> [measures <m> <n>] [tracks <t>...]\", "
        "\"caret <track> <m> <beat> [string]\", \"voice <1-4>\", right, left, up, down, "
        "next-measure, "
        "previous-measure, \"fret <n>\", \"pitch <C..B> [add]\", \"shift <semitones>\", "
        "\"duration <d>\", delete, delete-beat, "
        "insert-rest, \"drum [key]\", \"effect <name>\", \"bend <form> [cents]\", \"strum "
        "<down|up|none>\", "
        "\"pick <down|up|none>\", \"tremolo <8|16|32|none>\", "
        "\"whammy <dive|dip|pre-dive|return|none> [cents]\", \"bass <tap|slap|pop|none>\", "
        "\"hairpin <crescendo|diminuendo|none>\", \"fermata <short|normal|long|none>\", "
        "\"slur <on|none>\", \"ottava <8va|8vb|15ma|15mb|none>\", \"pedal <down|change|none>\", "
        "\"breath <comma|caesura|none>\", "
        "\"chord <name|none>\", \"text <words|none>\", "
        "\"slide-in "
        "<from-below|from-above|none>\", \"slide-out <shift|legato|downwards|upwards|none>\", "
        "\"vibrato <slight|wide|none>\", \"accent <accent|marcato|none>\", "
        "\"trill <on|semitones|none> [16|32|64]\", "
        "\"ornament <mordent|upper-mordent|turn|inverted-turn|none>\", "
        "\"fingering <1-5|t|none>...\", \"pluck <p|i|m|a|c|none>\", \"harmonic "
        "<natural|artificial|pinch|tapped|semi|none>\", undo, redo, "
        "save");
    editCommand->callback(
        [&context, options] { context.exitCode = runEdit(context.json, *options); });
}

} // namespace elara::cli
