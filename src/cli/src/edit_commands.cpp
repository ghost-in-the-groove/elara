// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing subcommands: edit (a script of undoable edits applied to a new song or
// a .elara file).
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "edit_steps.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <cstddef>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using core::Document;
using core::Song;

std::string jsonName(std::string_view name) {
    return name.empty() ? std::string("null") : std::format("\"{}\"", jsonEscape(name));
}

std::string caretJson(const Song& song, const core::Caret& caret) {
    if (!hasCaret(song)) {
        return "null";
    }
    return std::format(
        R"({{"track":{},"staff":{},"measure":{},"voice":{},"beat":{},"string":{},"freeSlot":{}}})",
        caret.track, caret.staff, caret.measure, caret.voice, caret.beat, caret.string,
        core::onFreeSlot(song, caret));
}

void printJson(const Document& doc, const EntryState& entry) {
    const Song& song = doc.song();
    std::cout
        << std::format(
               R"({{"title":"{}","tempo":{},"tracks":{},"measureCount":{},"measures":{},"issues":{},"caret":{},"input":"{}","undo":{{"count":{},"next":{}}},"redo":{{"count":{},"next":{}}},"historyBytes":{},"modified":{}}})",
               jsonEscape(song.info().title), song.startTempo().bpm(), tracksJson(song),
               song.measureCount(), measuresJson(song), issuesJson(core::validate(song)),
               caretJson(song, entry.caret), entry.input.toString(), doc.undoCount(),
               jsonName(doc.undoName()), doc.redoCount(), jsonName(doc.redoName()),
               doc.historyBytes(), doc.isModified())
        << '\n';
}

void printText(const Document& doc, const EntryState& entry) {
    printSongText(doc.song());
    if (const core::Caret& caret = entry.caret; hasCaret(doc.song())) {
        std::cout << std::format(
            "caret: track {}, staff {}, measure {}, voice {}, beat {}{}, string {}; input {}\n",
            caret.track, caret.staff, caret.measure, caret.voice, caret.beat,
            core::onFreeSlot(doc.song(), caret) ? " (free slot)" : "", caret.string,
            entry.input.toString());
    }
    std::cout << std::format("undo: {} ({} step(s)), redo: {} ({} step(s)){}\n",
                             doc.canUndo() ? doc.undoName() : "-", doc.undoCount(),
                             doc.canRedo() ? doc.redoName() : "-", doc.redoCount(),
                             doc.isModified() ? ", modified" : "");
}

struct EditOptions {
    std::string in;  // start from this file instead of a new song
    std::string out; // save the result here
    std::vector<std::string> steps;
};

int runEdit(bool json, const EditOptions& options) {
    Document doc;
    if (!options.in.empty()) {
        auto loaded = io::loadNative(options.in);
        if (!loaded) {
            return fail(std::format("{}: {}", options.in, loaded.error().message));
        }
        doc = Document(std::move(*loaded));
    }
    const auto& steps = options.steps;
    StepState state;
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
        printJson(doc, state.entry);
    } else {
        printText(doc, state.entry);
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
    editCommand->add_option(
        "steps", options->steps,
        "Steps in order, each quoted: \"add-track <preset> [at]\", "
        "\"remove-track <i>\", \"insert-measures <at> [n]\", \"append-measures <n>\", "
        "\"remove-measures <at> [n]\", \"time <m> <N/D>\", \"key <m> <key>\", "
        "\"mix <track> <volume 0..1> <pan -1..1> [mute] [solo]\", "
        "\"tempo <bpm>\", \"tempo-from <m> <bpm|none>\", \"tempo-gradual <m> <on|off>\", "
        "\"section <m> [name]\", "
        "\"title <text>\", "
        "\"credit <title|subtitle|artist|album|words|music|copyright|transcriber|notes> "
        "[text]\", "
        "\"note <track> <m> <string> <fret> "
        "<duration>\", \"rest <track> <m> <duration>\", \"fill <track> <m>\", "
        "\"copy <m> <n> [track <t> [<count>]]\", \"paste <m> [track <t>] [insert]\", "
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
