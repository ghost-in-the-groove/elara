// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio subcommands: sequence, render-audio, play, soundfont, sfz, render-tone, devices, tone,
// tune, listen.
#include <elara/audio/flac_reader.hpp>
#include <elara/audio/microphone_input.hpp>
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/pitch_detector.hpp>
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/audio/sound_font.hpp>
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
#include <expected>
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
        JsonWriter out;
        out.beginObject()
            .field("path", options.out)
            .field("frames", samples.size())
            .field("sampleRate", options.sampleRate)
            .field("channels", 1)
            .field("peak", peak)
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("wrote {} ({} frames @ {} Hz, peak {:.4f})\n", options.out,
                                 samples.size(), options.sampleRate, peak);
    }
    return kExitOk;
}

// One line per device.
template <typename Device>
std::string devicesText(const std::vector<Device>& devices) {
    std::string text;
    for (const auto& device : devices) {
        text +=
            std::format("  {} {} / {}\n", device.isDefault ? '*' : ' ', device.type, device.name);
    }
    return text;
}

// A JSON array of devices.
template <typename Device>
void writeDevices(JsonWriter& json, const std::vector<Device>& devices) {
    json.beginArray();
    for (const auto& device : devices) {
        json.beginObject()
            .field("type", device.type)
            .field("name", device.name)
            .field("default", device.isDefault)
            .endObject();
    }
    json.endArray();
}

int runDevices(bool json) {
    const auto outputs = audio::listOutputDevices();
    const auto inputs = audio::listInputDevices();
    if (json) {
        JsonWriter out;
        out.beginObject().key("outputs");
        writeDevices(out, outputs);
        out.key("inputs");
        writeDevices(out, inputs);
        out.endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << "outputs:\n" << devicesText(outputs) << "inputs:\n" << devicesText(inputs);
    }
    return kExitOk;
}

constexpr const char* kDeviceHelp =
    "Output device, as `devices` lists it: \"TYPE / NAME\" or NAME (default: the system's)";

struct DeviceToneOptions {
    ToneOptions tone;
    std::string device;
};

int runTone(bool json, const DeviceToneOptions& options) {
    const auto& tone = options.tone;
    audio::SystemAudio system;
    if (auto opened = system.open(audio::parseOutputDeviceChoice(options.device)); !opened) {
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
        JsonWriter out;
        out.beginObject()
            .field("device", system.deviceDescription())
            .field("frequency", tone.frequencyHz)
            .field("seconds", tone.seconds)
            .field("finished", finished)
            .endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("played {} Hz for {} s on {}\n", tone.frequencyHz, tone.seconds,
                                 system.deviceDescription());
    }
    return finished ? kExitOk : fail("tone did not finish; the audio callback may not be running");
}

// How the playback commands play a song for practice (ADR-0024).
struct PracticeOptions {
    int speed = 100; // percent of the written tempo
    bool metronome = false;

    [[nodiscard]] audio::SequenceOptions sequence() const {
        return {.speed = speed / 100.0, .metronome = metronome};
    }
};

void addPracticeOptions(CLI::App& command, PracticeOptions& options) {
    command.add_option("--speed", options.speed, "Play at this percentage of the written tempo")
        ->check(CLI::Range(25, 200))
        ->capture_default_str();
    command.add_flag("--metronome", options.metronome,
                     "Click every beat, on a track after the song's last");
}

// `"speed":0.5,"metronome":true`
void writePractice(JsonWriter& json, const audio::Sequence& sequence) {
    json.field("speed", sequence.speed()).field("metronome", sequence.metronome());
}

// `{"clicks":4,"interval":0.5}`
void writeCountIn(JsonWriter& json, const audio::CountIn& countIn) {
    json.beginObject()
        .field("clicks", countIn.clicks)
        .field("interval", countIn.interval)
        .endObject();
}

struct SequenceOptions {
    std::string file;
    bool notes = false;
    bool events = false;
    PracticeOptions practice;
    std::optional<double> countInAt;
};

