// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio subcommands: render-tone, devices, tone.
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/wav_writer.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace elara::cli {

namespace {

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

struct RenderOptions {
    ToneOptions tone;
    std::string out;
    std::uint32_t sampleRate = 48000;
};

int runRenderTone(bool json, const RenderOptions& options) {
    const auto& tone = options.tone;
    const auto samples = audio::renderTone(
        tone.frequencyHz, static_cast<double>(options.sampleRate), tone.seconds, 0.2f);
    if (auto written = audio::writeWavPcm16(options.out, samples,
                                            {.sampleRate = options.sampleRate, .channels = 1});
        !written) {
        return fail(written.error());
    }
    const auto peak = audio::peakLevel(samples);
    if (json) {
        std::cout
            << std::format(
                   R"({{"path":"{}","frames":{},"sampleRate":{},"channels":1,"peak":{:.4f}}})",
                   jsonEscape(options.out), samples.size(), options.sampleRate, peak)
            << '\n';
    } else {
        std::cout << std::format("wrote {} ({} frames @ {} Hz, peak {:.4f})\n", options.out,
                                 samples.size(), options.sampleRate, peak);
    }
    return kExitOk;
}

int runDevices(bool json) {
    const auto devices = audio::listOutputDevices();
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
    audio::SystemAudio system;
    if (auto opened = system.open(); !opened) {
        return fail(opened.error());
    }
    system.playTone(tone.frequencyHz, tone.seconds);
    using namespace std::chrono_literals;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(tone.seconds) + 2s;
    while (system.isPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    const bool finished = !system.isPlaying();
    if (json) {
        std::cout << std::format(R"({{"device":"{}","frequency":{},"seconds":{},"finished":{}}})",
                                 jsonEscape(system.deviceDescription()), tone.frequencyHz,
                                 tone.seconds, finished ? "true" : "false")
                  << '\n';
    } else {
        std::cout << std::format("played {} Hz for {} s on {}\n", tone.frequencyHz, tone.seconds,
                                 system.deviceDescription());
    }
    return finished ? kExitOk : fail("tone did not finish; the audio callback may not be running");
}

} // namespace

void addAudioCommands(CLI::App& app, Context& context) {
    auto render = std::make_shared<RenderOptions>();
    auto* renderTone =
        app.add_subcommand("render-tone", "Render a sine test tone to a 16-bit PCM WAV file");
    addToneOptions(*renderTone, render->tone);
    renderTone->add_option("--out", render->out, "Output WAV path")->required();
    renderTone->add_option("--sample-rate", render->sampleRate, "Sample rate in Hz")
        ->check(CLI::IsMember({44100, 48000, 96000}))
        ->capture_default_str();
    renderTone->callback(
        [&context, render] { context.exitCode = runRenderTone(context.json, *render); });

    app.add_subcommand("devices", "List audio output devices")->callback([&context] {
        context.exitCode = runDevices(context.json);
    });

    auto play = std::make_shared<ToneOptions>();
    auto* tone = app.add_subcommand("tone", "Play a sine test tone on the default output device");
    addToneOptions(*tone, *play);
    tone->callback([&context, play] { context.exitCode = runTone(context.json, *play); });
}

} // namespace elara::cli
