// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing subcommands: edit (a script of undoable edits applied to a new song or
// a .elara file).
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/parallel.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "edit_steps.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using core::Document;
using core::Song;

// `{"count":2,"next":"Add a note"}`, with "next" null when there is none.
void writeHistory(JsonWriter& json, std::size_t count, std::string_view next) {
    json.beginObject().field("count", count).key("next");
    if (next.empty()) {
        json.null();
    } else {
        json.string(next);
    }
    json.endObject();
}

void writeCaret(JsonWriter& json, const Song& song, const core::Caret& caret) {
    if (!hasCaret(song)) {
        json.null();
        return;
    }
    json.beginObject()
        .field("track", caret.track)
        .field("staff", caret.staff)
        .field("measure", caret.measure)
        .field("voice", caret.voice)
        .field("beat", caret.beat)
        .field("string", caret.string)
        .field("freeSlot", core::onFreeSlot(song, caret))
        .endObject();
}

void printJson(const Document& doc, const core::EditSession& session) {
    const Song& song = doc.song();
    JsonWriter out;
    out.beginObject()
        .field("title", song.info().title)
        .field("tempo", song.startTempo().bpm())
        .key("tracks");
    writeTracks(out, song);
    out.field("measureCount", song.measureCount()).key("measures");
    writeMeasures(out, song);
    out.key("issues");
    writeIssues(out, core::validate(song));
    out.key("caret");
    writeCaret(out, song, session.caret());
    out.field("input", session.input().toString()).key("undo");
    writeHistory(out, doc.undoCount(), doc.undoName());
    out.key("redo");
    writeHistory(out, doc.redoCount(), doc.redoName());
    out.field("historyBytes", doc.historyBytes()).field("modified", doc.isModified()).endObject();
    std::cout << out.text() << '\n';
}

void printText(const Document& doc, const core::EditSession& session) {
    printSongText(doc.song());
    if (const core::Caret& caret = session.caret(); hasCaret(doc.song())) {
        std::cout << std::format(
            "caret: track {}, staff {}, measure {}, voice {}, beat {}{}, string {}; input {}\n",
            caret.track, caret.staff, caret.measure, caret.voice, caret.beat,
            core::onFreeSlot(doc.song(), caret) ? " (free slot)" : "", caret.string,
            session.input().toString());
    }
    std::cout << std::format("undo: {} ({} step(s)), redo: {} ({} step(s)){}\n",
                             doc.canUndo() ? doc.undoName() : "-", doc.undoCount(),
                             doc.canRedo() ? doc.redoName() : "-", doc.redoCount(),
                             doc.isModified() ? ", modified" : "");
}

struct EditOptions {
    std::string in;                // start from this file instead of a new song
    std::string out;               // save the result here
    std::vector<std::string> from; // songs open beside it, that copy steps copy from
    std::vector<std::string> steps;
};

/// Opens the songs open beside the one edited, all at once on the shared
/// workers (ADR-0160): what opening one says is lost goes to stderr. Returns
/// the message of the first that does not open.
std::expected<std::vector<Song>, std::string> openSources(const std::vector<std::string>& files) {
    std::vector<std::optional<std::expected<io::ImportResult, io::FormatError>>> opened(
        files.size());
    core::forEachIndex(0, files.size(), core::threadsFor(0, files.size(), 2, files.size()),
                       [&](std::size_t i) { opened[i] = io::openNative(files[i]); });
    std::vector<Song> songs;
    songs.reserve(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        auto& result = *opened[i];
        if (!result) {
            return std::unexpected(std::format("{}: {}", files[i], result.error().message));
        }
        for (const std::string& warning : result->warnings) {
            std::cerr << std::format("warning: {}: {}\n", files[i], warning);
        }
        songs.push_back(std::move(result->song));
    }
    return songs;
}

