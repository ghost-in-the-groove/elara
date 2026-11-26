// SPDX-License-Identifier: AGPL-3.0-or-later
// CaretEditor (ADR-0128): what is at the caret of a document, read and edited
// through the document it is given.
#include <elara/core/entry.hpp>

#include "caret_editor.hpp"
#include "score_document.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>

namespace {

using elara::app::CaretEditor;
using elara::app::ScoreDocument;
using Move = ScoreDocument::CaretMove;

// How many times the editor said that what is at the caret changed.
struct Changes {
    int count = 0;

    explicit Changes(const CaretEditor& editor) {
        QObject::connect(&editor, &CaretEditor::caretChanged, [this] { ++count; });
    }
};

} // namespace

TEST_CASE("caret editor: without a document it reads an empty beat and edits nothing") {
    CaretEditor editor;
    CHECK(editor.document() == nullptr);
    CHECK(editor.caretStatus().isEmpty());
    CHECK_FALSE(editor.caretFretted());
    CHECK_FALSE(editor.caretOnDrums());
    CHECK_FALSE(editor.fretAtCaret());
    CHECK(editor.pitchesAtCaret().empty());
    CHECK(editor.caretEffects() == 0);
    CHECK(editor.caretStrum() == 0);
    CHECK(editor.caretChord().isEmpty());
    CHECK(editor.caretVerseCount() == 0);
    CHECK(editor.songChords().isEmpty());

    editor.deleteAtCaret();
    editor.insertRest();
    editor.typeNoteName(0, false);
    editor.toggleEffect(0);
    editor.setBend(1, 100);
    editor.setStrum(1);
    editor.setVibrato(1);
    editor.setDynamic(2);
    editor.setChordSymbol(QStringLiteral("Am"));
    CHECK(editor.setVerse(0, 0, QStringLiteral("la la")) == 0);
    // What does not depend on a song still answers.
    CHECK(editor.effectCount() == CaretEditor::kEffectCount);
    CHECK_FALSE(editor.effectName(0).isEmpty());
    CHECK(editor.effectForKey(editor.effectKey(0)) == 0);
}

TEST_CASE("caret editor: before a song is open it is as without a document") {
    ScoreDocument score;
    CaretEditor editor;
    editor.setDocument(&score);
    CHECK(editor.document() == &score);
    CHECK(editor.caretStatus().isEmpty());
    CHECK_FALSE(editor.caretFretted());
    editor.toggleEffect(0);
    editor.insertRest();
    CHECK_FALSE(score.modified());
}

TEST_CASE("caret editor: it edits the document's song at the document's caret") {
    ScoreDocument score;
    CaretEditor editor;
    editor.setDocument(&score);
    score.newSong(QStringLiteral("electric-guitar"));
    QStringList failures;
    QObject::connect(&score, &ScoreDocument::editFailed,
                     [&failures](const QString& message) { failures.append(message); });

    REQUIRE(editor.caretFretted());
    score.typeDigit(3);
    CHECK(editor.fretAtCaret() == 3);
    REQUIRE(editor.pitchesAtCaret().size() == 1);

    // An effect of the note, a beat effect and a chord over the beat: each an
    // undo step of the document's.
    editor.setVibrato(1);
    CHECK(editor.caretVibrato() == 1);
    CHECK(score.undoName() == QStringLiteral("Change vibrato"));
    editor.setStrum(1);
    CHECK(editor.caretStrum() == 1);
    editor.setChordSymbol(QStringLiteral("Am"));
    CHECK(editor.caretChord() == QStringLiteral("Am"));
    CHECK(editor.songChords() == QStringList{QStringLiteral("Am")});
    CHECK(editor.caretStatus().contains(QStringLiteral("Am")));
    score.undo();
    CHECK(editor.caretChord().isEmpty());
    CHECK(failures.isEmpty());

    // What the song refuses is told by the document, as its own edits are.
    score.moveCaret(Move::NextBeat); // the free slot: no note to bend
    editor.setBend(1, 100);
    REQUIRE(failures.size() == 1);
    CHECK(failures.front().contains(QStringLiteral("Effects go on notes")));

    // The caret is the document's: on another track, the editor reads that one.
    score.addTrack(QStringLiteral("drum-kit"));
    CHECK(editor.caretOnDrums());
    CHECK_FALSE(editor.caretFretted());
}

TEST_CASE("caret editor: it says so when the caret moves and when the song under it changes") {
    ScoreDocument score;
    CaretEditor editor;
    const Changes changes(editor);
    editor.setDocument(&score);
    CHECK(changes.count == 1); // another document: another caret

    score.newSong(QStringLiteral("electric-guitar"));
    score.typeDigit(3);
    int before = changes.count;
    score.moveCaret(Move::StringDown);
    CHECK(changes.count > before);
    score.moveCaret(Move::StringUp); // back on the note
    before = changes.count;
    editor.setVibrato(1);
    CHECK(changes.count > before);
    before = changes.count;
    score.undo();
    CHECK(changes.count > before);

    // Given another document, it no longer follows the first.
    ScoreDocument other;
    editor.setDocument(&other);
    before = changes.count;
    score.moveCaret(Move::StringUp);
    CHECK(changes.count == before);
    editor.setDocument(&other); // the same again: nothing to say
    CHECK(changes.count == before);
}
