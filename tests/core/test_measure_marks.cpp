// SPDX-License-Identifier: AGPL-3.0-or-later
// The marks of a measure header edited a group at a time (ADR-0123): signs and
// jumps (ADR-0049), repeats and endings (ADR-0016) and the double barline
// (ADR-0103).
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/names.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace elara::core;

namespace {

/// Four empty measures; the second has a section and a time signature of its
/// own, which no mark edit touches.
Song fourMeasures() {
    Song song;
    song.appendMeasures(4);
    song.measureHeader(1).section = "Verse";
    song.measureHeader(1).timeSignature = TimeSignature(3, 4);
    return song;
}

/// Executes `command`, which must change something, and gives the undo name.
std::string_view run(Document& doc, std::unique_ptr<Command> command) {
    REQUIRE(command != nullptr);
    doc.execute(std::move(command));
    return doc.undoName();
}

} // namespace

TEST_CASE("Navigation marks: the signs and the jump of a measure as one step",
          "[core][commands][marks]") {
    const Song before = fourMeasures();
    Document doc(before);
    NavigationMarks marks;
    marks.set(NavigationSign::Segno, true);
    marks.set(NavigationSign::ToCoda, true);
    marks.jump = JumpTo::Segno;
    marks.jumpThen = JumpThen::Coda;
    CHECK(run(doc, changeNavigation(doc.song(), 1, marks)) == "Change navigation marks");
    CHECK(doc.lastChanges().sound);

    Song expected = before;
    expected.measureHeader(1).segno = true;
    expected.measureHeader(1).toCoda = true;
    expected.measureHeader(1).jump = JumpTo::Segno;
    expected.measureHeader(1).jumpThen = JumpThen::Coda;
    CHECK(doc.song() == expected);
    CHECK(navigationOf(doc.song().measureHeader(1)) == marks);

    // The same marks again change nothing.
    CHECK(changeNavigation(doc.song(), 1, marks) == nullptr);

    // Without a jump nothing ends the music early: what was to end it goes.
    marks.jump = JumpTo::None;
    run(doc, changeNavigation(doc.song(), 1, marks));
    expected.measureHeader(1).jump = JumpTo::None;
    expected.measureHeader(1).jumpThen = JumpThen::End;
    CHECK(doc.song() == expected);
    CHECK(changeNavigation(doc.song(), 1, marks) == nullptr);

    doc.undo();
    doc.undo();
    CHECK(doc.song() == before);
}

