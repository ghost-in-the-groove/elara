// SPDX-License-Identifier: AGPL-3.0-or-later
// Music-theory subcommands: pitch, fret, positions, tunings, key, spell, meter, rests.
#include <elara/core/duration.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/tempo.hpp>
#include <elara/core/tuning.hpp>
#include <elara/core/validation.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <array>
#include <charconv>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using core::KeySignature;
using core::Pitch;
using core::SpellingPreference;
using core::Tuning;

constexpr std::string_view kStepNames = "CDEFGAB";

std::optional<int> parseMidi(std::string_view text) {
    int value = 0;
    const auto* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end || value < 0 || value > 127) {
        return std::nullopt;
    }
    return value;
}

SpellingPreference preferenceFor(bool flats) {
    return flats ? SpellingPreference::Flats : SpellingPreference::Sharps;
}

void writeStrings(JsonWriter& json, const std::vector<std::string>& items) {
    json.beginArray();
    for (const auto& item : items) {
        json.value(item);
    }
    json.endArray();
}

// ---- pitch ------------------------------------------------------------------

struct PitchOptions {
    std::string input; // pitch text ("F#3") or MIDI number ("61")
    bool flats = false;
    double a4Hz = 440.0;
};

int runPitch(bool json, const PitchOptions& options) {
    std::optional<Pitch> pitch;
    if (!options.input.empty() && options.input.front() >= '0' && options.input.front() <= '9') {
        const auto midi = parseMidi(options.input);
        if (!midi) {
            return usageError(std::format("MIDI number out of range 0..127: {}", options.input));
        }
        pitch = Pitch::fromMidi(*midi, preferenceFor(options.flats));
    } else {
        pitch = Pitch::parse(options.input);
        if (!pitch) {
            return usageError(
                std::format("not a pitch (expected e.g. C4, F#3, Bb2): {}", options.input));
        }
    }
    const std::string text = pitch->toString();
    const double frequency = pitch->frequency(options.a4Hz);
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("pitch", text)
            .field("step", std::string_view(text).substr(0, 1))
            .field("alter", pitch->alter())
            .field("octave", pitch->octave())
            .field("midi", pitch->midiNumber())
            .field("frequency", frequency)
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("{}  MIDI {}  {:.4f} Hz\n", text, pitch->midiNumber(), frequency);
    }
    return kExitOk;
}

// ---- fret / positions / tunings ---------------------------------------------

struct FretOptions {
    std::string tuning = "guitar";
    int string = 1;
    int fret = 0;
    int capo = 0;
    bool flats = false;
};

int runFret(bool json, const FretOptions& options) {
    const auto tuning = tuningFromOption(options.tuning);
    if (!tuning) {
        return usageError(std::format("unknown tuning: {}", options.tuning));
    }
    if (options.string < 1 || options.string > tuning->stringCount()) {
        return usageError(std::format("string {} does not exist on a {}-string tuning",
                                      options.string, tuning->stringCount()));
    }
    if (options.fret < 0 || options.capo < 0 || options.capo + options.fret > Tuning::kMaxFret) {
        return usageError(std::format("capo + fret must stay within 0..{}", Tuning::kMaxFret));
    }
    const int midi = tuning->midiAt(options.string, options.fret, options.capo);
    const auto pitch = Pitch::fromMidi(midi, preferenceFor(options.flats));
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("tuning", tuning->toString())
            .field("string", options.string)
            .field("fret", options.fret)
            .field("capo", options.capo)
            .field("midi", midi)
            .field("pitch", pitch.toString())
            .field("frequency", pitch.frequency())
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("string {} fret {} (capo {}): {}  MIDI {}\n", options.string,
                                 options.fret, options.capo, pitch.toString(), midi);
    }
    return kExitOk;
}

struct PositionsOptions {
    std::string pitch;
    std::string tuning = "guitar";
    int capo = 0;
    int maxFret = Tuning::kMaxFret;
};

