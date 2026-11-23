// SPDX-License-Identifier: AGPL-3.0-or-later
// elara-cli: headless access to Elara for tests and scripts (ADR-0003).

#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/wav_writer.hpp>
#include <elara/core/pitch.hpp>
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
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    return kExitUsage;
}
