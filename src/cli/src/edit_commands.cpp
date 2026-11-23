// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing subcommands: edit (a script of undoable edits applied to a new song).
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/validation.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <charconv>
#include <cstddef>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace elara::cli {

namespace {

using core::Document;
using core::Song;

/// A step that is not well formed (as opposed to one the song rejects).
class StepSyntaxError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

std::vector<std::string_view> splitWords(std::string_view text) {
    std::vector<std::string_view> words;
    while (!text.empty()) {
        const auto start = text.find_first_not_of(' ');
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        const auto end = std::min(text.find(' '), text.size());
        words.push_back(text.substr(0, end));
        text.remove_prefix(end);
    }
    return words;
}

class Step {
public:
    explicit Step(std::string_view text) : words_(splitWords(text)) {
        if (words_.empty()) {
            throw StepSyntaxError("empty step");
        }
    }

    [[nodiscard]] std::string_view verb() const { return words_.front(); }
    [[nodiscard]] bool has(std::size_t i) const { return i < words_.size(); }

    [[nodiscard]] std::string_view word(std::size_t i) const {
        if (!has(i)) {
            throw StepSyntaxError(std::format("{}: missing argument {}", verb(), i));
        }
        return words_[i];
    }

    template <typename T>
    [[nodiscard]] T number(std::size_t i) const {
        const std::string_view text = word(i);
        T value{};
        const auto* end = text.data() + text.size();
        const auto [ptr, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc{} || ptr != end) {
            throw StepSyntaxError(std::format("{}: not a number: {}", verb(), text));
        }
        return value;
    }

    [[nodiscard]] std::size_t index(std::size_t i) const { return number<std::size_t>(i); }

    /// Words from `i` to the end, joined by single spaces.
    [[nodiscard]] std::string rest(std::size_t i) const {
        std::string text(word(i));
        for (std::size_t j = i + 1; j < words_.size(); ++j) {
            (text += ' ') += words_[j];
        }
        return text;
    }

    [[nodiscard]] core::Duration duration(std::size_t i) const {
        const auto parsed = core::Duration::parse(word(i));
        if (!parsed) {
            throw StepSyntaxError(std::format("{}: not a duration: {}", verb(), word(i)));
        }
        return *parsed;
    }

private:
    std::vector<std::string_view> words_;
};

// Note-level edits replace the first staff's measure with an edited copy (ADR-0005).
template <typename Change>
std::unique_ptr<core::Command> editMeasure(const Song& song, std::size_t track, std::size_t measure,
                                           Change change) {
    core::Measure content = song.track(track).staff(0).measure(measure);
    change(content);
    return std::make_unique<core::ReplaceMeasure>(track, 0, measure, std::move(content));
}

void runStep(Document& doc, const Step& step) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();