int runPositions(bool json, const PositionsOptions& options) {
    const auto tuning = tuningFromOption(options.tuning);
    if (!tuning) {
        return usageError(std::format("unknown tuning: {}", options.tuning));
    }
    const auto pitch = Pitch::parse(options.pitch);
    if (!pitch) {
        return usageError(
            std::format("not a pitch (expected e.g. C4, F#3, Bb2): {}", options.pitch));
    }
    const auto positions = tuning->positionsFor(*pitch, options.capo, options.maxFret);
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("pitch", pitch->toString())
            .field("midi", pitch->midiNumber())
            .field("tuning", tuning->toString())
            .field("capo", options.capo)
            .key("positions")
            .beginArray();
        for (const auto& position : positions) {
            out.beginObject()
                .field("string", position.string)
                .field("fret", position.fret)
                .endObject();
        }
        out.endArray().endObject();
        std::cout << out.text() << '\n';
    } else {
        for (const auto& position : positions) {
            std::cout << std::format("string {} fret {}\n", position.string, position.fret);
        }
    }
    return kExitOk;
}

int runTunings(bool json) {
    JsonWriter out;
    out.beginObject().key("tunings").beginArray();
    for (const auto name : Tuning::presetNames()) {
        const auto strings = Tuning::preset(name)->toString();
        if (json) {
            out.beginObject().field("name", name).field("strings", strings).endObject();
        } else {
            std::cout << std::format("{:<24} {}\n", name, strings);
        }
    }
    if (json) {
        out.endArray().endObject();
        std::cout << out.text() << '\n';
    }
    return kExitOk;
}

// ---- key / spell -------------------------------------------------------------

std::string stepWithAlter(core::Step step, int alter) {
    std::string text(1, kStepNames[static_cast<std::size_t>(step)]);
    text += alter > 0 ? "#" : alter < 0 ? "b" : "";
    return text;
}

int runKey(bool json, const std::string& name) {
    const auto key = KeySignature::parse(name);
    if (!key) {
        return usageError(
            std::format("not a key (expected e.g. \"D major\", \"Bb minor\"): {}", name));
    }
    std::vector<std::string> accidentals;
    for (const auto step : key->alteredSteps()) {
        accidentals.push_back(stepWithAlter(step, key->alterFor(step)));
    }
    const auto tonic = stepWithAlter(key->tonic(), key->tonicAlter());
    const std::string_view mode = key->mode() == core::KeyMode::Major ? "major" : "minor";
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("key", key->toString())
            .field("fifths", key->fifths())
            .field("mode", mode)
            .field("tonic", tonic)
            .key("accidentals");
        writeStrings(out, accidentals);
        out.endObject();
        std::cout << out.text() << '\n';
    } else {
        std::string list;
        for (const auto& accidental : accidentals) {
            list += (list.empty() ? "" : " ") + accidental;
        }
        std::cout << std::format("{}: {}\n", key->toString(),
                                 list.empty() ? "no accidentals" : list);
    }
    return kExitOk;
}

struct SpellOptions {
    std::string midi;
    std::string key = "C major";
};

int runSpell(bool json, const SpellOptions& options) {
    const auto midi = parseMidi(options.midi);
    if (!midi) {
        return usageError(std::format("MIDI number out of range 0..127: {}", options.midi));
    }
    const auto key = KeySignature::parse(options.key);
    if (!key) {
        return usageError(std::format("not a key: {}", options.key));
    }
    const auto pitch = key->spell(*midi);
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("midi", *midi)
            .field("key", key->toString())
            .field("pitch", pitch.toString())
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << pitch.toString() << '\n';
    }
    return kExitOk;
}

// ---- meter ----------------------------------------------------------------------

struct MeterOptions {
    std::string time;
    double bpm = 120.0;
    std::string beatUnit = "quarter";
};

// "quarter", "dotted-quarter", "eighth", ...
std::optional<core::Duration> beatUnitFromName(std::string_view name) {
    using core::NoteValue;
    constexpr std::array<std::pair<std::string_view, NoteValue>, 5> kValues{{
        {"whole", NoteValue::Whole},
        {"half", NoteValue::Half},
        {"quarter", NoteValue::Quarter},
        {"eighth", NoteValue::Eighth},
        {"sixteenth", NoteValue::Sixteenth},
    }};
    constexpr std::string_view kDotted = "dotted-";
    const bool dotted = name.starts_with(kDotted);
    if (dotted) {
        name.remove_prefix(kDotted.size());
    }
    for (const auto& [valueName, value] : kValues) {
        if (name == valueName) {
            return core::Duration(value, dotted ? 1 : 0);
        }
    }
    return std::nullopt;
}

