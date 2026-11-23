// SPDX-License-Identifier: AGPL-3.0-or-later
// elara-cli: headless access to Elara for tests and scripts (ADR-0003).

#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/wav_writer.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/tuning.hpp>
#include <elara/core/version.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    return out;
}

int fail(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitError;
}

struct ToneOptions {
    double frequencyHz = 440.0;
    double seconds = 1.0;
};

void addToneOptions(CLI::App& command, ToneOptions& options) {
    command.add_option("--frequency", options.frequencyHz, "Frequency in Hz")
        ->check(CLI::Range(20.0, 20000.0))
        ->capture_default_str();
    command.add_option("--seconds", options.seconds, "Duration in seconds")
        ->check(CLI::Range(0.01, 60.0))
        ->capture_default_str();
}

int runVersion(bool json) {
    if (json) {
        std::cout << std::format(R"({{"name":"elara","version":"{}"}})", elara::core::version())
                  << '\n';
    } else {
        std::cout << "elara " << elara::core::version() << '\n';
    }
    return kExitOk;
}

struct PitchOptions {
    std::string input; // pitch text ("F#3") or MIDI number ("61")
    bool flats = false;
    double a4Hz = 440.0;
};

int usageError(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitUsage;
}

int runPitch(bool json, const PitchOptions& options) {
    using elara::core::Pitch;
    const bool isNumber =
        !options.input.empty() &&
        std::ranges::all_of(options.input, [](unsigned char c) { return std::isdigit(c) != 0; });
    std::optional<Pitch> pitch;
    if (isNumber) {
        const int midi = options.input.size() > 3 ? 128 : std::stoi(options.input);
        if (midi > 127) {
            return usageError(std::format("MIDI number out of range 0..127: {}", options.input));
        }
        pitch = Pitch::fromMidi(midi, options.flats ? elara::core::SpellingPreference::Flats
                                                    : elara::core::SpellingPreference::Sharps);
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
        std::cout
            << std::format(
                   R"({{"pitch":"{}","step":"{}","alter":{},"octave":{},"midi":{},"frequency":{:.4f}}})",
                   jsonEscape(text), text.front(), pitch->alter(), pitch->octave(),
                   pitch->midiNumber(), frequency)
            << '\n';
    } else {
        std::cout << std::format("{}  MIDI {}  {:.4f} Hz\n", text, pitch->midiNumber(), frequency);
    }
    return kExitOk;
}

// --tuning takes a preset name ("guitar", "bass-5") or open strings ("D2 A2 D3 G3 B3 E4").
std::optional<elara::core::Tuning> tuningFromOption(std::string_view text) {
    if (auto preset = elara::core::Tuning::preset(text)) {
        return preset;
    }
    return elara::core::Tuning::parse(text);
}

struct FretOptions {
    std::string tuning = "guitar";
    int string = 1;
    int fret = 0;
    int capo = 0;
    bool flats = false;
};

