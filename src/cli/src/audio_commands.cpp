// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio subcommands: sequence, render-audio, play, render-tone, devices, tone.
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/wav_writer.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
    switch (kind) {
    case audio::EventKind::NoteOn:
        return "on";
    case audio::EventKind::NoteOff:
        return "off";
    case audio::EventKind::Pitch:
        return "pitch";
    }
    return "off";
}

// How a note is struck (ADR-0022), printed only when it is not normal.
std::string touchJson(audio::Touch touch) {
    return touch == audio::Touch::Normal ? std::string()
                                         : std::format(R"(,"touch":"{}")", audio::touchName(touch));
}

std::string touchText(audio::Touch touch) {
    return touch == audio::Touch::Normal ? std::string()
                                         : std::format("  {}", audio::touchName(touch));
}

// What an event does after its key: the velocity it strikes or releases
// with, or the offset a pitch event moves to and how long it takes.
template <typename Event>
std::string eventJson(const Event& event) {
    if (event.kind == audio::EventKind::Pitch) {
        return std::format(R"("cents":{},"glide":{})", event.cents,
                           jsonNumber(static_cast<double>(event.glide)));
    }
    return std::format(R"("velocity":{}{})", event.velocity, touchJson(event.touch));
}

template <typename Event>
std::string eventText(const Event& event) {
    if (event.kind == audio::EventKind::Pitch) {
        return std::format("cents {:5} glide {:.3f}", event.cents, event.glide);
    }
    return std::format("velocity {:3}{}", event.velocity, touchText(event.touch));
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
                R"({}{{"track":{},"staff":{},"voice":{},"pass":{},"start":{},"end":{},"key":{},"velocity":{}{}{}}})",
                i == 0 ? "" : ",", note.track, note.staff, note.voice, note.pass,
                jsonNumber(note.start), jsonNumber(note.end), note.key, note.velocity,
                touchJson(note.touch), note.struck ? "" : R"(,"struck":false)");
        }
        json += ']';
    }
    if (options.events) {
        json += R"(,"events":[)";
        for (std::size_t i = 0; i < sequence.events().size(); ++i) {
            const audio::NoteEvent& event = sequence.events()[i];
            json += std::format(R"({}{{"time":{},"kind":"{}","track":{},"key":{},{}}})",
                                i == 0 ? "" : ",", jsonNumber(event.time), kindName(event.kind),
                                event.track, event.key, eventJson(event));
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
                "{:9.3f} {:9.3f}  track {} staff {} voice {}  key {:3} velocity {:3}{}{}\n",
                note.start, note.end, note.track, note.staff, note.voice, note.key, note.velocity,
                touchText(note.touch), note.struck ? "" : "  legato");
        }
    }
    if (options.events) {
        for (const audio::NoteEvent& event : sequence.events()) {
            std::cout << std::format("{:9.3f} {:3}  track {}  key {:3} {}\n", event.time,
                                     kindName(event.kind), event.track, event.key,
                                     eventText(event));
        }
    }
    return kExitOk;
}

struct RenderAudioOptions {
    std::string file;
    std::string out;
    std::uint32_t sampleRate = 48000;
    double tail = 2.0;
    bool events = false;
};

