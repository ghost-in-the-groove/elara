// SPDX-License-Identifier: AGPL-3.0-or-later
// File subcommands: new (create a .elara song), dump (print one) and import
// (read a foreign format, optionally saving it as .elara).
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
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

struct ImportOptions {
    std::string file;
    std::string out;
    bool force = false;
};

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

int runImport(bool json, const ImportOptions& options) {
    const std::filesystem::path path(options.file);
    const std::string extension = lowercase(displayPath(path.extension()));
    if (extension != ".musicxml" && extension != ".xml" && extension != ".mxl") {
        return usageError(std::format(
            "cannot import {}: supported files are .musicxml, .xml and .mxl", options.file));
    }
    const auto imported = io::importMusicXmlFile(path);
    if (!imported) {
        return fail(std::format("{}: {}", options.file, imported.error().message));
    }
    const core::Song& song = imported->song;
    if (!options.out.empty()) {
        if (!options.force && std::filesystem::exists(options.out)) {
            return fail(std::format("{} already exists (use --force to replace it)", options.out));
        }
        try {
            io::saveNative(song, options.out);
        } catch (const std::exception& error) {
            return fail(error.what());
        }
    }
    if (json) {
        std::string warnings = "[";
        for (const std::string& warning : imported->warnings) {
            warnings +=
                std::format("{}\"{}\"", warnings.size() > 1 ? "," : "", jsonEscape(warning));
        }
        warnings += ']';
        const std::string out =
            options.out.empty() ? "null" : std::format("\"{}\"", jsonEscape(options.out));
        std::cout << std::format(R"({{"file":"{}","format":"musicxml","out":{},"warnings":{},)"
                                 R"("song":{},"durationSeconds":{:.3f},"issues":{}}})",
                                 jsonEscape(options.file), out, warnings, io::songToJson(song),
                                 song.durationSeconds(), issuesJson(core::validate(song)))
                  << '\n';
    } else {
        std::cout << std::format("imported {} (MusicXML)\n", options.file);
        printSongText(song);
        for (const std::string& warning : imported->warnings) {
            std::cout << "warning: " << warning << '\n';
        }
        if (!options.out.empty()) {
            std::cout << std::format("saved {}\n", options.out);
        }
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

    auto imported = std::make_shared<ImportOptions>();
    auto* importCommand = app.add_subcommand(
        "import", "Read a MusicXML file (.musicxml, .xml, .mxl): with --json, the song, what "
                  "was dropped (warnings) and issues");
    importCommand->add_option("file", imported->file, "File to import")->required();
    importCommand->add_option("--out", imported->out, "Also save the song as a .elara file");
    importCommand->add_flag("--force", imported->force, "Replace the --out file if it exists");
    importCommand->callback(
        [&context, imported] { context.exitCode = runImport(context.json, *imported); });
}

} // namespace elara::cli