int runFret(bool json, const FretOptions& options) {
    using elara::core::Tuning;
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
    const auto pitch =
        elara::core::Pitch::fromMidi(midi, options.flats ? elara::core::SpellingPreference::Flats
                                                         : elara::core::SpellingPreference::Sharps);
    if (json) {
        std::cout
            << std::format(
                   R"({{"tuning":"{}","string":{},"fret":{},"capo":{},"midi":{},"pitch":"{}","frequency":{:.4f}}})",
                   tuning->toString(), options.string, options.fret, options.capo, midi,
                   pitch.toString(), pitch.frequency())
            << '\n';
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
    int maxFret = elara::core::Tuning::kMaxFret;
};

int runPositions(bool json, const PositionsOptions& options) {
    const auto tuning = tuningFromOption(options.tuning);
    if (!tuning) {
        return usageError(std::format("unknown tuning: {}", options.tuning));
    }
    const auto pitch = elara::core::Pitch::parse(options.pitch);
    if (!pitch) {
        return usageError(
            std::format("not a pitch (expected e.g. C4, F#3, Bb2): {}", options.pitch));
    }
    const auto positions = tuning->positionsFor(*pitch, options.capo, options.maxFret);
    if (json) {
        std::string list;
        for (const auto& position : positions) {
            if (!list.empty()) {
                list += ',';
            }
            list += std::format(R"({{"string":{},"fret":{}}})", position.string, position.fret);
        }
        std::cout << std::format(
                         R"({{"pitch":"{}","midi":{},"tuning":"{}","capo":{},"positions":[{}]}})",
                         pitch->toString(), pitch->midiNumber(), tuning->toString(), options.capo,
                         list)
                  << '\n';
    } else {
        for (const auto& position : positions) {
            std::cout << std::format("string {} fret {}\n", position.string, position.fret);
        }
    }
    return kExitOk;
}

int runTunings(bool json) {
    using elara::core::Tuning;
    std::string list;
    for (const auto name : Tuning::presetNames()) {
        const auto strings = Tuning::preset(name)->toString();
        if (json) {
            if (!list.empty()) {
                list += ',';
            }
            list += std::format(R"({{"name":"{}","strings":"{}"}})", name, strings);
        } else {
            std::cout << std::format("{:<24} {}\n", name, strings);
        }
    }
    if (json) {
        std::cout << R"({"tunings":[)" << list << "]}\n";
    }
    return kExitOk;
}

int runRenderTone(bool json, const ToneOptions& tone, const std::string& out,
                  std::uint32_t sampleRate) {
    const auto samples = elara::audio::renderTone(tone.frequencyHz, static_cast<double>(sampleRate),
                                                  tone.seconds, 0.2f);
    if (auto written =
            elara::audio::writeWavPcm16(out, samples, {.sampleRate = sampleRate, .channels = 1});
        !written) {
        return fail(written.error());
    }
    const auto peak = elara::audio::peakLevel(samples);
    if (json) {
        std::cout
            << std::format(
                   R"({{"path":"{}","frames":{},"sampleRate":{},"channels":1,"peak":{:.4f}}})",
                   jsonEscape(out), samples.size(), sampleRate, peak)
            << '\n';
    } else {
        std::cout << std::format("wrote {} ({} frames @ {} Hz, peak {:.4f})\n", out, samples.size(),
                                 sampleRate, peak);
    }
    return kExitOk;
}

int runDevices(bool json) {
    const auto devices = elara::audio::listOutputDevices();
    if (json) {
        std::string body;
        for (const auto& device : devices) {
            if (!body.empty()) {
                body += ',';
            }
            body +=
                std::format(R"({{"type":"{}","name":"{}","default":{}}})", jsonEscape(device.type),
                            jsonEscape(device.name), device.isDefault ? "true" : "false");
        }
        std::cout << R"({"outputs":[)" << body << "]}\n";
    } else {
        for (const auto& device : devices) {
            std::cout << std::format("{} {} / {}\n", device.isDefault ? '*' : ' ', device.type,
                                     device.name);
        }
    }
    return kExitOk;
}

int runTone(bool json, const ToneOptions& tone) {
    elara::audio::SystemAudio audio;
    if (auto opened = audio.open(); !opened) {
        return fail(opened.error());
    }
    audio.playTone(tone.frequencyHz, tone.seconds);
    using namespace std::chrono_literals;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(tone.seconds) + 2s;
    while (audio.isPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    const bool finished = !audio.isPlaying();
    if (json) {
        std::cout << std::format(R"({{"device":"{}","frequency":{},"seconds":{},"finished":{}}})",
                                 jsonEscape(audio.deviceDescription()), tone.frequencyHz,
                                 tone.seconds, finished ? "true" : "false")
                  << '\n';
    } else {
        std::cout << std::format("played {} Hz for {} s on {}\n", tone.frequencyHz, tone.seconds,
                                 audio.deviceDescription());
    }
    return finished ? kExitOk : fail("tone did not finish; the audio callback may not be running");
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Elara command-line tools (headless)", "elara-cli"};
    app.require_subcommand(1);
    app.fallthrough();

    bool json = false;
    app.add_flag("--json", json, "Print one machine-readable JSON object on stdout");

    auto* version = app.add_subcommand("version", "Print the Elara version");

    ToneOptions renderOptions;
    std::string renderOut;
    std::uint32_t renderRate = 48000;
    auto* renderTone =
        app.add_subcommand("render-tone", "Render a sine test tone to a 16-bit PCM WAV file");
    addToneOptions(*renderTone, renderOptions);
    renderTone->add_option("--out", renderOut, "Output WAV path")->required();
    renderTone->add_option("--sample-rate", renderRate, "Sample rate in Hz")
        ->check(CLI::IsMember({44100, 48000, 96000}))
        ->capture_default_str();

    auto* devices = app.add_subcommand("devices", "List audio output devices");

    ToneOptions playOptions;
    auto* tone = app.add_subcommand("tone", "Play a sine test tone on the default output device");
    addToneOptions(*tone, playOptions);

    PitchOptions pitchOptions;
    auto* pitch = app.add_subcommand("pitch", "Show a pitch: spelling, MIDI number and frequency");
    pitch->add_option("pitch", pitchOptions.input, "Pitch such as C4, F#3, Bb2, or a MIDI number")
        ->required();
    pitch->add_flag("--flats", pitchOptions.flats, "Spell MIDI numbers with flats, not sharps");
    pitch->add_option("--a4", pitchOptions.a4Hz, "Reference frequency of A4 in Hz")
        ->check(CLI::Range(400.0, 480.0))
        ->capture_default_str();

    const std::string tuningHelp = "Tuning preset (see `tunings`) or open strings, lowest first";

    FretOptions fretOptions;
    auto* fret = app.add_subcommand("fret", "Show the pitch of a string/fret position");
    fret->add_option("--tuning", fretOptions.tuning, tuningHelp)->capture_default_str();
    fret->add_option("--string", fretOptions.string, "String number, 1 = top tab line")->required();
    fret->add_option("--fret", fretOptions.fret, "Fret, relative to the capo")->required();
    fret->add_option("--capo", fretOptions.capo, "Capo fret (0 = none)")->capture_default_str();
    fret->add_flag("--flats", fretOptions.flats, "Spell the result with flats, not sharps");

    PositionsOptions positionsOptions;
    auto* positions =
        app.add_subcommand("positions", "List every string/fret position that sounds a pitch");
    positions->add_option("pitch", positionsOptions.pitch, "Pitch such as C4, F#3, Bb2")
        ->required();
    positions->add_option("--tuning", positionsOptions.tuning, tuningHelp)->capture_default_str();
    positions->add_option("--capo", positionsOptions.capo, "Capo fret (0 = none)")
        ->check(CLI::Range(0, elara::core::Tuning::kMaxFret))
        ->capture_default_str();
    positions->add_option("--max-fret", positionsOptions.maxFret, "Highest fret above the capo")
        ->check(CLI::Range(0, elara::core::Tuning::kMaxFret))
        ->capture_default_str();

    auto* tunings = app.add_subcommand("tunings", "List the tuning presets");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        const int code = app.exit(error);
        return code == 0 ? kExitOk : kExitUsage;
    }

    try {
        if (*version) {
            return runVersion(json);
        }
        if (*renderTone) {
            return runRenderTone(json, renderOptions, renderOut, renderRate);
        }
        if (*devices) {
            return runDevices(json);
        }
        if (*tone) {
            return runTone(json, playOptions);
        }
        if (*pitch) {
            return runPitch(json, pitchOptions);
        }
        if (*fret) {
            return runFret(json, fretOptions);
        }
        if (*positions) {
            return runPositions(json, positionsOptions);
        }
        if (*tunings) {
            return runTunings(json);
        }
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    return kExitUsage;
}
