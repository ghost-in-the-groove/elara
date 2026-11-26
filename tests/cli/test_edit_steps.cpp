// SPDX-License-Identifier: AGPL-3.0-or-later
// The steps of `elara-cli edit` run against a song: what each does to it, and
// whether a step it cannot take is malformed (a StepSyntaxError, exit 2) or
// rejected by the song (any other exception, exit 1).
#include <elara/core/document.hpp>
#include <elara/core/song.hpp>

#include "edit_steps.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <cstddef>
#include <exception>
#include <initializer_list>
#include <string_view>

using Catch::Matchers::Message;
using elara::cli::runStep;
using elara::cli::StepState;
using elara::cli::StepSyntaxError;
using elara::core::BeatEffects;
using elara::core::Document;
using elara::core::Hairpin;
using elara::core::JumpThen;
using elara::core::JumpTo;
using elara::core::Song;
using elara::core::Strum;

namespace {

/// A document and what its steps carry from one to the next.
struct Run {
    Document doc;
    StepState state;
};

/// `steps` run in order on a new song.
Run runSteps(std::initializer_list<std::string_view> steps) {
    Run run;
    for (const std::string_view step : steps) {
        runStep(run.doc, step, run.state);
    }
    return run;
}

/// How a step fails.
enum class Failure { None, Syntax, Rejected };

Failure failureOf(Run& run, std::string_view step) {
    try {
        runStep(run.doc, step, run.state);
    } catch (const StepSyntaxError&) {
        return Failure::Syntax;
    } catch (const std::exception&) {
        return Failure::Rejected;
    }
    return Failure::None;
}

} // namespace

TEST_CASE("Edit steps: repeats, endings and barlines change the measure's header", "[cli][edit]") {
    const Run base = runSteps({"add-track electric-guitar", "append-measures 3"});
    Run run = runSteps({"add-track electric-guitar", "append-measures 3", "repeat 0 start",
                        "repeat 1 end 3", "ending 1 1 2", "barline 2 double"});
    Song expected = base.doc.song();
    expected.measureHeader(0).repeatStart = true;
    expected.measureHeader(1).repeatTimes = 3;
    expected.measureHeader(1).alternateEndings = 0b011;
    expected.measureHeader(2).doubleBar = true;
    CHECK(run.doc.song() == expected);
    CHECK(run.doc.undoName() == "Change barline");
    runStep(run.doc, "undo", run.state);
    CHECK(run.doc.undoName() == "Change repeats");

    // A step that changes nothing adds no undo step.
    const std::size_t steps = run.doc.undoCount();
    runStep(run.doc, "ending 1 2 1", run.state);
    CHECK(run.doc.undoCount() == steps);
}

TEST_CASE("Edit steps: a jump step names the whole jump", "[cli][edit]") {
    Run run = runSteps({"add-track electric-guitar", "append-measures 3", "sign 0 segno",
                        "jump 2 dal-segno al-coda"});
    CHECK(run.doc.song().measureHeader(0).segno);
    CHECK(run.doc.song().measureHeader(2).jumpThen == JumpThen::Coda);
    CHECK(run.doc.undoName() == "Change navigation marks");
    // Without al-... the music plays to the end; the signs stay.
    runStep(run.doc, "jump 2 da-capo", run.state);
    CHECK(run.doc.song().measureHeader(2).jump == JumpTo::Start);
    CHECK(run.doc.song().measureHeader(2).jumpThen == JumpThen::End);
    CHECK(run.doc.song().measureHeader(0).segno);
}

TEST_CASE("Edit steps: a beat effect step changes that effect only", "[cli][edit]") {
    const Run run = runSteps({"add-track electric-guitar", "append-measures 1", "caret 0 0 0",
                              "fret 3", "strum down", "hairpin crescendo"});
    const BeatEffects& effects =
        run.doc.song().track(0).staff(0).measure(0).voices[0].beats.at(0).effects;
    CHECK(effects.strum == Strum::Down);
    CHECK(effects.hairpin == Hairpin::Crescendo);
    CHECK(run.doc.undoName() == "Change beat effects");
}

