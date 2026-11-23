// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio subcommands: sequence, render-tone, devices, tone.
#include <elara/audio/sequence.hpp>
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/wav_writer.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
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

struct SequenceOptions {
    std::string file;
    bool notes = false;
    bool events = false;
};

std::string_view kindName(audio::EventKind kind) {
    return kind == audio::EventKind::NoteOn ? "on" : "off";
}

std::string sequenceJson(const audio::Sequence& sequence, const SequenceOptions& options) {
    std::string json = std::format(R"({{"file":"{}","duration":{},"measures":[)",
                                   jsonEscape(options.file), jsonNumber(sequence.duration()));
    for (std::size_t k = 0; k < sequence.measures().size(); ++k) {
        const audio::PlayedMeasure& measure = sequence.measures()[k];
        json += std::format(R"({}{{"measure":{},"start":{},"end":{}}})", k == 0 ? "" : ",",
                            measure.measure, jsonNumber(measure.start), jsonNumber(measure.end));
    }
    json += std::format(R"(],"noteCount":{},"eventCount":{})", sequence.notes().size(),
                        sequence.events().size());
    if (options.notes) {
        json += R"(,"notes":[)";
        for (std::size_t i = 0; i < sequence.notes().size(); ++i) {
            const audio::PlayedNote& note = sequence.notes()[i];
            json += std::format(
                R"({}{{"track":{},"staff":{},"voice":{},"pass":{},"start":{},"end":{},"key":{},"velocity":{}}})",
                i == 0 ? "" : ",", note.track, note.staff, note.voice, note.pass,
                jsonNumber(note.start), jsonNumber(note.end), note.key, note.velocity);
        }
        json += ']';
    }
    if (options.events) {
        json += R"(,"events":[)";
        for (std::size_t i = 0; i < sequence.events().size(); ++i) {
            const audio::NoteEvent& event = sequence.events()[i];
            json += std::format(R"({}{{"time":{},"kind":"{}","track":{},"key":{},"velocity":{}}})",
                                i == 0 ? "" : ",", jsonNumber(event.time), kindName(event.kind),
                                event.track, event.key, event.velocity);
        }
        json += ']';
    }
    return json + '}';
}

int runSequence(bool json, const SequenceOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const audio::Sequence sequence(*song);
    if (json) {
        std::cout << sequenceJson(sequence, options) << '\n';
        return kExitOk;
    }
    std::string order;
    for (const audio::PlayedMeasure& measure : sequence.measures()) {
        order += std::format(" {}", measure.measure + 1);
    }
    std::cout << std::format("played measures:{}\n{} measures, {} notes, {} events in {:.3f} s\n",
                             order, sequence.measures().size(), sequence.notes().size(),
                             sequence.events().size(), sequence.duration());
    if (options.notes) {
        for (const audio::PlayedNote& note : sequence.notes()) {
            std::cout << std::format(
                "{:9.3f} {:9.3f}  track {} staff {} voice {}  key {:3} velocity {:3}\n", note.start,
                note.end, note.track, note.staff, note.voice, note.key, note.velocity);
        }
    }
    if (options.events) {
        for (const audio::NoteEvent& event : sequence.events()) {
            std::cout << std::format("{:9.3f} {:3}  track {}  key {:3} velocity {:3}\n", event.time,
                                     kindName(event.kind), event.track, event.key, event.velocity);
        }
    }
    return kExitOk;
}

} // namespace

void addAudioCommands(CLI::App& app, Context& context) {
    auto sequenced = std::make_shared<SequenceOptions>();
    auto* sequence = app.add_subcommand(
        "sequence", "Play a song through the sequencer without sound: the measures in play order "
                    "(repeats unrolled), its notes and note events, in seconds");
    sequence->add_option("file", sequenced->file, "A .elara file or any file import reads")
        ->required();
    sequence->add_flag("--notes", sequenced->notes, "List the notes as played");
    sequence->add_flag("--events", sequenced->events, "List the note events for a synthesizer");
    sequence->callback(
        [&context, sequenced] { context.exitCode = runSequence(context.json, *sequenced); });

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