int runMeter(bool json, const MeterOptions& options) {
    const auto time = core::TimeSignature::parse(options.time);
    if (!time) {
        return usageError(
            std::format("not a time signature (expected e.g. 4/4, 6/8): {}", options.time));
    }
    const auto beatUnit = beatUnitFromName(options.beatUnit);
    if (!beatUnit) {
        return usageError(std::format("unknown beat unit: {}", options.beatUnit));
    }
    const core::Tempo tempo(options.bpm, *beatUnit);
    const double seconds = tempo.secondsFor(time->measureLength());
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("time", time->toString())
            .field("measureLength", time->measureLength().toString())
            .field("beatLength", time->beatLength().toString())
            .key("tempo")
            .beginObject()
            .field("bpm", options.bpm)
            .field("beatUnit", options.beatUnit)
            .endObject()
            .field("secondsPerMeasure", seconds)
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("{}: {} of a whole note, {:.4f} s per measure at {} = {}\n",
                                 time->toString(), time->measureLength().toString(), seconds,
                                 options.beatUnit, options.bpm);
    }
    return kExitOk;
}

// ---- rests ----------------------------------------------------------------------

struct RestsOptions {
    std::string time;
    std::vector<std::string> durations;
};

std::string_view fillName(core::VoiceFill fill) {
    switch (fill) {
    case core::VoiceFill::Empty:
        return "empty";
    case core::VoiceFill::Incomplete:
        return "incomplete";
    case core::VoiceFill::Complete:
        return "complete";
    case core::VoiceFill::Overfull:
        return "overfull";
    }
    return "unknown";
}

int runRests(bool json, const RestsOptions& options) {
    const auto time = core::TimeSignature::parse(options.time);
    if (!time) {
        return usageError(
            std::format("not a time signature (expected e.g. 4/4, 6/8): {}", options.time));
    }
    core::Voice voice;
    for (const auto& text : options.durations) {
        const auto duration = core::Duration::parse(text);
        if (!duration) {
            return usageError(
                std::format("not a duration (expected e.g. 4, 8., 16*3:2): {}", text));
        }
        voice.beats.push_back(core::Beat{.duration = *duration, .notes = {core::Note{}}});
    }
    const core::VoiceFill fill = core::voiceFill(voice, *time);
    const std::size_t written = voice.beats.size();
    if (!core::fillWithRests(voice, *time)) {
        return fail(
            fill == core::VoiceFill::Overfull
                ? std::format("the durations are longer than a {} measure", time->toString())
                : std::string("the rest of the beat cannot be written with rests"));
    }
    std::vector<std::string> rests;
    for (std::size_t i = written; i < voice.beats.size(); ++i) {
        rests.push_back(voice.beats[i].duration.toString());
    }
    if (json) {
        JsonWriter out;
        out.beginObject().field("time", time->toString()).key("written");
        writeStrings(out, options.durations);
        out.field("fill", fillName(fill)).key("rests");
        writeStrings(out, rests);
        out.endObject();
        std::cout << out.text() << '\n';
    } else if (rests.empty()) {
        std::cout << std::format("{}: {}, no rests needed\n", time->toString(), fillName(fill));
    } else {
        std::string list;
        for (const auto& rest : rests) {
            list += (list.empty() ? "" : " ") + rest;
        }
        std::cout << std::format("{}: add rests {}\n", time->toString(), list);
    }
    return kExitOk;
}

} // namespace

