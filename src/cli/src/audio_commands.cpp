// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio subcommands: sequence, render-audio, play, render-tone, devices, tone, tune, listen.
#include <elara/audio/microphone_input.hpp>
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/pitch_detector.hpp>
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/system_audio.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/audio/tuner.hpp>
#include <elara/audio/wav_reader.hpp>
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

// One JSON array of devices, or one line per device.
template <typename Device>
std::string devicesText(const std::vector<Device>& devices, bool json) {
    std::string text;
    for (const auto& device : devices) {
        if (json) {
            text += std::format(R"({}{{"type":"{}","name":"{}","default":{}}})",
                                text.empty() ? "" : ",", jsonEscape(device.type),
                                jsonEscape(device.name), device.isDefault ? "true" : "false");
        } else {
            text += std::format("  {} {} / {}\n", device.isDefault ? '*' : ' ', device.type,
                                device.name);
        }
    }
    return text;
}

int runDevices(bool json) {
    const auto outputs = audio::listOutputDevices();
    const auto inputs = audio::listInputDevices();
    if (json) {
        std::cout << std::format(R"({{"outputs":[{}],"inputs":[{}]}})", devicesText(outputs, true),
                                 devicesText(inputs, true))
                  << '\n';
    } else {
        std::cout << "outputs:\n"
                  << devicesText(outputs, false) << "inputs:\n"
                  << devicesText(inputs, false);
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
                body +=
                    std::format(R"({}{{"frame":{},"kind":"{}","track":{},"key":{},"velocity":{}}})",
                                i == 0 ? "" : ",", event.frame, kindName(event.kind), event.track,
                                event.key, event.velocity);
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
            std::cout << std::format("{:10} {:3}  track {}  key {:3} velocity {:3}\n", event.frame,
                                     kindName(event.kind), event.track, event.key, event.velocity);
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

struct TuneOptions {
    std::string file;
    double a4Hz = 440.0;
    std::string tuning;
};

// Cents with a sign and one decimal, never "-0.0".
std::string signedCents(double cents) {
    const double rounded = std::round(cents * 10.0) / 10.0;
    return std::format("{:+.1f}", rounded == 0.0 ? 0.0 : rounded);
}

// The tuner's reading of a recording: the median pitch of the windows, half a
// window apart, where the detector hears one clearly.
// The tuner's --tuning: none when empty. False for text that names no tuning.
bool tunerTuning(const std::string& text, std::optional<core::Tuning>& tuning) {
    if (text.empty()) {
        return true;
    }
    tuning = tuningFromOption(text);
    return tuning.has_value();
}

int runTune(bool json, const TuneOptions& options) {
    std::optional<core::Tuning> tuning;
    if (!tunerTuning(options.tuning, tuning)) {
        return usageError(std::format("unknown tuning: {}", options.tuning));
    }
    const auto wav = audio::readWav(options.file);
    if (!wav) {
        return fail(std::format("{}: {}", options.file, wav.error()));
    }
    const double rate = wav->format.sampleRate;
    if (rate <= 2.0 * audio::PitchDetector::kMaxFrequency) {
        return fail(std::format("{}: a sample rate of {} Hz is too low for the tuner", options.file,
                                wav->format.sampleRate));
    }
    const std::vector<float> samples = wav->mono();
    audio::PitchDetector detector(rate);
    const std::size_t window = detector.frameSize();
    std::size_t windows = 0;
    std::vector<audio::PitchEstimate> heard;
    for (std::size_t at = 0; at + window <= samples.size(); at += window / 2) {
        ++windows;
        if (const auto estimate = detector.detect(std::span(samples).subspan(at, window))) {
            heard.push_back(*estimate);
        }
    }
    if (heard.empty()) {
        return fail(std::format("{}: no clear pitch", options.file));
    }
    std::ranges::sort(heard, {}, &audio::PitchEstimate::frequency);
    const std::size_t middle = heard.size() / 2;
    const double frequency = heard.size() % 2 == 1
                                 ? heard[middle].frequency
                                 : (heard[middle - 1].frequency + heard[middle].frequency) / 2.0;
    double clarity = 0.0;
    for (const audio::PitchEstimate& estimate : heard) {
        clarity += estimate.clarity;
    }
    clarity /= static_cast<double>(heard.size());
    const auto reading = audio::tunerReading(frequency, options.a4Hz, tuning);
    if (!reading) {
        return fail(std::format("{}: {} Hz has no note", options.file, frequency));
    }

    if (json) {
        std::string out = std::format(
            R"({{"file":"{}","sampleRate":{},"windows":{},"pitched":{},"a4":{},"frequency":{},"note":"{}","cents":{},"clarity":{})",
            jsonEscape(options.file), wav->format.sampleRate, windows, heard.size(),
            jsonNumber(options.a4Hz), jsonNumber(frequency), reading->note.toString(),
            jsonNumber(reading->cents), jsonNumber(clarity));
        if (tuning) {
            out += std::format(R"(,"tuning":"{}","string":{},"stringPitch":"{}","stringCents":{})",
                               tuning->toString(), reading->string,
                               tuning->openString(reading->string).toString(),
                               jsonNumber(reading->stringCents));
        }
        std::cout << out << "}\n";
        return kExitOk;
    }
    std::cout << std::format("{} {} cents ({:.2f} Hz, clarity {:.2f}, {} of {} windows)\n",
                             reading->note.toString(), signedCents(reading->cents), frequency,
                             clarity, heard.size(), windows);
    if (tuning) {
        std::cout << std::format("string {} ({}) {} cents\n", reading->string,
                                 tuning->openString(reading->string).toString(),
                                 signedCents(reading->stringCents));
    }
    return kExitOk;
}

struct ListenOptions {
    double seconds = 5.0;
    double a4Hz = 440.0;
    std::string tuning;
};

// One reading of `listen`: what the tuner shows from `time` seconds on.
std::string listenJson(double time, const std::optional<audio::TunerReading>& reading,
                       const std::optional<core::Tuning>& tuning) {
    if (!reading) {
        return std::format(R"({{"time":{},"note":null}})", jsonNumber(time));
    }
    std::string out = std::format(R"({{"time":{},"note":"{}","cents":{},"frequency":{})",
                                  jsonNumber(time), reading->note.toString(),
                                  jsonNumber(reading->cents), jsonNumber(reading->frequency));
    if (tuning) {
        out += std::format(R"(,"string":{},"stringCents":{})", reading->string,
                           jsonNumber(reading->stringCents));
    }
    return out + '}';
}

// Listens to the default input for a while and reports each change of the
// tuner's reading as it happens.
int runListen(bool json, const ListenOptions& options) {
    std::optional<core::Tuning> tuning;
    if (!tunerTuning(options.tuning, tuning)) {
        return usageError(std::format("unknown tuning: {}", options.tuning));
    }
    audio::MicrophoneInput input;
    if (auto opened = input.open(); !opened) {
        return fail(opened.error());
    }
    if (input.sampleRate() <= 2.0 * audio::PitchDetector::kMaxFrequency) {
        return fail(
            std::format("a sample rate of {} Hz is too low for the tuner", input.sampleRate()));
    }
    audio::Tuner tuner(input.sampleRate(), {.a4 = options.a4Hz, .tuning = tuning});
    if (!json) {
        std::cout << std::format("listening on {} for {} s\n", input.deviceDescription(),
                                 options.seconds);
    }
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::now();
    std::vector<float> block(8192);
    std::string readings;
    float peak = 0.0f;
    for (;;) {
        std::this_thread::sleep_for(20ms);
        const double time =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (std::size_t got = input.read(block); got > 0; got = input.read(block)) {
            if (!tuner.push(std::span(block).first(got))) {
                continue;
            }
            const auto& reading = tuner.reading();
            if (json) {
                readings += (readings.empty() ? "" : ",") + listenJson(time, reading, tuning);
            } else if (!reading) {
                std::cout << std::format("{:7.2f} s  -\n", time) << std::flush;
            } else {
                std::cout << std::format("{:7.2f} s  {} {} cents ({:.2f} Hz){}\n", time,
                                         reading->note.toString(), signedCents(reading->cents),
                                         reading->frequency,
                                         tuning
                                             ? std::format(", string {} {} cents", reading->string,
                                                           signedCents(reading->stringCents))
                                             : std::string())
                          << std::flush;
            }
        }
        peak = std::max(peak, input.takePeak());
        if (time >= options.seconds) {
            break;
        }
    }
    if (json) {
        std::cout
            << std::format(
                   R"({{"device":"{}","sampleRate":{},"seconds":{},"peak":{},"dropped":{},"readings":[{}]}})",
                   jsonEscape(input.deviceDescription()), jsonNumber(input.sampleRate()),
                   jsonNumber(options.seconds), jsonNumber(static_cast<double>(peak)),
                   input.dropped(), readings)
            << '\n';
    } else {
        std::cout << std::format("peak level {:.3f}\n", peak);
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

    app.add_subcommand("devices", "List audio output and input devices")->callback([&context] {
        context.exitCode = runDevices(context.json);
    });

    auto play = std::make_shared<ToneOptions>();
    auto* tone = app.add_subcommand("tone", "Play a sine test tone on the default output device");
    addToneOptions(*tone, *play);
    tone->callback([&context, play] { context.exitCode = runTone(context.json, *play); });

    auto tuned = std::make_shared<TuneOptions>();
    auto* tune = app.add_subcommand(
        "tune", "Detect the pitch of a WAV recording, as a tuner would: the nearest note, how many "
                "cents off, and with --tuning the nearest open string");
    tune->add_option("file", tuned->file, "A WAV file (PCM or float, any channels)")->required();
    tune->add_option("--a4", tuned->a4Hz, "Reference frequency of A4 in Hz")
        ->check(CLI::Range(400.0, 480.0))
        ->capture_default_str();
    tune->add_option("--tuning", tuned->tuning,
                     "Name the nearest open string of a preset (\"guitar\", \"bass-5\") or of "
                     "open strings (\"D2 A2 D3 G3 B3 E4\")");
    tune->callback([&context, tuned] { context.exitCode = runTune(context.json, *tuned); });

    auto listening = std::make_shared<ListenOptions>();
    auto* listen = app.add_subcommand(
        "listen", "Tune from the default audio input: print each change of the tuner's reading "
                  "(note, cents, and with --tuning the nearest open string) while listening");
    listen->add_option("--seconds", listening->seconds, "How long to listen")
        ->check(CLI::Range(0.1, 3600.0))
        ->capture_default_str();
    listen->add_option("--a4", listening->a4Hz, "Reference frequency of A4 in Hz")
        ->check(CLI::Range(400.0, 480.0))
        ->capture_default_str();
    listen->add_option("--tuning", listening->tuning,
                       "Name the nearest open string of a preset or of open strings");
    listen->callback(
        [&context, listening] { context.exitCode = runListen(context.json, *listening); });
}

} // namespace elara::cli