std::string_view kindName(audio::EventKind kind) {
    switch (kind) {
    case audio::EventKind::NoteOn:
        return "on";
    case audio::EventKind::NoteOff:
        return "off";
    case audio::EventKind::Legato:
        return "legato";
    case audio::EventKind::Pitch:
        return "pitch";
    }
    return "off";
}

// How a note is struck (ADR-0022), written only when it is not normal.
void writeTouch(JsonWriter& json, audio::Touch touch) {
    if (touch != audio::Touch::Normal) {
        json.field("touch", audio::touchName(touch));
    }
}

std::string touchText(audio::Touch touch) {
    return touch == audio::Touch::Normal ? std::string()
                                         : std::format("  {}", audio::touchName(touch));
}

// What an event does after its key: the velocity it strikes or releases
// with, the offset a pitch event moves to and how long it takes, or the
// offset and velocity of a note joined to the key.
template <typename Event>
void writeEvent(JsonWriter& json, const Event& event) {
    if (event.kind == audio::EventKind::Pitch) {
        json.field("cents", event.cents).field("glide", event.glide);
    } else if (event.kind == audio::EventKind::Legato) {
        json.field("cents", event.cents).field("velocity", event.velocity);
    } else {
        json.field("velocity", event.velocity);
        writeTouch(json, event.touch);
    }
}

template <typename Event>
std::string eventText(const Event& event) {
    if (event.kind == audio::EventKind::Pitch) {
        return std::format("cents {:5} glide {:.3f}", event.cents, event.glide);
    }
    if (event.kind == audio::EventKind::Legato) {
        return std::format("cents {:5} velocity {:3}", event.cents, event.velocity);
    }
    return std::format("velocity {:3}{}", event.velocity, touchText(event.touch));
}

std::string sequenceJson(const audio::Sequence& sequence, const SequenceOptions& options) {
    JsonWriter json;
    json.beginObject().field("file", options.file).field("duration", sequence.duration());
    writePractice(json, sequence);
    if (options.countInAt) {
        json.key("countIn");
        writeCountIn(json, sequence.countInAt(*options.countInAt));
    }
    json.key("measures").beginArray();
    for (const audio::PlayedMeasure& measure : sequence.measures()) {
        json.beginObject()
            .field("measure", measure.measure)
            .field("start", measure.start)
            .field("end", measure.end)
            .endObject();
    }
    json.endArray()
        .field("noteCount", sequence.notes().size())
        .field("eventCount", sequence.eventCount());
    if (options.notes) {
        json.key("notes").beginArray();
        for (const audio::PlayedNote& note : sequence.notes()) {
            json.beginObject()
                .field("track", note.track)
                .field("staff", note.staff)
                .field("voice", note.voice)
                .field("pass", note.pass)
                .field("start", note.start)
                .field("end", note.end)
                .field("key", note.key)
                .field("velocity", note.velocity);
            writeTouch(json, note.touch);
            if (!note.struck) {
                json.field("struck", false);
            }
            json.endObject();
        }
        json.endArray();
    }
    if (options.events) {
        json.key("events").beginArray();
        for (const audio::NoteEvent& event : sequence.events()) {
            json.beginObject()
                .field("time", event.time)
                .field("kind", kindName(event.kind))
                .field("track", event.track)
                .field("key", event.key);
            writeEvent(json, event);
            json.endObject();
        }
        json.endArray();
    }
    json.endObject();
    return json.text();
}

int runSequence(bool json, const SequenceOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const audio::Sequence sequence(*song, options.practice.sequence());
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
                             sequence.eventCount(), sequence.duration());
    if (options.practice.speed != 100 || sequence.metronome()) {
        std::cout << std::format(
            "at {} % speed{}\n", options.practice.speed,
            sequence.metronome() ? std::format(", metronome on track {}", sequence.metronomeTrack())
                                 : std::string());
    }
    if (options.countInAt) {
        const audio::CountIn countIn = sequence.countInAt(*options.countInAt);
        std::cout << std::format("count-in at {:.3f} s: {} clicks {:.3f} s apart\n",
                                 *options.countInAt, countIn.clicks, countIn.interval);
    }
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

