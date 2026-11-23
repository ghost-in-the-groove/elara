// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing subcommands: edit (a script of undoable edits applied to a new song or
// a .elara file).
#include <elara/core/clipboard.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/transpose.hpp>
#include <elara/core/validation.hpp>
#include <elara/io/native_format.hpp>

#include "common.hpp"
#include "song_report.hpp"
#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
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
        std::string_view text = word(i);
        if (text.size() > 1 && text.front() == '+') {
            text.remove_prefix(1); // "+2" as well as "2"
        }
        T value{};
        const auto* end = text.data() + text.size();
        const auto [ptr, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc{} || ptr != end) {
            throw StepSyntaxError(std::format("{}: not a number: {}", verb(), text));
        }
        return value;
    }

    [[nodiscard]] std::size_t index(std::size_t i) const { return number<std::size_t>(i); }

    /// A decimal such as "0.75" or "-1" (strtod: from_chars for floating point
    /// is not on every toolchain yet).
    [[nodiscard]] double decimal(std::size_t i) const {
        const std::string text(word(i));
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size()) {
            throw StepSyntaxError(std::format("{}: not a number: {}", verb(), text));
        }
        return value;
    }

    /// Words from `i` to the end, joined by single spaces.
    [[nodiscard]] std::string rest(std::size_t i) const {
        std::string text(word(i));
        for (std::size_t j = i + 1; j < words_.size(); ++j) {
            (text += ' ') += words_[j];
        }
        return text;
    }

    /// A note name from C to B.
    [[nodiscard]] core::Step noteName(std::size_t i) const {
        static constexpr std::string_view kNames = "CDEFGAB";
        const std::string_view text = word(i);
        const auto found = text.size() == 1 ? kNames.find(text.front()) : std::string_view::npos;
        if (found == std::string_view::npos) {
            throw StepSyntaxError(std::format("{}: not a note name (C to B): {}", verb(), text));
        }
        return static_cast<core::Step>(found);
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

/// Keyboard entry across steps (ADR-0021): the caret and the input duration.
struct EntryState {
    core::Caret caret{};
    core::Duration input{core::NoteValue::Quarter};
};

// The names `effect <name>` takes (ADR-0037).
constexpr std::array<std::pair<std::string_view, core::NoteEffect>, 12> kEffectNames{{
    {"tie", core::NoteEffect::Tie},
    {"legato", core::NoteEffect::Legato},
    {"slide", core::NoteEffect::Slide},
    {"bend", core::NoteEffect::Bend},
    {"vibrato", core::NoteEffect::Vibrato},
    {"palm-mute", core::NoteEffect::PalmMute},
    {"dead-note", core::NoteEffect::DeadNote},
    {"let-ring", core::NoteEffect::LetRing},
    {"harmonic", core::NoteEffect::Harmonic},
    {"ghost", core::NoteEffect::Ghost},
    {"accent", core::NoteEffect::Accent},
    {"staccato", core::NoteEffect::Staccato},
}};

/// Runs a keyboard entry step; false when `step` is not one.
bool runEntryStep(Document& doc, const Step& step, EntryState& entry) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();
    static constexpr std::array<std::pair<std::string_view, CaretMove>, 6> kMoves{{
        {"right", CaretMove::NextBeat},
        {"left", CaretMove::PreviousBeat},
        {"next-measure", CaretMove::NextMeasure},
        {"previous-measure", CaretMove::PreviousMeasure},
        {"up", CaretMove::StringUp},
        {"down", CaretMove::StringDown},
    }};
    for (const auto& [name, move] : kMoves) {
        if (verb == name) {
            entry.caret = moveCaret(song, entry.caret, move);
            return true;
        }
    }
    Entry edit;
    if (verb == "caret") { // caret <track> <measure> <beat> [string]
        const Caret wanted{.track = step.index(1),
                           .measure = step.index(2),
                           .beat = step.index(3),
                           .string = step.has(4) ? step.number<int>(4) : 1};
        if (clampCaret(song, wanted) != wanted) {
            throw std::out_of_range("the song has no such caret position");
        }
        entry.caret = wanted;
        return true;
    } else if (verb == "fret") {
        edit = enterFret(song, entry.caret, step.number<int>(1), entry.input);
    } else if (verb == "pitch") { // pitch <C..B> [add]
        if (step.has(2) && step.word(2) != "add") {
            throw StepSyntaxError(std::format("pitch: expected add, got {}", step.word(2)));
        }
        edit = enterPitch(song, entry.caret, step.noteName(1), entry.input,
                          step.has(2) ? PitchMode::AddToChord : PitchMode::Replace);
    } else if (verb == "shift") { // shift <semitones>
        edit = shiftPitch(song, entry.caret, step.number<int>(1));
    } else if (verb == "duration") {
        entry.input = step.duration(1);
        edit = setBeatDuration(song, entry.caret, entry.input);
    } else if (verb == "delete") {
        edit = deleteAtCaret(song, entry.caret);
    } else if (verb == "delete-beat") {
        edit = deleteBeat(song, entry.caret);
    } else if (verb == "insert-rest") {
        edit = insertRest(song, entry.caret, entry.input);
    } else if (verb == "drum") { // drum [key]: the caret's lane, or that piece's
        if (step.has(1)) {
            const int key = step.number<int>(1);
            const auto pieces = drumPieces();
            const auto found = std::ranges::find(pieces, key, &DrumPiece::key);
            if (found == pieces.end()) {
                throw std::out_of_range(std::format("drum: no lane for key {}", key));
            }
            entry.caret.string = static_cast<int>(found - pieces.begin()) + 1;
        }
        edit = toggleDrum(song, entry.caret, entry.input);
    } else if (verb == "effect") { // effect <name>: toggled on the caret's notes
        const std::string_view name = step.word(1);
        const auto found = std::ranges::find_if(
            kEffectNames, [name](const auto& effect) { return effect.first == name; });
        if (found == kEffectNames.end()) {
            throw StepSyntaxError(std::format("not an effect: {}", name));
        }
        edit = toggleEffect(song, entry.caret, found->second);
    } else {
        return false;
    }
    if (edit.command) {
        doc.execute(std::move(edit.command));
    }
    entry.caret = edit.caret;
    return true;
}