void addTheoryCommands(CLI::App& app, Context& context) {
    auto pitch = std::make_shared<PitchOptions>();
    auto* pitchCommand =
        app.add_subcommand("pitch", "Show a pitch: spelling, MIDI number and frequency");
    pitchCommand->add_option("pitch", pitch->input, "Pitch such as C4, F#3, Bb2, or a MIDI number")
        ->required();
    pitchCommand->add_flag("--flats", pitch->flats, "Spell MIDI numbers with flats, not sharps");
    pitchCommand->add_option("--a4", pitch->a4Hz, "Reference frequency of A4 in Hz")
        ->check(CLI::Range(400.0, 480.0))
        ->capture_default_str();
    pitchCommand->callback(
        [&context, pitch] { context.exitCode = runPitch(context.json, *pitch); });

    const std::string tuningHelp = "Tuning preset (see `tunings`) or open strings, lowest first";

    auto fret = std::make_shared<FretOptions>();
    auto* fretCommand = app.add_subcommand("fret", "Show the pitch of a string/fret position");
    fretCommand->add_option("--tuning", fret->tuning, tuningHelp)->capture_default_str();
    fretCommand->add_option("--string", fret->string, "String number, 1 = top tab line")
        ->required();
    fretCommand->add_option("--fret", fret->fret, "Fret, relative to the capo")->required();
    fretCommand->add_option("--capo", fret->capo, "Capo fret (0 = none)")->capture_default_str();
    fretCommand->add_flag("--flats", fret->flats, "Spell the result with flats, not sharps");
    fretCommand->callback([&context, fret] { context.exitCode = runFret(context.json, *fret); });

    auto positions = std::make_shared<PositionsOptions>();
    auto* positionsCommand =
        app.add_subcommand("positions", "List every string/fret position that sounds a pitch");
    positionsCommand->add_option("pitch", positions->pitch, "Pitch such as C4, F#3, Bb2")
        ->required();
    positionsCommand->add_option("--tuning", positions->tuning, tuningHelp)->capture_default_str();
    positionsCommand->add_option("--capo", positions->capo, "Capo fret (0 = none)")
        ->check(CLI::Range(0, Tuning::kMaxFret))
        ->capture_default_str();
    positionsCommand->add_option("--max-fret", positions->maxFret, "Highest fret above the capo")
        ->check(CLI::Range(0, Tuning::kMaxFret))
        ->capture_default_str();
    positionsCommand->callback(
        [&context, positions] { context.exitCode = runPositions(context.json, *positions); });

    app.add_subcommand("tunings", "List the tuning presets")->callback([&context] {
        context.exitCode = runTunings(context.json);
    });

    auto keyName = std::make_shared<std::string>();
    auto* keyCommand = app.add_subcommand("key", "Show a key signature: tonic and accidentals");
    keyCommand->add_option("key", *keyName, "Key such as \"D major\" or \"Bb minor\"")->required();
    keyCommand->callback(
        [&context, keyName] { context.exitCode = runKey(context.json, *keyName); });

    auto spell = std::make_shared<SpellOptions>();
    auto* spellCommand = app.add_subcommand("spell", "Spell a MIDI number in a key");
    spellCommand->add_option("midi", spell->midi, "MIDI note number, 0..127")->required();
    spellCommand->add_option("--key", spell->key, "Key such as \"D minor\"")->capture_default_str();
    spellCommand->callback(
        [&context, spell] { context.exitCode = runSpell(context.json, *spell); });

    auto meter = std::make_shared<MeterOptions>();
    auto* meterCommand =
        app.add_subcommand("meter", "Show a time signature's lengths and duration at a tempo");
    meterCommand->add_option("time", meter->time, "Time signature such as 4/4 or 6/8")->required();
    meterCommand->add_option("--bpm", meter->bpm, "Beats per minute")
        ->check(CLI::Range(core::Tempo::kMinBpm, core::Tempo::kMaxBpm))
        ->capture_default_str();
    meterCommand
        ->add_option("--beat-unit", meter->beatUnit,
                     "whole, half, quarter, eighth, sixteenth, optionally dotted- (dotted-quarter)")
        ->capture_default_str();
    meterCommand->callback(
        [&context, meter] { context.exitCode = runMeter(context.json, *meter); });

    auto rests = std::make_shared<RestsOptions>();
    auto* restsCommand =
        app.add_subcommand("rests", "Show the rests that complete a measure after some durations");
    restsCommand->add_option("time", rests->time, "Time signature such as 4/4 or 6/8")->required();
    restsCommand
        ->add_option("durations", rests->durations,
                     "Written durations in order: 4 = quarter, 8. = dotted eighth, 8*3:2 = triplet")
        ->required();
    restsCommand->callback(
        [&context, rests] { context.exitCode = runRests(context.json, *rests); });
}

} // namespace elara::cli
