// SPDX-License-Identifier: AGPL-3.0-or-later
// Typing frets on the keyboard (ADR-0032), through the document: a digit
// types a fret at the caret, and a second one within kTwoDigitMilliseconds on
// the same caret makes a two-digit fret, as one undo step.
#include "score_document.hpp"
#include <QObject>
#include <QString>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <thread>

namespace {

using elara::app::ScoreDocument;

// A new song of rests on `preset`, the caret on its first beat.
struct Entry {
    ScoreDocument score;
    int failures = 0;

    explicit Entry(const char* preset = "electric-guitar") {
        score.newSong(QString::fromLatin1(preset));
        QObject::connect(&score, &ScoreDocument::editFailed, [this] { ++failures; });
    }

    void type(int first, int second) {
        score.typeDigit(first);
        score.typeDigit(second);
    }
};

} // namespace

TEST_CASE("fret entry: two digits make one fret, as one undo step") {
    Entry entry;
    REQUIRE(entry.score.caretFretted());
    REQUIRE_FALSE(entry.score.fretAtCaret());
    entry.type(1, 2);
    CHECK(entry.score.fretAtCaret() == 12);
    entry.score.undo();
    CHECK_FALSE(entry.score.fretAtCaret());
    CHECK_FALSE(entry.score.modified());
    entry.score.redo();
    CHECK(entry.score.fretAtCaret() == 12);
}

TEST_CASE("fret entry: a second digit off the neck starts a fret of its own") {
    Entry entry;
    entry.type(4, 5); // no fret 45
    CHECK(entry.score.fretAtCaret() == 5);
    CHECK(entry.failures == 0);
    entry.score.undo();
    CHECK(entry.score.fretAtCaret() == 4); // the first digit is a step of its own
}

TEST_CASE("fret entry: a first digit 0 is fret 0, and the next digit a fret of its own") {
    Entry entry;
    entry.type(0, 7);
    CHECK(entry.score.fretAtCaret() == 7);
    entry.score.undo();
    CHECK(entry.score.fretAtCaret() == 0);
}

TEST_CASE("fret entry: a second digit after the caret moved starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1);
    entry.score.moveCaret(ScoreDocument::CaretMove::StringDown);
    entry.score.moveCaret(ScoreDocument::CaretMove::StringUp);
    entry.score.typeDigit(2);
    CHECK(entry.score.fretAtCaret() == 2);
}

TEST_CASE("fret entry: a second digit after an undo starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1);
    entry.score.undo();
    entry.score.typeDigit(2);
    CHECK(entry.score.fretAtCaret() == 2);
}

TEST_CASE("fret entry: a second digit too late starts a fret of its own") {
    Entry entry;
    entry.score.typeDigit(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(ScoreDocument::kTwoDigitMilliseconds) +
                                std::chrono::milliseconds(50));
    entry.score.typeDigit(2);
    CHECK(entry.score.fretAtCaret() == 2);
    entry.score.undo();
    CHECK(entry.score.fretAtCaret() == 1);
}

TEST_CASE("fret entry: frets are typed only on a track with strings") {
    Entry piano("piano");
    CHECK_FALSE(piano.score.caretFretted());
    piano.score.typeDigit(3);
    CHECK(piano.failures == 1);
    CHECK_FALSE(piano.score.modified());

    Entry drums("drum-kit");
    drums.score.typeDigit(3);
    CHECK(drums.failures == 1);
    CHECK_FALSE(drums.score.modified());
}