// An SFZ instrument of --sfz and the tracks it plays.
struct SfzChoice {
    audio::SfzTracks tracks = audio::SfzTracks::Pitched;
    std::string file;
};

// The values of --sfz, each "[pitched=|guitars=|basses=]FILE".
std::expected<std::vector<SfzChoice>, std::string>
sfzChoices(const std::vector<std::string>& values) {
    std::vector<SfzChoice> choices;
    for (const std::string& value : values) {
        const auto equals = value.find('=');
        const std::string_view family(value.data(), equals == std::string::npos ? 0 : equals);
        if (family.empty() ||
            !std::ranges::all_of(family, [](char c) { return c >= 'a' && c <= 'z'; })) {
            choices.push_back({.file = value});
            continue;
        }
        const auto tracks = audio::sfzTracksFromName(family);
        if (!tracks) {
            return std::unexpected(
                std::format("--sfz plays pitched, guitars or basses tracks, not {}", family));
        }
        choices.push_back({.tracks = *tracks, .file = value.substr(equals + 1)});
    }
    return choices;
}

// The instruments of --sfz as a JSON list of their files and tracks.
void writeSfz(JsonWriter& json, const std::vector<SfzChoice>& choices) {
    json.beginArray();
    for (const SfzChoice& choice : choices) {
        json.beginObject()
            .field("file", choice.file)
            .field("tracks", audio::sfzTracksName(choice.tracks))
            .endObject();
    }
    json.endArray();
}

// The sound bank of --soundfont and --sfz: the built-in synthesizer when both
// are empty. Each SFZ instrument plays its tracks and leaves the others to the
// banks before it, down to the SoundFont or the built-in synthesizer, which
// play the percussion tracks. A file named twice is read once.
std::expected<std::shared_ptr<const audio::SoundBank>, std::string>
soundBankFrom(const std::string& soundFont, const std::vector<SfzChoice>& sfz = {}) {
    std::shared_ptr<const audio::SoundBank> bank = audio::builtinBank();
    if (!soundFont.empty()) {
        auto opened = audio::SoundFont::open(soundFont);
        if (!opened) {
            return std::unexpected(opened.error());
        }
        bank = *std::move(opened);
    }
    std::vector<std::shared_ptr<const audio::SfzBank>> read;
    for (std::size_t i = 0; i < sfz.size(); ++i) {
        std::shared_ptr<const audio::SfzBank> instrument;
        for (std::size_t j = 0; j < i && !instrument; ++j) {
            if (sfz[j].file == sfz[i].file) {
                instrument = read[j]->with(bank, sfz[i].tracks);
            }
        }
        if (!instrument) {
            auto opened = audio::SfzBank::open(sfz[i].file, bank, sfz[i].tracks);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            instrument = *std::move(opened);
        }
        read.push_back(instrument);
        bank = std::move(instrument);
    }
    return bank;
}

// A path as a JSON string, or null when it is empty.
void writePath(JsonWriter& json, std::string_view name, const std::string& path) {
    json.key(name);
    if (path.empty()) {
        json.null();
    } else {
        json.string(path);
    }
}

constexpr const char* kSfzHelp =
    "Play tracks with this SFZ instrument: every track but the percussion ones, or with "
    "guitars= or basses= only those. Repeatable; a later one takes its tracks from the earlier";

struct RenderAudioOptions {
    std::string file;
    std::string out;
    std::uint32_t sampleRate = 48000;
    double tail = 2.0;
    bool events = false;
    std::string soundFont;
    std::vector<std::string> sfz;
    PracticeOptions practice;
};