int runEdit(bool json, const EditOptions& options) {
    Document doc;
    if (!options.in.empty()) {
        auto loaded = io::openNative(options.in);
        if (!loaded) {
            return fail(std::format("{}: {}", options.in, loaded.error().message));
        }
        // On stderr, which leaves --json's output alone: what a newer Elara
        // added is lost when the song is saved here (ADR-0139).
        for (const std::string& warning : loaded->warnings) {
            std::cerr << std::format("warning: {}: {}\n", options.in, warning);
        }
        doc = Document(std::move(loaded->song));
    }
    const auto& steps = options.steps;
    StepState state;
    auto sources = openSources(options.from);
    if (!sources) {
        return fail(sources.error());
    }
    state.sources = std::move(*sources);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        try {
            runStep(doc, steps[i], state);
        } catch (const StepSyntaxError& error) {
            return usageError(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        } catch (const std::exception& error) {
            return fail(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        }
    }
    if (!options.out.empty()) {
        try {
            io::saveNative(doc.song(), options.out);
        } catch (const std::exception& error) {
            return fail(error.what());
        }
        doc.markSaved();
    }
    if (json) {
        printJson(doc, state.session);
    } else {
        printText(doc, state.session);
    }
    return kExitOk;
}

} // namespace

void addEditCommands(CLI::App& app, Context& context) {
    auto options = std::make_shared<EditOptions>();
    auto* editCommand = app.add_subcommand(
        "edit", "Apply undoable edits to a song and print the result and its history");
    editCommand->add_option("--in", options->in, "Start from this .elara file (default: new song)");
    editCommand->add_option("--out", options->out, "Save the result to this .elara file");
    // One file each time it is given, so that the steps after it stay steps.
    editCommand
        ->add_option("--from", options->from,
                     "Another .elara file open beside it, which \"copy ... from <n>\" "
                     "copies from: the first is song 1 (repeat for more)")
        ->allow_extra_args(false);
    editCommand->add_option(
        "steps", options->steps,
        "Steps in order, each quoted: \"add-track <preset> [at]\", "
        "\"remove-track <i>\", \"rename-track <i> [name]\", \"insert-measures <at> [n]\", "
        "\"append-measures <n>\", "
        "\"remove-measures <at> [n]\", \"time <m> <N/D>\", \"key <m> <key>\", "
        "\"mix <track> <volume 0..1> <pan -1..1> [mute] [solo]\", "
        "\"tempo <bpm>\", \"tempo-from <m> <bpm|none>\", \"tempo-gradual <m> <on|off>\", "
        "\"section <m> [name]\", "
        "\"title <text>\", "
        "\"credit <title|subtitle|artist|album|words|music|copyright|transcriber|notes> "
        "[text]\", "
        "\"note <track> <m> <string> <fret> "
        "<duration>\", \"rest <track> <m> <duration>\", \"fill <track> <m>\", "
        "\"copy <m> <n> [track <t> [<count>]] [from <song>]\", \"paste <m> [track <t>] [insert]\", "
        "\"transpose <semitones> [measures <m> <n>] [tracks <t>...]\", "
        "\"caret <track> <m> <beat> [string]\", \"voice <1-4>\", right, left, up, down, "
        "next-measure, "
        "previous-measure, \"fret <n>\", \"pitch <C..B> [add]\", \"shift <semitones>\", "
        "\"duration <d>\", delete, delete-beat, "
        "insert-rest, \"drum [key]\", \"effect <name>\", \"bend <form> [cents]\", \"strum "
        "<down|up|none>\", "
        "\"pick <down|up|none>\", \"tremolo <8|16|32|none>\", "
        "\"whammy <dive|dip|pre-dive|return|none> [cents]\", \"bass <tap|slap|pop|none>\", "
        "\"hairpin <crescendo|diminuendo|none>\", \"fermata <short|normal|long|none>\", "
        "\"slur <on|none>\", \"ottava <8va|8vb|15ma|15mb|none>\", \"pedal <down|change|none>\", "
        "\"breath <comma|caesura|none>\", "
        "\"chord <name|none>\", \"text <words|none>\", "
        "\"slide-in "
        "<from-below|from-above|none>\", \"slide-out <shift|legato|downwards|upwards|none>\", "
        "\"vibrato <slight|wide|none>\", \"accent <accent|marcato|none>\", "
        "\"trill <on|semitones|none> [16|32|64]\", "
        "\"ornament <mordent|upper-mordent|turn|inverted-turn|none>\", "
        "\"fingering <1-5|t|none>...\", \"pluck <p|i|m|a|c|none>\", \"harmonic "
        "<natural|artificial|pinch|tapped|semi|none>\", undo, redo, "
        "save");
    editCommand->callback(
        [&context, options] { context.exitCode = runEdit(context.json, *options); });
}

} // namespace elara::cli