TEST_CASE("Navigation marks: a missing measure or an unknown jump is refused",
          "[core][commands][marks]") {
    const Song song = fourMeasures();
    CHECK_THROWS_AS(changeNavigation(song, 4, NavigationMarks{}), std::out_of_range);
    CHECK_THROWS_AS(
        changeNavigation(song, 0, NavigationMarks{.jump = static_cast<JumpTo>(kEnumCount<JumpTo>)}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        changeNavigation(
            song, 0, NavigationMarks{.jump = JumpTo::Start, .jumpThen = static_cast<JumpThen>(9)}),
        std::invalid_argument);
}

TEST_CASE("Navigation marks: each sign is its own flag of the header", "[core][commands][marks]") {
    for (const NavigationSign sign : enumValues<NavigationSign>()) {
        MeasureHeader header;
        header.*signFlag(sign) = true;
        const NavigationMarks marks = navigationOf(header);
        for (const NavigationSign other : enumValues<NavigationSign>()) {
            CHECK(marks.has(other) == (other == sign));
        }
    }
    // The flags as MeasureHeader names them, which the native format and the
    // app's popup key the signs by.
    CHECK(signField(NavigationSign::Segno) == "segno");
    CHECK(signField(NavigationSign::SegnoSegno) == "segnoSegno");
    CHECK(signField(NavigationSign::Coda) == "coda");
    CHECK(signField(NavigationSign::DoubleCoda) == "doubleCoda");
    CHECK(signField(NavigationSign::Fine) == "fine");
    CHECK(signField(NavigationSign::ToCoda) == "toCoda");
    CHECK(signField(NavigationSign::ToDoubleCoda) == "toDoubleCoda");
}

TEST_CASE("Repeats: the barlines and endings of a measure as one step", "[core][commands][marks]") {
    const Song before = fourMeasures();
    Document doc(before);
    const RepeatMarks marks{.start = true,
                            .times = 3,
                            .endings = static_cast<std::uint8_t>(endingBit(1) | endingBit(2))};
    CHECK(run(doc, changeRepeats(doc.song(), 1, marks)) == "Change repeats");
    CHECK(doc.lastChanges().sound);

    Song expected = before;
    expected.measureHeader(1).repeatStart = true;
    expected.measureHeader(1).repeatTimes = 3;
    expected.measureHeader(1).alternateEndings = 0b011;
    CHECK(doc.song() == expected);
    CHECK(repeatsOf(doc.song().measureHeader(1)) == marks);
    CHECK(changeRepeats(doc.song(), 1, marks) == nullptr);

    // No end repeat is 0 times.
    run(doc, changeRepeats(doc.song(), 1, RepeatMarks{.start = true}));
    expected.measureHeader(1).repeatTimes = 0;
    expected.measureHeader(1).alternateEndings = 0;
    CHECK(doc.song() == expected);

    doc.undo();
    doc.undo();
    CHECK(doc.song() == before);
}

TEST_CASE("Repeats: times and endings out of range are refused, the song unchanged",
          "[core][commands][marks]") {
    const Song song = fourMeasures();
    for (const int times : {-1, 1, 256}) {
        CHECK_THROWS_AS(changeRepeats(song, 0, RepeatMarks{.times = times}), std::invalid_argument);
        CHECK_THROWS_AS(checkRepeatTimes(times), std::invalid_argument);
    }
    // 0 is no end repeat to an edit, but no number of times a section plays.
    CHECK(changeRepeats(song, 0, RepeatMarks{}) == nullptr);
    CHECK_THROWS_AS(checkRepeatTimes(0), std::invalid_argument);
    CHECK_NOTHROW(checkRepeatTimes(2));
    CHECK_NOTHROW(checkRepeatTimes(255));
    try {
        checkRepeatTimes(1);
        FAIL("1 time was taken");
    } catch (const std::invalid_argument& error) {
        CHECK(std::string_view(error.what()) == "a section plays 2 to 255 times, not 1");
    }

    CHECK(endingBit(1) == 0b1);
    CHECK(endingBit(MeasureHeader::kMaxEnding) == 0b1000'0000);
    for (const int time : {-1, 0, MeasureHeader::kMaxEnding + 1}) {
        CHECK_THROWS_AS(endingBit(time), std::invalid_argument);
    }
    try {
        static_cast<void>(endingBit(9));
        FAIL("a ninth time was taken");
    } catch (const std::invalid_argument& error) {
        CHECK(std::string_view(error.what()) == "times through are 1 to 8, not 9");
    }
    CHECK_THROWS_AS(changeRepeats(song, 4, RepeatMarks{}), std::out_of_range);
}

TEST_CASE("Double barline: one step that does not change the sound", "[core][commands][marks]") {
    const Song before = fourMeasures();
    Document doc(before);
    CHECK(run(doc, changeBarline(doc.song(), 2, true)) == "Change barline");
    CHECK(doc.lastChanges().notation);
    CHECK_FALSE(doc.lastChanges().sound);
    Song expected = before;
    expected.measureHeader(2).doubleBar = true;
    CHECK(doc.song() == expected);
    CHECK(changeBarline(doc.song(), 2, true) == nullptr);
    CHECK_THROWS_AS(changeBarline(doc.song(), 4, true), std::out_of_range);
    doc.undo();
    CHECK(doc.song() == before);
}

TEST_CASE("Mark edits keep the measure's other marks", "[core][commands][marks]") {
    Song song = fourMeasures();
    MeasureHeader& header = song.measureHeader(1);
    header.coda = true;
    header.jump = JumpTo::Start;
    header.jumpThen = JumpThen::Fine;
    header.repeatStart = true;
    header.repeatTimes = 2;
    header.alternateEndings = 0b10;
    header.doubleBar = true;
    Document doc(song);

    run(doc, changeRepeats(doc.song(), 1, RepeatMarks{.times = 4}));
    run(doc, changeNavigation(doc.song(), 1, NavigationMarks{}));
    run(doc, changeBarline(doc.song(), 1, false));
    Song expected = song;
    expected.measureHeader(1).repeatStart = false;
    expected.measureHeader(1).repeatTimes = 4;
    expected.measureHeader(1).alternateEndings = 0;
    expected.measureHeader(1).coda = false;
    expected.measureHeader(1).jump = JumpTo::None;
    expected.measureHeader(1).jumpThen = JumpThen::End;
    expected.measureHeader(1).doubleBar = false;
    CHECK(doc.song() == expected);
    CHECK(doc.song().measureHeader(1).section == "Verse");
    CHECK(doc.song().measureHeader(1).timeSignature == TimeSignature(3, 4));
}