int runRenderAudio(bool json, const RenderAudioOptions& options) {
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const auto sfz = sfzChoices(options.sfz);
    if (!sfz) {
        return usageError(sfz.error());
    }
    const auto bank = soundBankFrom(options.soundFont, *sfz);
    if (!bank) {
        return fail(bank.error());
    }
    const audio::Sequence sequence(*song, options.practice.sequence());
    audio::OfflineRenderer renderer(*song, sequence,
                                    {.sampleRate = static_cast<double>(options.sampleRate),
                                     .tail = options.tail,
                                     .logEvents = true,
                                     .bank = *bank});
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
        JsonWriter out;
        out.beginObject().field("file", options.file);
        writePath(out, "out", options.out);
        writePath(out, "soundFont", options.soundFont);
        out.key("sfz");
        writeSfz(out, *sfz);
        out.field("sampleRate", options.sampleRate)
            .field("channels", 2)
            .field("frames", frames)
            .field("seconds", seconds)
            .field("peak", peak)
            .field("rms", rms)
            .key("tracks")
            .beginArray();
        for (std::size_t t = 0; t < renderer.timbres().size(); ++t) {
            out.beginObject()
                .field("track", t)
                .field("name", song->track(t).name())
                .field("timbre", audio::timbreName(renderer.timbres()[t]))
                .field("sound", renderer.sounds()[t])
                .endObject();
        }
        out.endArray().field("eventCount", log.size());
        if (options.events) {
            out.key("events").beginArray();
            for (const audio::LoggedEvent& event : log) {
                out.beginObject()
                    .field("frame", event.frame)
                    .field("kind", kindName(event.kind))
                    .field("track", event.track)
                    .field("key", event.key);
                writeEvent(out, event);
                out.endObject();
            }
            out.endArray();
        }
        writePractice(out, sequence);
        out.endObject();
        std::cout << out.text() << '\n';
        return kExitOk;
    }
    std::cout << std::format("rendered {:.3f} s ({} frames @ {} Hz, stereo), peak {:.4f}, "
                             "rms {:.4f}, {} events\n",
                             seconds, frames, options.sampleRate, peak, rms, log.size());
    for (std::size_t t = 0; t < renderer.sounds().size(); ++t) {
        std::cout << std::format("  track {} {}: {}\n", t + 1, song->track(t).name(),
                                 renderer.sounds()[t]);
    }
    if (options.practice.speed != 100) {
        std::cout << std::format("  at {} % speed\n", options.practice.speed);
    }
    if (sequence.metronome()) {
        std::cout << std::format("  metronome on track {}\n", sequence.metronomeTrack() + 1);
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
    double seconds = 0.0; // 0: to the end, or twice round the loop
    PracticeOptions practice;
    bool countIn = false;
    std::vector<double> loop; // from, to
    std::string soundFont;
    std::vector<std::string> sfz;
    std::string device;
};

