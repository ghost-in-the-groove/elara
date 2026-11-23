// SPDX-License-Identifier: AGPL-3.0-or-later
// File subcommands: new (create a .elara song) and dump (print one).
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace elara::cli {

namespace {

struct NewOptions {
    std::string file;
    std::string title;
    std::vector<std::string> tracks;
    std::size_t measures = 4;
    std::string time = "4/4";
    std::string key = "C major";
    int tempo = 120;
    bool force = false;
};

int runNew(bool json, const NewOptions& options) {
    const auto time = core::TimeSignature::parse(options.time);
    if (!time) {
        return usageError(std::format("not a time signature: {}", options.time));
    }
    const auto key = core::KeySignature::parse(options.key);
    if (!key) {
        return usageError(std::format("not a key: {}", options.key));
    }
    core::Song song;
    song.info().title = options.title;
    song.setStartTempo(core::Tempo(options.tempo));
    for (const auto& id : options.tracks) {
        const auto* preset = core::findInstrumentPreset(id);
        if (preset == nullptr) {
            return usageError(std::format("unknown instrument preset: {}", id));
        }
        song.addTrack(core::Track::fromPreset(*preset));
    }
    song.appendMeasures(options.measures);
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        song.measureHeader(m).timeSignature = *time;
        song.measureHeader(m).keySignature = *key;
    }

    if (!options.force && std::filesystem::exists(options.file)) {
        return fail(std::format("{} already exists (use --force to replace it)", options.file));
    }
    try {
        io::saveNative(song, options.file);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    if (json) {
        std::cout << std::format(R"({{"file":"{}","title":"{}","tracks":{},"measures":{}}})",
                                 jsonEscape(options.file), jsonEscape(song.info().title),
                                 song.trackCount(), song.measureCount())
                  << '\n';
    } else {
        std::cout << std::format("created {}: {} track(s), {} measure(s)\n", options.file,
                                 song.trackCount(), song.measureCount());
    }
    return kExitOk;
}

int runDump(bool json, const std::string& file) {
    const auto song = io::loadNative(file);
    if (!song) {
        return fail(std::format("{}: {}", file, song.error().message));
    }
    if (json) {
        std::cout << std::format(
                         R"({{"file":"{}","song":{},"durationSeconds":{:.3f},"issues":{}}})",
                         jsonEscape(file), io::songToJson(*song), song->durationSeconds(),
                         issuesJson(core::validate(*song)))
                  << '\n';
    } else {
        std::cout << file << '\n';
        printSongText(*song);
    }
    return kExitOk;
}

} // namespace

void addFileCommands(CLI::App& app, Context& context) {
    auto created = std::make_shared<NewOptions>();
    auto* newCommand = app.add_subcommand("new", "Create a .elara song");
    newCommand->add_option("file", created->file, "File to create, e.g. song.elara")->required();
    newCommand->add_option("--title", created->title, "Song title");
    newCommand->add_option("--track", created->tracks,
                           "Instrument preset for a new track (repeatable), e.g. electric-guitar");
    newCommand->add_option("--measures", created->measures, "Number of measures")
        ->check(CLI::Range(0, 10'000))
        ->capture_default_str();
    newCommand->add_option("--time", created->time, "Time signature")->capture_default_str();
    newCommand->add_option("--key", created->key, "Key signature")->capture_default_str();
    newCommand->add_option("--tempo", created->tempo, "Tempo in quarter notes per minute")
        ->check(CLI::Range(static_cast<int>(core::Tempo::kMinBpm),
                           static_cast<int>(core::Tempo::kMaxBpm)))
        ->capture_default_str();
    newCommand->add_flag("--force", created->force, "Replace the file if it exists");
    newCommand->callback(
        [&context, created] { context.exitCode = runNew(context.json, *created); });

    auto dumped = std::make_shared<std::string>();
    auto* dumpCommand = app.add_subcommand(
        "dump", "Print a .elara song: with --json, its complete model, length and issues");
    dumpCommand->add_option("file", *dumped, "A .elara file")->required();
    dumpCommand->callback(
        [&context, dumped] { context.exitCode = runDump(context.json, *dumped); });
}

} // namespace elara::cli
