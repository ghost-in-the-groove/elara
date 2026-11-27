// SPDX-License-Identifier: AGPL-3.0-or-later
// Typing frets on the keyboard (ADR-0032), through the document: a digit
// types a fret at the caret, and a second one within kTwoDigitMilliseconds on
// the same caret makes a two-digit fret, as one undo step.
#include "caret_editor.hpp"
#include "score_document.hpp"
#include <QObject>
#include <QString>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

namespace {

using elara::app::CaretEditor;
using elara::app::ScoreDocument;
using Clock = elara::core::EditSession::Clock;

// The two-digit window is measured between the times the keys are given, so
// these tests type at chosen instants rather than at the clock's whim.
const Clock::time_point kStart = Clock::now();

// A new song of rests on `preset`, the caret on its first beat.
struct Entry {
    ScoreDocument score;
    CaretEditor editor; // what is at the caret
    int failures = 0;

    explicit Entry(const char* preset = "electric-guitar") {
        editor.setDocument(&score);
        score.newSong(QString::fromLatin1(preset));
        QObject::connect(&score, &ScoreDocument::editFailed, [this] { ++failures; });
    }

    void type(int first, int second) {
        score.typeDigit(first, kStart);
        score.typeDigit(second, kStart);
    }
};

} // namespace

TEST_CASE("fret entry: two digits make one fret, as one undo step") {
    Entry entry;
    REQUIRE(entry.editor.caretFretted());
    REQUIRE_FALSE(entry.editor.fretAtCaret());
    entry.type(1, 2);
    CHECK(entry.editor.fretAtCaret() == 12);
    entry.score.undo();
    CHECK_FALSE(entry.editor.fretAtCaret());
    CHECK_FALSE(entry.score.modified());
    entry.score.redo();
    CHECK(entry.editor.fretAtCaret() == 12);
}

TEST_CASE("fret entry: a second digit off the neck starts a fret of its own") {
    Entry entry;
    entry.type(4, 5); // no fret 45
    CHECK(entry.editor.fretAtCaret() == 5);
    CHECK(entry.failures == 0);
    entry.score.undo();
    CHECK(entry.editor.fretAtCaret() == 4); // the first digit is a step of its own
}

TEST_CASE("fret entry: a first digit 0 is fret 0, and the next digit a fret of its own") {
    Entry entry;
    entry.type(0, 7);
    CHECK(entry.editor.fretAtCaret() == 7);
    entry.score.undo();
    CHECK(entry.editor.fretAtCaret() == 0);
}

TEST_CASE("fret entry: a second digit after the caret moved starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1, kStart);
    entry.score.moveCaret(ScoreDocument::CaretMove::StringDown);
    entry.score.moveCaret(ScoreDocument::CaretMove::StringUp);
    entry.score.typeDigit(2, kStart);
    CHECK(entry.editor.fretAtCaret() == 2);
}

TEST_CASE("fret entry: a second digit after an undo starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1, kStart);
    entry.score.undo();
    entry.score.typeDigit(2, kStart);
    CHECK(entry.editor.fretAtCaret() == 2);
}

TEST_CASE("fret entry: a second digit too late starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1, kStart);
    entry.score.typeDigit(2, kStart +
                                 std::chrono::milliseconds(ScoreDocument::kTwoDigitMilliseconds) +
                                 std::chrono::milliseconds(1));
    CHECK(entry.editor.fretAtCaret() == 2);
    entry.score.undo();
    CHECK(entry.editor.fretAtCaret() == 1);
}

TEST_CASE("fret entry: frets are typed only on a track with strings") {
    Entry piano("piano");
    CHECK_FALSE(piano.editor.caretFretted());
    piano.score.typeDigit(3);
    CHECK(piano.failures == 1);
    CHECK_FALSE(piano.score.modified());

    Entry drums("drum-kit");
    drums.score.typeDigit(3);
    CHECK(drums.failures == 1);
    CHECK_FALSE(drums.score.modified());
}