int runPlay(bool json, const PlayOptions& options) {
    const bool looping = options.loop.size() == 2;
    if (looping && options.loop[1] <= options.loop[0]) {
        return usageError("--loop needs its start before its end");
    }
    const auto song = openSong(options.file);
    if (!song) {
        return fail(song.error());
    }
    const auto sfz = sfzChoices(options.sfz);
    if (!sfz) {
        return usageError(sfz.error());
    }
    const auto bank = soundBankFrom(options.soundFont, *sfz);
    if (!bank) {
        return fail(bank.error());
    }
    const audio::Sequence sequence(*song, options.practice.sequence());
    audio::SystemAudio system;
    if (auto opened = system.open(audio::parseOutputDeviceChoice(options.device)); !opened) {
        return fail(opened.error());
    }
    audio::Player player(system.sampleRate());
    player.setSoundBank(*bank);
    player.load(*song, sequence);
    player.seek(options.from);
    const double from = player.position();
    if (looping) {
        player.setLoop(options.loop[0], options.loop[1]);
    }
    const audio::CountIn countIn = options.countIn ? sequence.countInAt(from) : audio::CountIn{};
    player.play(countIn);
    system.setSource(&player);

    // Without --seconds: to the end, or into the loop and twice round it.
    using namespace std::chrono_literals;
    const double loopFrom = looping ? std::clamp(options.loop[0], 0.0, sequence.duration()) : 0.0;
    const double loopTo = looping ? std::clamp(options.loop[1], 0.0, sequence.duration()) : 0.0;
    const bool inLoop = looping && from < loopTo;
    const double length = options.seconds > 0.0 ? options.seconds
                          : inLoop ? std::max(0.0, loopFrom - from) + (2.0 * (loopTo - loopFrom))
                                   : sequence.duration() - from;
    const double counting = countIn.clicks * countIn.interval;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    bool moved = false;
    while (player.isPlaying() && elapsed() < counting + length + 2.0) {
        moved = moved || player.position() != from;
        if (inLoop ? elapsed() >= counting + length : player.position() >= from + length) {
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    const bool finished = inLoop ? moved && elapsed() >= counting + length
                                 : player.position() >= from + length - 0.05;
    player.pause();
    std::this_thread::sleep_for(500ms); // let the last notes ring out
    const double to = player.position();
    system.setSource(nullptr);

    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("file", options.file)
            .field("device", system.deviceDescription())
            .field("from", from)
            .field("to", to)
            .field("finished", finished);
        writePractice(out, sequence);
        out.key("countIn");
        writeCountIn(out, countIn);
        out.key("loop");
        if (looping) {
            out.beginArray().value(loopFrom).value(loopTo).endArray();
        } else {
            out.null();
        }
        out.endObject();
        std::cout << out.text() << '\n';
    } else {
        std::cout << std::format("played {} from {:.3f} s to {:.3f} s on {}\n", options.file, from,
                                 to, system.deviceDescription());
    }
    return finished ? kExitOk
                    : fail("playback did not finish; the audio callback may not be running");
}

struct SoundFontOptions {
    std::string file;
};

int runSoundFont(bool json, const SoundFontOptions& options) {
    const auto font = audio::SoundFont::open(options.file);
    if (!font) {
        return fail(font.error());
    }
    const auto& presets = (*font)->presets();
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("file", options.file)
            .field("presetCount", presets.size())
            .key("presets")
            .beginArray();
        for (const audio::SoundFontPreset& preset : presets) {
            out.beginObject()
                .field("bank", preset.bank)
                .field("program", preset.program)
                .field("name", preset.name)
                .endObject();
        }
        out.endArray().endObject();
        std::cout << out.text() << '\n';
        return kExitOk;
    }
    std::cout << std::format("{}: {} presets\n", options.file, presets.size());
    for (const audio::SoundFontPreset& preset : presets) {
        std::cout << std::format("  {:3}:{:3}  {}\n", preset.bank, preset.program, preset.name);
    }
    return kExitOk;
}

struct SfzOptions {
    std::string file;
};

// A JSON array of strings.
void writeStrings(JsonWriter& json, std::string_view name, const std::vector<std::string>& values) {
    json.key(name).beginArray();
    for (const std::string& value : values) {
        json.value(value);
    }
    json.endArray();
}

// The touches an SFZ library can play itself, and its legato, with its
// control for each.
std::array<std::pair<std::string_view, const audio::SfzTechnique*>, 4>
techniquesOf(const audio::SfzBank& bank) {
    const audio::SfzTechniques& techniques = bank.techniques();
    return {{{audio::touchName(audio::Touch::PalmMute), &techniques.palmMute},
             {audio::touchName(audio::Touch::Dead), &techniques.dead},
             {audio::touchName(audio::Touch::Harmonic), &techniques.harmonic},
             {"legato", &techniques.legato}}};
}

// {"palm-mute": {"keyswitch" or "cc": N, "label": ...} or null, ...}
void writeTechniques(JsonWriter& json, const audio::SfzBank& bank) {
    json.beginObject();
    for (const auto& [touch, technique] : techniquesOf(bank)) {
        json.key(touch);
        if (!technique->found()) {
            json.null();
        } else if (technique->keyswitch >= 0) {
            json.beginObject()
                .field("keyswitch", technique->keyswitch)
                .field("label", technique->label)
                .endObject();
        } else {
            json.beginObject()
                .field("cc", technique->cc)
                .field("label", technique->label)
                .endObject();
        }
    }
    json.endObject();
}