    if (verb == "undo") {
        doc.undo();
    } else if (verb == "redo") {
        doc.redo();
    } else if (verb == "save") {
        doc.markSaved();
    } else if (verb == "add-track") {
        const auto* preset = findInstrumentPreset(step.word(1));
        if (preset == nullptr) {
            throw StepSyntaxError(std::format("unknown instrument preset: {}", step.word(1)));
        }
        const std::size_t at = step.has(2) ? step.index(2) : song.trackCount();
        doc.execute(std::make_unique<InsertTrack>(at, Track::fromPreset(*preset)));
    } else if (verb == "remove-track") {
        doc.execute(std::make_unique<RemoveTrack>(step.index(1)));
    } else if (verb == "insert-measures") {
        const std::size_t count = step.has(2) ? step.index(2) : 1;
        doc.execute(std::make_unique<InsertMeasures>(step.index(1), count));
    } else if (verb == "append-measures") {
        doc.execute(std::make_unique<InsertMeasures>(song.measureCount(), step.index(1)));
    } else if (verb == "remove-measures") {
        const std::size_t count = step.has(2) ? step.index(2) : 1;
        doc.execute(std::make_unique<RemoveMeasures>(step.index(1), count));
    } else if (verb == "time" || verb == "key") {
        const std::size_t measure = step.index(1);
        MeasureHeader header = song.measureHeader(measure);
        if (verb == "time") {
            const auto time = TimeSignature::parse(step.word(2));
            if (!time) {
                throw StepSyntaxError(std::format("not a time signature: {}", step.word(2)));
            }
            header.timeSignature = *time;
        } else {
            const auto key = KeySignature::parse(step.rest(2));
            if (!key) {
                throw StepSyntaxError(std::format("not a key: {}", step.rest(2)));
            }
            header.keySignature = *key;
        }
        doc.execute(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
    } else if (verb == "tempo") {
        doc.execute(std::make_unique<SetStartTempo>(Tempo(step.number<int>(1))));
    } else if (verb == "title") {
        SongInfo info = song.info();
        info.title = step.rest(1);
        doc.execute(std::make_unique<SetSongInfo>(std::move(info)));
    } else if (verb == "note") { // note <track> <measure> <string> <fret> <duration>
        const std::size_t track = step.index(1);
        const Note note =
            song.track(track).staff(0).frettedNote(step.number<int>(3), step.number<int>(4));
        const Duration duration = step.duration(5);
        doc.execute(editMeasure(song, track, step.index(2), [&](Measure& measure) {
            measure.voices[0].beats.push_back(Beat{.duration = duration, .notes = {note}});
        }));
    } else if (verb == "rest") { // rest <track> <measure> <duration>
        const Duration duration = step.duration(3);
        doc.execute(editMeasure(song, step.index(1), step.index(2), [&](Measure& measure) {
            measure.voices[0].beats.push_back(Beat{.duration = duration, .notes = {}});
        }));
    } else if (verb == "fill") { // fill <track> <measure>
        const std::size_t measure = step.index(2);
        const TimeSignature time = song.measureHeader(measure).timeSignature;
        doc.execute(editMeasure(song, step.index(1), measure, [&](Measure& content) {
            if (!fillWithRests(content.voices[0], time)) {
                throw std::runtime_error("the voice cannot be completed with rests");
            }
        }));
    } else {
        throw StepSyntaxError(std::format("unknown step: {}", verb));
    }
}

std::string jsonName(std::string_view name) {
    return name.empty() ? std::string("null") : std::format("\"{}\"", jsonEscape(name));
}

std::string jsonOptional(const std::optional<std::size_t>& value) {
    return value ? std::to_string(*value) : std::string("null");
}

void printJson(const Document& doc) {
    const Song& song = doc.song();
    std::string tracks;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const auto& track = song.track(t);
        std::size_t beats = 0;
        std::size_t notes = 0;
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                for (const auto& voice : track.staff(s).measure(m).voices) {
                    beats += voice.beats.size();
                    for (const auto& beat : voice.beats) {
                        notes += beat.notes.size();
                    }
                }
            }
        }
        tracks +=
            std::format(R"({}{{"name":"{}","staves":{},"beats":{},"notes":{}}})", t == 0 ? "" : ",",
                        jsonEscape(track.name()), track.staffCount(), beats, notes);
    }
    std::string measures;
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const auto& header = song.measureHeader(m);
        measures += std::format(R"({}{{"time":"{}","key":"{}"}})", m == 0 ? "" : ",",
                                header.timeSignature.toString(), header.keySignature.toString());
    }
    std::string issues;
    for (const auto& issue : core::validate(song)) {
        issues += std::format(
            R"({}{{"kind":"{}","track":{},"staff":{},"measure":{},"voice":{},"beat":{},"note":{}}})",
            issues.empty() ? "" : ",", core::issueName(issue.kind), issue.track, issue.staff,
            issue.measure, issue.voice, jsonOptional(issue.beat), jsonOptional(issue.note));
    }
    std::cout
        << std::format(
               R"({{"title":"{}","tempo":{},"tracks":[{}],"measureCount":{},"measures":[{}],"issues":[{}],"undo":{{"count":{},"next":{}}},"redo":{{"count":{},"next":{}}},"modified":{}}})",
               jsonEscape(song.info().title), song.startTempo().bpm(), tracks, song.measureCount(),
               measures, issues, doc.undoCount(), jsonName(doc.undoName()), doc.redoCount(),
               jsonName(doc.redoName()), doc.isModified())
        << '\n';
}

void printText(const Document& doc) {
    const Song& song = doc.song();
    std::cout << std::format("{}: {} track(s), {} measure(s), {} bpm\n",
                             song.info().title.empty() ? "Untitled" : song.info().title,
                             song.trackCount(), song.measureCount(), song.startTempo().bpm());
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        std::cout << std::format("  track {}: {}\n", t, song.track(t).name());
    }
    std::cout << std::format("undo: {} ({} step(s)), redo: {} ({} step(s)){}\n",
                             doc.canUndo() ? doc.undoName() : "-", doc.undoCount(),
                             doc.canRedo() ? doc.redoName() : "-", doc.redoCount(),
                             doc.isModified() ? ", modified" : "");
    const auto issues = core::validate(song);
    for (const auto& issue : issues) {
        std::cout << std::format("  issue: {} (track {}, measure {}, voice {})\n",
                                 core::issueName(issue.kind), issue.track, issue.measure,
                                 issue.voice);
    }
}

int runEdit(bool json, const std::vector<std::string>& steps) {
    Document doc;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        try {
            runStep(doc, Step(steps[i]));
        } catch (const StepSyntaxError& error) {
            return usageError(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        } catch (const std::exception& error) {
            return fail(std::format("step {} ({}): {}", i + 1, steps[i], error.what()));
        }
    }
    if (json) {
        printJson(doc);
    } else {
        printText(doc);
    }
    return kExitOk;
}

} // namespace

void addEditCommands(CLI::App& app, Context& context) {
    auto steps = std::make_shared<std::vector<std::string>>();
    auto* editCommand = app.add_subcommand(
        "edit", "Apply undoable edits to a new song and print the result and its history");
    editCommand
        ->add_option("steps", *steps,
                     "Steps in order, each quoted: \"add-track <preset> [at]\", "
                     "\"remove-track <i>\", \"insert-measures <at> [n]\", \"append-measures <n>\", "
                     "\"remove-measures <at> [n]\", \"time <m> <N/D>\", \"key <m> <key>\", "
                     "\"tempo <bpm>\", \"title <text>\", \"note <track> <m> <string> <fret> "
                     "<duration>\", \"rest <track> <m> <duration>\", \"fill <track> <m>\", "
                     "undo, redo, save")
        ->required();
    editCommand->callback([&context, steps] { context.exitCode = runEdit(context.json, *steps); });
}

} // namespace elara::cli