int runRenderAudio(bool json, const RenderAudioOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const audio::Sequence sequence(*song);
    audio::OfflineRenderer renderer(*song, sequence,
                                    {.sampleRate = static_cast<double>(options.sampleRate),
                                     .tail = options.tail,
                                     .logEvents = true});
    std::optional<audio::WavFileWriter> writer;
    if (!options.out.empty()) {
        auto created = audio::WavFileWriter::create(
            options.out, {.sampleRate = options.sampleRate, .channels = 2},
            static_cast<std::uint64_t>(renderer.frames()));
        if (!created) {
            return fail(created.error());
        }
        writer.emplace(std::move(*created));
    }

    // One second at a time: tracks render in parallel inside each call.
    std::vector<float> block(2 * static_cast<std::size_t>(options.sampleRate));
    float peak = 0.0f;
    double sumOfSquares = 0.0;
    while (const std::size_t frames = renderer.render(block)) {
        const std::span<const float> rendered(block.data(), 2 * frames);
        peak = std::max(peak, audio::peakLevel(rendered));
        for (const float sample : rendered) {
            sumOfSquares += static_cast<double>(sample) * static_cast<double>(sample);
        }
        if (writer) {
            if (auto written = writer->write(rendered); !written) {
                return fail(written.error());
            }
        }
    }
    if (writer) {
        if (auto finished = writer->finish(); !finished) {
            return fail(finished.error());
        }
    }
    const auto frames = renderer.frames();
    const double seconds = static_cast<double>(frames) / options.sampleRate;
    const double rms =
        frames > 0 ? std::sqrt(sumOfSquares / (2.0 * static_cast<double>(frames))) : 0.0;
    const auto& log = renderer.log();

    if (json) {
        std::string body = std::format(
            R"({{"file":"{}","out":{},"sampleRate":{},"channels":2,"frames":{},"seconds":{},"peak":{:.4f},"rms":{:.4f},"tracks":[)",
            jsonEscape(options.file),
            options.out.empty() ? std::string("null") : '"' + jsonEscape(options.out) + '"',
            options.sampleRate, frames, jsonNumber(seconds), peak, rms);
        for (std::size_t t = 0; t < renderer.timbres().size(); ++t) {
            body += std::format(R"({}{{"track":{},"name":"{}","timbre":"{}"}})", t == 0 ? "" : ",",
                                t, jsonEscape(song->track(t).name()),
                                audio::timbreName(renderer.timbres()[t]));
        }
        body += std::format(R"(],"eventCount":{})", log.size());
        if (options.events) {
            body += R"(,"events":[)";
            for (std::size_t i = 0; i < log.size(); ++i) {
                const audio::LoggedEvent& event = log[i];
                body += std::format(R"({}{{"frame":{},"kind":"{}","track":{},"key":{},{}}})",
                                    i == 0 ? "" : ",", event.frame, kindName(event.kind),
                                    event.track, event.key, eventJson(event));
            }
            body += ']';
        }
        std::cout << body << "}\n";
        return kExitOk;
    }
    std::cout << std::format("rendered {:.3f} s ({} frames @ {} Hz, stereo), peak {:.4f}, "
                             "rms {:.4f}, {} events\n",
                             seconds, frames, options.sampleRate, peak, rms, log.size());
    for (std::size_t t = 0; t < renderer.timbres().size(); ++t) {
        std::cout << std::format("  track {} {}: {}\n", t + 1, song->track(t).name(),
                                 audio::timbreName(renderer.timbres()[t]));
    }
    if (!options.out.empty()) {
        std::cout << std::format("wrote {}\n", options.out);
    }
    if (options.events) {
        for (const audio::LoggedEvent& event : log) {
            std::cout << std::format("{:10} {:3}  track {}  key {:3} {}\n", event.frame,
                                     kindName(event.kind), event.track, event.key,
                                     eventText(event));
        }
    }
    return kExitOk;
}

struct PlayOptions {
    std::string file;
    double from = 0.0;
    double seconds = 0.0; // 0: to the end
};

int runPlay(bool json, const PlayOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const audio::Sequence sequence(*song);
    audio::SystemAudio system;
    if (auto opened = system.open(); !opened) {
        return fail(opened.error());
    }
    audio::Player player(system.sampleRate());
    player.load(*song, sequence);
    player.seek(options.from);
    const double from = player.position();
    player.play();
    system.setSource(&player);

    using namespace std::chrono_literals;
    const double length = options.seconds > 0.0 ? options.seconds : sequence.duration() - from;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(length) + 2s;
    while (player.isPlaying() && player.position() < from + length &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    const bool finished = player.position() >= from + length - 0.05;
    player.pause();
    std::this_thread::sleep_for(500ms); // let the last notes ring out
    const double to = player.position();
    system.setSource(nullptr);

    if (json) {
        std::cout << std::format(R"({{"file":"{}","device":"{}","from":{},"to":{},"finished":{}}})",
                                 jsonEscape(options.file), jsonEscape(system.deviceDescription()),
                                 jsonNumber(from), jsonNumber(to), finished ? "true" : "false")
                  << '\n';
    } else {
        std::cout << std::format("played {} from {:.3f} s to {:.3f} s on {}\n", options.file, from,
                                 to, system.deviceDescription());
    }
    return finished ? kExitOk
                    : fail("playback did not finish; the audio callback may not be running");
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

    auto rendering = std::make_shared<RenderAudioOptions>();
    auto* renderAudio = app.add_subcommand(
        "render-audio", "Play a song through the built-in synthesizer offline, optionally into a "
                        "16-bit stereo WAV file, and report its level and the events dispatched");
    renderAudio->add_option("file", rendering->file, "A .elara file or any file import reads")
        ->required();
    renderAudio->add_option("--out", rendering->out, "Output WAV path");
    renderAudio->add_option("--sample-rate", rendering->sampleRate, "Sample rate in Hz")
        ->check(CLI::IsMember({44100, 48000, 96000}))
        ->capture_default_str();
    renderAudio->add_option("--tail", rendering->tail, "Seconds rendered after the last measure")
        ->check(CLI::Range(0.0, 30.0))
        ->capture_default_str();
    renderAudio->add_flag("--events", rendering->events,
                          "List the note events dispatched, with their sample frames");
    renderAudio->callback(
        [&context, rendering] { context.exitCode = runRenderAudio(context.json, *rendering); });

    auto playing = std::make_shared<PlayOptions>();
    auto* playCommand = app.add_subcommand(
        "play", "Play a song through the built-in synthesizer on the default output device");
    playCommand->add_option("file", playing->file, "A .elara file or any file import reads")
        ->required();
    playCommand->add_option("--from", playing->from, "Start this many seconds into the song")
        ->check(CLI::NonNegativeNumber)
        ->capture_default_str();
    playCommand->add_option("--seconds", playing->seconds, "Stop after this many seconds")
        ->check(CLI::NonNegativeNumber);
    playCommand->callback(
        [&context, playing] { context.exitCode = runPlay(context.json, *playing); });

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