TEST_CASE("Edit steps: malformed steps and steps the song rejects fail apart", "[cli][edit]") {
    Run run = runSteps({"add-track electric-guitar", "append-measures 1"});
    const Song before = run.doc.song();

    // Malformed: exit 2, with the reason after the step's verb.
    CHECK_THROWS_MATCHES(runStep(run.doc, "repeat 0 end 1", run.state), StepSyntaxError,
                         Message("repeat: a section plays 2 to 255 times, not 1"));
    CHECK_THROWS_MATCHES(runStep(run.doc, "ending 0 9", run.state), StepSyntaxError,
                         Message("ending: times through are 1 to 8, not 9"));
    CHECK_THROWS_MATCHES(runStep(run.doc, "jump 0 none al-coda", run.state), StepSyntaxError,
                         Message("jump: not al-fine, al-coda or al-double-coda: al-coda"));
    CHECK_THROWS_MATCHES(runStep(run.doc, "explode 2", run.state), StepSyntaxError,
                         Message("unknown step: explode"));
    for (const std::string_view step :
         {"sign 0 dolce", "barline 0 triple", "strum sideways", "voice 5", "repeat 0", ""}) {
        CHECK(failureOf(run, step) == Failure::Syntax);
    }

    // Rejected by the song: exit 1.
    for (const std::string_view step :
         {"repeat 4 start", "barline 4 double", "remove-track 3", "caret 0 5 0", "paste 0"}) {
        CAPTURE(step);
        CHECK(failureOf(run, step) == Failure::Rejected);
    }
    CHECK(run.doc.song() == before);

    Run empty;
    CHECK(failureOf(empty, "undo") == Failure::Rejected);
    CHECK(failureOf(empty, "fret 3") == Failure::Rejected);
}

// ---- The session the steps share with the app (ADR-0127) -----------------------

TEST_CASE("Edit steps: the input duration follows the beat the caret moves to", "[cli][edit]") {
    using elara::core::Duration;
    using elara::core::NoteValue;
    Run run = runSteps({"add-track electric-guitar", "append-measures 2", "duration 8", "fret 3",
                        "right", "duration 2", "fret 5"});
    CHECK(run.state.session.input() == Duration(NoteValue::Half));
    // Back on the eighth, typing uses eighths again, as the app's keys do.
    runStep(run.doc, "left", run.state);
    CHECK(run.state.session.input() == Duration(NoteValue::Eighth));
    runStep(run.doc, "insert-rest", run.state);
    const auto& beats = run.doc.song().track(0).staff(0).measure(0).voices[0].beats;
    REQUIRE(beats.size() == 3);
    CHECK(beats[0].isRest());
    CHECK(beats[0].duration == Duration(NoteValue::Eighth));

    // A caret step follows the beat it names too, and the free slot leaves
    // the input as it was.
    runStep(run.doc, "caret 0 0 2", run.state);
    CHECK(run.state.session.input() == Duration(NoteValue::Half));
    runStep(run.doc, "next-measure", run.state);
    CHECK(run.state.session.input() == Duration(NoteValue::Half));
    runStep(run.doc, "fret 7", run.state);
    CHECK(run.doc.song().track(0).staff(0).measure(1).voices[0].beats.at(0).duration ==
          Duration(NoteValue::Half));
}

TEST_CASE("Edit steps: the caret is inside the song after every step", "[cli][edit]") {
    using elara::core::Caret;
    Run run = runSteps({"add-track electric-guitar", "append-measures 3", "caret 0 2 0", "fret 3",
                        "remove-measures 1 2"});
    CHECK(run.state.session.caret() == Caret{});
    runStep(run.doc, "undo", run.state);
    runStep(run.doc, "caret 0 2 0", run.state);
    CHECK(run.state.session.caret() == Caret{.measure = 2});
    // A caret the song does not have is refused, and the caret stays.
    CHECK(failureOf(run, "caret 0 9 0") == Failure::Rejected);
    CHECK(failureOf(run, "caret 0 2 5") == Failure::Rejected);
    CHECK(run.state.session.caret() == Caret{.measure = 2});
}

TEST_CASE("Edit steps: a song with no place for a caret refuses the entry steps", "[cli][edit]") {
    Run run = runSteps({});
    for (const std::string_view step : {"right", "voice 2", "duration 8", "fret 3", "delete"}) {
        INFO(step);
        CHECK(failureOf(run, step) == Failure::Rejected);
    }
    CHECK_FALSE(run.doc.canUndo());
}

TEST_CASE("Edit steps: a track takes another name, or none", "[cli][edit]") {
    Run run = runSteps({"add-track electric-guitar", "add-track electric-bass",
                        "rename-track 1 Bass, with a pick"});
    CHECK(run.doc.song().track(1).name() == "Bass, with a pick");
    CHECK(run.doc.undoName() == "Rename track");
    // The name it has adds no undo step.
    const std::size_t steps = run.doc.undoCount();
    runStep(run.doc, "rename-track 1 Bass, with a pick", run.state);
    CHECK(run.doc.undoCount() == steps);
    runStep(run.doc, "rename-track 1", run.state);
    CHECK(run.doc.song().track(1).name().empty());
    CHECK(failureOf(run, "rename-track 2 Nobody") == Failure::Rejected);
    CHECK(failureOf(run, "rename-track") == Failure::Syntax);
}