int runSfz(bool json, const SfzOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    const auto bank = audio::SfzBank::open(options.file);
    if (!bank) {
        return fail(bank.error());
    }
    const double loadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const audio::SfzProgram& program = (*bank)->program();
    int lowest = 127;
    int highest = 0;
    for (const audio::SfzRegion& region : program.regions) {
        lowest = std::min(lowest, region.loKey);
        highest = std::max(highest, region.hiKey);
    }
    if (json) {
        JsonWriter out;
        out.beginObject()
            .field("file", options.file)
            .field("name", (*bank)->name())
            .field("regions", program.regions.size())
            .field("samples", (*bank)->sampleCount())
            .field("sampleBytes", (*bank)->sampleBytes())
            .field("lowestKey", lowest)
            .field("highestKey", highest);
        writeStrings(out, "missingSamples", (*bank)->missingSamples());
        writeStrings(out, "unknownOpcodes", program.unknownOpcodes);
        writeStrings(out, "skippedHeaders", program.skippedHeaders);
        out.key("techniques");
        writeTechniques(out, **bank);
        out.field("loadSeconds", loadSeconds).endObject();
        std::cout << out.text() << '\n';
        return kExitOk;
    }
    std::cout << std::format("{}: {} regions over keys {} to {}, {} samples ({:.1f} MB) read in "
                             "{:.2f} s\n",
                             (*bank)->name(), program.regions.size(), lowest, highest,
                             (*bank)->sampleCount(),
                             static_cast<double>((*bank)->sampleBytes()) / 1e6, loadSeconds);
    for (const std::string& missing : (*bank)->missingSamples()) {
        std::cout << std::format("  missing sample: {}\n", missing);
    }
    if (!program.unknownOpcodes.empty()) {
        std::cout << "  opcodes not played:";
        for (const std::string& opcode : program.unknownOpcodes) {
            std::cout << ' ' << opcode;
        }
        std::cout << '\n';
    }
    for (const auto& [touch, technique] : techniquesOf(**bank)) {
        if (technique->keyswitch >= 0) {
            std::cout << std::format("  {}: keyswitch {} \"{}\"\n", touch, technique->keyswitch,
                                     technique->label);
        } else if (technique->cc >= 0) {
            std::cout << std::format("  {}: controller {} \"{}\"\n", touch, technique->cc,
                                     technique->label);
        }
    }
    return kExitOk;
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
    const auto wav = audio::readAudioFile(options.file);
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
        JsonWriter out;
        out.beginObject()
            .field("file", options.file)
            .field("sampleRate", wav->format.sampleRate)
            .field("windows", windows)
            .field("pitched", heard.size())
            .field("a4", options.a4Hz)
            .field("frequency", frequency)
            .field("note", reading->note.toString())
            .field("cents", reading->cents)
            .field("clarity", clarity);
        if (tuning) {
            out.field("tuning", tuning->toString())
                .field("string", reading->string)
                .field("stringPitch", tuning->openString(reading->string).toString())
                .field("stringCents", reading->stringCents);
        }
        out.endObject();
        std::cout << out.text() << '\n';
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
void writeReading(JsonWriter& json, double time, const std::optional<audio::TunerReading>& reading,
                  const std::optional<core::Tuning>& tuning) {
    json.beginObject().field("time", time).key("note");
    if (!reading) {
        json.null().endObject();
        return;
    }
    json.string(reading->note.toString())
        .field("cents", reading->cents)
        .field("frequency", reading->frequency);
    if (tuning) {
        json.field("string", reading->string).field("stringCents", reading->stringCents);
    }
    json.endObject();
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
    JsonWriter readings;
    readings.beginArray();
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
                writeReading(readings, time, reading, tuning);
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
        readings.endArray();
        JsonWriter out;
        out.beginObject()
            .field("device", input.deviceDescription())
            .field("sampleRate", input.sampleRate())
            .field("seconds", options.seconds)
            .field("peak", peak)
            .field("dropped", input.dropped())
            .key("readings")
            .raw(readings.text())
            .endObject();
        std::cout << out.text() << '\n';
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
    addPracticeOptions(*sequence, sequenced->practice);
    sequence->add_option("--count-in-at", sequenced->countInAt,
                         "Show the count-in for playback from this many seconds");
    sequence->callback(
        [&context, sequenced] { context.exitCode = runSequence(context.json, *sequenced); });

    auto rendering = std::make_shared<RenderAudioOptions>();
    auto* renderAudio = app.add_subcommand(
        "render-audio", "Play a song offline through the built-in synthesizer or a SoundFont, "
                        "optionally into a 16-bit stereo WAV file, and report its level and the "
                        "events dispatched");
    renderAudio->add_option("file", rendering->file, "A .elara file or any file import reads")
        ->required();
    renderAudio->add_option("--out", rendering->out, "Output WAV path");
    renderAudio->add_option("--sample-rate", rendering->sampleRate, "Sample rate in Hz")
        ->check(CLI::IsMember({44100, 48000, 96000}))
        ->capture_default_str();
    renderAudio->add_option("--tail", rendering->tail, "Seconds rendered after the last measure")
        ->check(CLI::Range(0.0, 30.0))
        ->capture_default_str();
    renderAudio->add_option("--soundfont", rendering->soundFont,
                            "Play the tracks with this SoundFont (SF2 or SF3)");
    renderAudio->add_option("--sfz", rendering->sfz, kSfzHelp)->type_name("[TRACKS=]FILE");
    renderAudio->add_flag("--events", rendering->events,
                          "List the note events dispatched, with their sample frames");
    addPracticeOptions(*renderAudio, rendering->practice);
    renderAudio->callback(
        [&context, rendering] { context.exitCode = runRenderAudio(context.json, *rendering); });

    auto playing = std::make_shared<PlayOptions>();
    auto* playCommand = app.add_subcommand(
        "play", "Play a song through the built-in synthesizer or a SoundFont on an output device");
    playCommand->add_option("file", playing->file, "A .elara file or any file import reads")
        ->required();
    playCommand->add_option("--from", playing->from, "Start this many seconds into the song")
        ->check(CLI::NonNegativeNumber)
        ->capture_default_str();
    playCommand->add_option("--seconds", playing->seconds, "Stop after this many seconds")
        ->check(CLI::NonNegativeNumber);
    addPracticeOptions(*playCommand, playing->practice);
    playCommand->add_flag("--count-in", playing->countIn,
                          "Click one measure of beats before playback starts");
    playCommand->add_option("--soundfont", playing->soundFont,
                            "Play the tracks with this SoundFont (SF2 or SF3)");
    playCommand->add_option("--sfz", playing->sfz, kSfzHelp)->type_name("[TRACKS=]FILE");
    playCommand->add_option("--loop", playing->loop, "Loop from FROM to TO seconds")
        ->expected(2)
        ->type_name("FROM TO");
    playCommand->add_option("--device", playing->device, kDeviceHelp);
    playCommand->callback(
        [&context, playing] { context.exitCode = runPlay(context.json, *playing); });

    auto font = std::make_shared<SoundFontOptions>();
    auto* soundFont =
        app.add_subcommand("soundfont", "List the presets of a SoundFont (SF2 or SF3)");
    soundFont->add_option("file", font->file, "An .sf2 or .sf3 file")->required();
    soundFont->callback([&context, font] { context.exitCode = runSoundFont(context.json, *font); });

    auto sfz = std::make_shared<SfzOptions>();
    auto* sfzCommand = app.add_subcommand(
        "sfz", "Read an SFZ instrument and its samples, and say what Elara plays of it");
    sfzCommand->add_option("file", sfz->file, "An .sfz file")->required();
    sfzCommand->callback([&context, sfz] { context.exitCode = runSfz(context.json, *sfz); });

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

    auto play = std::make_shared<DeviceToneOptions>();
    auto* tone = app.add_subcommand("tone", "Play a sine test tone on an output device");
    addToneOptions(*tone, play->tone);
    tone->add_option("--device", play->device, kDeviceHelp);
    tone->callback([&context, play] { context.exitCode = runTone(context.json, *play); });

    auto tuned = std::make_shared<TuneOptions>();
    auto* tune = app.add_subcommand(
        "tune",
        "Detect the pitch of a WAV or FLAC recording, as a tuner would: the nearest note, how many "
        "cents off, and with --tuning the nearest open string");
    tune->add_option("file", tuned->file, "A WAV (PCM or float) or FLAC file, any channels")
        ->required();
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