// `clipboard` holds what the last copy step copied.
void runStep(Document& doc, const Step& step, core::MeasureClip& clipboard, EntryState& entry) {
    using namespace core;
    const Song& song = doc.song();
    const std::string_view verb = step.verb();

    if (runEntryStep(doc, step, entry)) {
        return;
    }
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
    } else if (verb == "mix") { // mix <track> <volume> <pan> [mute] [solo]
        core::TrackMix mix{.volume = step.decimal(2), .pan = step.decimal(3)};
        for (std::size_t i = 4; step.has(i); ++i) {
            if (step.word(i) == "mute") {
                mix.mute = true;
            } else if (step.word(i) == "solo") {
                mix.solo = true;
            } else {
                throw StepSyntaxError(std::format("mix: not mute or solo: {}", step.word(i)));
            }
        }
        doc.execute(std::make_unique<core::SetTrackMix>(step.index(1), mix));
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
    } else if (verb == "copy") { // copy <m> <n> [track <t> [<count>]]
        std::size_t track = 0;
        std::size_t tracks = 0; // every track
        if (step.has(3)) {
            if (step.word(3) != "track") {
                throw StepSyntaxError(std::format("copy: expected track: {}", step.word(3)));
            }
            track = step.index(4);
            tracks = step.has(5) ? step.index(5) : 1;
        }
        clipboard = copyMeasures(song, step.index(1), step.index(2), track, tracks);
    } else if (verb == "paste") { // paste <m> [track <t>] [insert]
        std::size_t track = 0;
        PasteMode mode = PasteMode::Replace;
        for (std::size_t i = 2; step.has(i); ++i) {
            if (step.word(i) == "track") {
                track = step.index(++i);
            } else if (step.word(i) == "insert") {
                mode = PasteMode::Insert;
            } else {
                throw StepSyntaxError(
                    std::format("paste: expected track or insert: {}", step.word(i)));
            }
        }
        doc.execute(makePaste(song, clipboard, step.index(1), track, mode));
    } else if (verb == "transpose") { // transpose <semitones> [measures <m> <n>] [tracks <t>...]
        Transposition what{.semitones = step.number<int>(1)};
        for (std::size_t i = 2; step.has(i);) {
            if (step.word(i) == "measures") {
                what.first = step.index(i + 1);
                what.count = step.index(i + 2);
                i += 3;
            } else if (step.word(i) == "tracks") {
                for (++i; step.has(i) && step.word(i) != "measures"; ++i) {
                    what.tracks.push_back(step.index(i));
                }
                if (what.tracks.empty()) {
                    throw StepSyntaxError("transpose: tracks needs at least one track");
                }
            } else {
                throw StepSyntaxError(
                    std::format("transpose: expected measures or tracks: {}", step.word(i)));
            }
        }
        doc.execute(makeTranspose(song, what));
    } else {
        throw StepSyntaxError(std::format("unknown step: {}", verb));
    }
}

std::string jsonName(std::string_view name) {
    return name.empty() ? std::string("null") : std::format("\"{}\"", jsonEscape(name));
}

bool hasCaret(const Song& song) {
    return song.trackCount() > 0 && song.measureCount() > 0;
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
               R"({{"title":"{}","tempo":{},"tracks":{},"measureCount":{},"measures":{},"issues":{},"caret":{},"input":"{}","undo":{{"count":{},"next":{}}},"redo":{{"count":{},"next":{}}},"modified":{}}})",
               jsonEscape(song.info().title), song.startTempo().bpm(), tracksJson(song),
               song.measureCount(), measuresJson(song), issuesJson(core::validate(song)),
               caretJson(song, entry.caret), entry.input.toString(), doc.undoCount(),
               jsonName(doc.undoName()), doc.redoCount(), jsonName(doc.redoName()),
               doc.isModified())
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
    core::MeasureClip clipboard;
    EntryState entry;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        try {
            runStep(doc, Step(steps[i]), clipboard, entry);
            // Undo, removals and other edits can take the caret's beat away.
            if (hasCaret(doc.song())) {
                entry.caret = core::clampCaret(doc.song(), entry.caret);
            }
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
        printJson(doc, entry);
    } else {
        printText(doc, entry);
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
        "\"tempo <bpm>\", \"title <text>\", \"note <track> <m> <string> <fret> "
        "<duration>\", \"rest <track> <m> <duration>\", \"fill <track> <m>\", "
        "\"copy <m> <n> [track <t> [<count>]]\", \"paste <m> [track <t>] [insert]\", "
        "\"transpose <semitones> [measures <m> <n>] [tracks <t>...]\", "
        "\"caret <track> <m> <beat> [string]\", right, left, up, down, next-measure, "
        "previous-measure, \"fret <n>\", \"pitch <C..B> [add]\", \"shift <semitones>\", "
        "\"duration <d>\", delete, delete-beat, "
        "insert-rest, \"drum [key]\", \"effect <name>\", undo, redo, save");
    editCommand->callback(
        [&context, options] { context.exitCode = runEdit(context.json, *options); });
}

} // namespace elara::cli
