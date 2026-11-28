// SPDX-License-Identifier: AGPL-3.0-or-later
// Editing through the document as the keys and menus of the app do it
// (ADR-0021, ADR-0027, ADR-0032): where the caret goes, what the input
// duration follows, what a selection covers and what copy, paste and undo do
// to them.
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>
#include <elara/notation/display_list.hpp>

#include "caret_editor.hpp"
#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace {

using elara::app::CaretEditor;
using elara::app::ScoreDocument;
using elara::core::Beat;
using elara::core::Caret;
using elara::core::NoteValue;
using Move = ScoreDocument::CaretMove;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// A new song of eight empty measures on `preset`, the caret on its first beat.
struct Editing {
    ScoreDocument score;
    CaretEditor editor; // what is at the caret
    QStringList failures;
    QStringList notices;

    explicit Editing(const char* preset = "electric-guitar") {
        editor.setDocument(&score);
        score.newSong(QString::fromLatin1(preset));
        QObject::connect(&score, &ScoreDocument::editFailed,
                         [this](const QString& message) { failures.append(message); });
        QObject::connect(&score, &ScoreDocument::notice,
                         [this](const QString& message) { notices.append(message); });
    }

    // The beats of the first voice of a measure of the first track.
    [[nodiscard]] const std::vector<Beat>& beats(std::size_t measure, std::size_t voice = 0,
                                                 std::size_t track = 0) const {
        return score.song()->track(track).staff(0).measure(measure).voices.at(voice).beats;
    }
    [[nodiscard]] std::size_t measures() const { return score.song()->measureCount(); }

    void move(Move move, int times = 1) {
        for (int i = 0; i < times; ++i) {
            score.moveCaret(move);
        }
    }
    // Types a one-digit fret and lets the moment for a second digit pass.
    void fret(int digit) {
        score.typeDigit(digit);
        score.moveCaret(Move::StringDown);
        score.moveCaret(Move::StringUp);
    }
};

} // namespace

// ---- The caret and typing --------------------------------------------------

TEST_CASE("editing: a fret typed on the free slot adds a beat of the input duration") {
    Editing editing;
    REQUIRE(editing.beats(0).empty());
    CHECK(editing.score.inputValue() == 4);

    editing.fret(3);
    REQUIRE(editing.beats(0).size() == 1);
    CHECK(editing.beats(0)[0].duration.value() == NoteValue::Quarter);
    CHECK(editing.score.caret() == Caret{});
    CHECK(editing.editor.fretAtCaret() == 3);

    // The free slot after it takes the next one.
    editing.move(Move::NextBeat);
    CHECK(editing.score.caret().beat == 1);
    editing.score.setInputValue(8);
    editing.fret(5);
    REQUIRE(editing.beats(0).size() == 2);
    CHECK(editing.beats(0)[1].duration.value() == NoteValue::Eighth);
}

TEST_CASE("editing: the caret goes to a measure's first beat, and stays in the song") {
    Editing editing;
    editing.fret(3);
    editing.move(Move::NextBeat);
    editing.fret(5);
    editing.move(Move::StringDown);
    REQUIRE(editing.score.caret().string == 2);

    // What "go to measure 5" does: the first beat, on the same string.
    editing.score.caretToMeasure(4);
    CHECK(editing.score.caret() == Caret{.measure = 4, .string = 2});
    editing.score.caretToMeasure(0);
    CHECK(editing.score.caret() == Caret{.string = 2});
    // A measure past the end is the last; none before the first.
    editing.score.caretToMeasure(100);
    CHECK(editing.score.caretMeasure() == static_cast<int>(editing.measures()) - 1);
    editing.score.caretToMeasure(-1);
    CHECK(editing.score.caretMeasure() == static_cast<int>(editing.measures()) - 1);
}

TEST_CASE("editing: the input duration follows the beat the caret moves to") {
    Editing editing;
    editing.score.setInputValue(8);
    editing.fret(3);
    editing.move(Move::NextBeat);
    editing.score.setInputValue(2);
    editing.score.toggleDot();
    editing.fret(5);

    editing.move(Move::PreviousBeat);
    CHECK(editing.score.inputValue() == 8);
    CHECK_FALSE(editing.score.inputDotted());
    editing.move(Move::NextBeat);
    CHECK(editing.score.inputValue() == 2);
    CHECK(editing.score.inputDotted());
    // The free slot has no duration to follow: the input stays.
    editing.move(Move::NextBeat);
    CHECK(editing.score.caret().beat == 2);
    CHECK(editing.score.inputValue() == 2);
    CHECK(editing.score.inputDotted());
}

TEST_CASE("editing: the caret's band holds its box, and stays when the string changes") {
    Editing editing;
    REQUIRE(settle([&editing] { return editing.score.caretPage() == 0; }));
    const QRectF box = editing.score.caretRect();
    const QRectF band = editing.score.caretColumn();
    REQUIRE(box.isValid());
    // Notation over tablature: the band runs over both, the box sits on a string.
    CHECK(band.left() == box.left());
    CHECK(band.width() == box.width());
    CHECK(band.top() < box.top());
    CHECK(band.bottom() > box.bottom());
    CHECK(band.height() > 3 * box.height());

    editing.move(Move::StringDown);
    CHECK(editing.score.caretRect().top() > box.top());
    CHECK(editing.score.caretColumn() == band);

    // Notation alone: the box spans the staff, and the band is the box.
    editing.score.setView(ScoreDocument::View::Standard);
    REQUIRE(settle([&editing] { return !editing.score.busy(); }));
    CHECK(editing.score.caretColumn() == editing.score.caretRect());
}

TEST_CASE("editing: an input duration given on a beat is given to the beat, as one undo step") {
    Editing editing;
    editing.fret(3);
    editing.score.setInputValue(2);
    CHECK(editing.beats(0)[0].duration.value() == NoteValue::Half);
    editing.score.toggleDot();
    CHECK(editing.beats(0)[0].duration.dots() == 1);
    editing.score.toggleDot();
    CHECK(editing.beats(0)[0].duration.dots() == 0);
    editing.score.setInputTuplet(3);
    CHECK(editing.score.inputTuplet() == 3);
    CHECK(editing.beats(0)[0].duration.tuplet().actual() == 3);
    // The same tuplet again takes it off.
    editing.score.setInputTuplet(3);
    CHECK(editing.score.inputTuplet() == 0);
    CHECK(editing.beats(0)[0].duration.tuplet().isNone());

    editing.score.longerDuration();
    CHECK(editing.score.inputValue() == 1);
    editing.score.longerDuration(); // none longer
    CHECK(editing.score.inputValue() == 1);
    editing.score.shorterDuration();
    editing.score.shorterDuration();
    CHECK(editing.score.inputValue() == 4);
    CHECK(editing.beats(0)[0].duration.value() == NoteValue::Quarter);

    // A value typing does not offer changes nothing.
    editing.score.setInputValue(3);
    CHECK(editing.score.inputValue() == 4);
    CHECK(editing.failures.isEmpty());
}

TEST_CASE("editing: delete takes the note, then the rest; insert puts a rest before the beat") {
    Editing editing;
    editing.fret(3);
    editing.editor.deleteAtCaret();
    REQUIRE(editing.beats(0).size() == 1);
    CHECK(editing.beats(0)[0].isRest());
    editing.editor.deleteAtCaret();
    CHECK(editing.beats(0).empty());

    editing.fret(3);
    editing.score.setInputValue(8);
    CHECK(editing.beats(0)[0].duration.value() == NoteValue::Eighth);
    editing.score.setInputValue(2);
    editing.editor.insertRest();
    REQUIRE(editing.beats(0).size() == 2);
    CHECK(editing.beats(0)[0].isRest());
    CHECK(editing.beats(0)[0].duration.value() == NoteValue::Half);
    CHECK(editing.score.caret().beat == 0);
    editing.editor.deleteBeat();
    REQUIRE(editing.beats(0).size() == 1);
    CHECK(editing.editor.fretAtCaret() == 3);
}

TEST_CASE("editing: the voice typing goes into") {
    Editing editing;
    editing.fret(3);
    editing.score.setCaretVoice(2);
    CHECK(editing.score.caretVoice() == 2);
    CHECK(editing.score.caret().voice == 1);
    editing.fret(5);
    CHECK(editing.beats(0, 1).size() == 1);
    CHECK(editing.beats(0, 0).size() == 1);
    editing.score.setCaretVoice(5); // there are four
    CHECK(editing.score.caretVoice() == 2);
    editing.score.setCaretVoice(0);
    CHECK(editing.score.caretVoice() == 2);
}

TEST_CASE("editing: a measure inserted after the caret's takes the caret, one before leaves it") {
    Editing editing;
    editing.fret(3);
    editing.score.insertMeasure(true);
    CHECK(editing.measures() == 9);
    CHECK(editing.score.caret().measure == 1);
    CHECK(editing.beats(0).size() == 1);
    CHECK(editing.beats(1).empty());

    editing.move(Move::PreviousMeasure);
    REQUIRE(editing.score.caret().measure == 0);
    editing.score.insertMeasure(false);
    CHECK(editing.measures() == 10);
    CHECK(editing.score.caret().measure == 0);
    CHECK(editing.beats(0).empty());
    CHECK(editing.beats(1).size() == 1);
}

TEST_CASE("editing: the caret and the selection stay inside the song when measures go") {
    Editing editing;
    editing.move(Move::NextMeasure, 7);
    REQUIRE(editing.score.caret().measure == 7);
    editing.score.removeMeasure();
    CHECK(editing.measures() == 7);
    CHECK(editing.score.caret().measure == 6);
    editing.score.undo();
    CHECK(editing.measures() == 8);
    CHECK(editing.score.caret().measure == 6);

    // The selection started at the last measure, which goes.
    editing.score.selectAll();
    REQUIRE(editing.score.selectionLast() == 7);
    editing.score.setMeasureCount(4);
    CHECK(editing.score.selectionFirst() == 0);
    CHECK(editing.score.selectionLast() == 3);

    // The last measure stays: a song has one at least.
    editing.score.clearSelection();
    editing.score.setMeasureCount(1);
    editing.score.removeMeasure();
    CHECK(editing.measures() == 1);
}

TEST_CASE("editing: a track takes another name, as one undo step") {
    Editing editing;
    const QString preset = editing.score.trackNames().at(0);
    editing.score.setTrackName(0, QStringLiteral("  Lead guitar "));
    CHECK(editing.score.trackNames().at(0) == QStringLiteral("Lead guitar"));
    CHECK(editing.score.undoName() == QStringLiteral("Rename track"));
    // The name it has, a track the song lacks: nothing to undo.
    editing.score.undo();
    CHECK(editing.score.trackNames().at(0) == preset);
    CHECK_FALSE(editing.score.modified());
    editing.score.setTrackName(0, preset);
    editing.score.setTrackName(3, QStringLiteral("Nobody"));
    editing.score.setTrackName(-1, QStringLiteral("Nobody"));
    CHECK_FALSE(editing.score.modified());
    // Without a name the song has none, and the lists call the track by its number.
    editing.score.setTrackName(0, QString());
    CHECK(editing.score.song()->track(0).name().empty());
    CHECK(editing.score.trackNames().at(0) == QStringLiteral("Track 1"));
}

TEST_CASE("editing: the caret stays on the track shown") {
    Editing editing;
    editing.score.addTrack(QStringLiteral("electric-bass"));
    REQUIRE(editing.score.song()->trackCount() == 2);
    CHECK(editing.score.track() == 1);
    CHECK(editing.score.caret().track == 1);
    editing.fret(3);
    CHECK(editing.beats(0, 0, 1).size() == 1);
    CHECK(editing.beats(0, 0, 0).empty());

    editing.score.setTrack(0);
    CHECK(editing.score.caret().track == 0);
    // With every track shown, the caret stays on the one it was on.
    editing.score.setTrack(ScoreDocument::kAllTracks);
    CHECK(editing.score.caret().track == 0);

    // The track shown goes: the one before it is shown, and has the caret.
    editing.score.setTrack(1);
    editing.score.removeTrack(1);
    CHECK(editing.score.track() == 0);
    CHECK(editing.score.caret().track == 0);
}

TEST_CASE("editing: undo and redo keep the caret on a beat the song has") {
    Editing editing;
    editing.fret(3);
    editing.move(Move::NextBeat);
    editing.fret(5);
    REQUIRE(editing.score.caret().beat == 1);
    editing.score.undo();
    CHECK(editing.beats(0).size() == 1);
    CHECK(editing.score.caret().beat == 1); // the free slot
    editing.score.undo();
    CHECK(editing.beats(0).empty());
    CHECK(editing.score.caret().beat == 0);
    CHECK_FALSE(editing.score.modified());
    editing.score.redo();
    editing.score.redo();
    CHECK(editing.beats(0).size() == 2);
    CHECK_FALSE(editing.score.canRedo());
}

// ---- The selection ---------------------------------------------------------

TEST_CASE("editing: a selection runs from where it started to the caret's measure") {
    Editing editing;
    editing.move(Move::NextMeasure, 2);
    CHECK_FALSE(editing.score.hasSelection());
    CHECK(editing.score.selectionFirst() == -1);

    editing.score.extendSelection(2);
    CHECK(editing.score.hasSelection());
    CHECK(editing.score.selectionFirst() == 2);
    CHECK(editing.score.selectionLast() == 4);
    CHECK(editing.score.caret().measure == 4);
    // Back past where it started: it turns round.
    editing.score.extendSelection(-3);
    CHECK(editing.score.selectionFirst() == 1);
    CHECK(editing.score.selectionLast() == 2);
    // It stops at the ends of the song.
    editing.score.extendSelection(-10);
    CHECK(editing.score.selectionFirst() == 0);
    editing.score.extendSelection(100);
    CHECK(editing.score.selectionLast() == 7);
    editing.score.extendSelection(0);
    CHECK(editing.score.selectionLast() == 7);

    editing.score.selectAll();
    CHECK(editing.score.selectionFirst() == 0);
    CHECK(editing.score.selectionLast() == 7);
    CHECK(editing.score.caret().measure == 0);
}

TEST_CASE("editing: moving the caret ends the selection, another voice keeps it") {
    Editing editing;
    editing.score.extendSelection(2);
    editing.score.setCaretVoice(2);
    CHECK(editing.score.hasSelection());
    editing.move(Move::NextBeat);
    CHECK_FALSE(editing.score.hasSelection());

    editing.score.extendSelection(1);
    editing.score.clearSelection();
    CHECK_FALSE(editing.score.hasSelection());
    editing.score.clearSelection(); // none to clear
    CHECK_FALSE(editing.score.hasSelection());
}

TEST_CASE("editing: another song starts without a selection, at its first beat") {
    Editing editing;
    editing.move(Move::NextMeasure, 3);
    editing.score.setInputValue(8);
    editing.score.selectAll();
    editing.score.newSong(QStringLiteral("electric-bass"));
    CHECK_FALSE(editing.score.hasSelection());
    CHECK(editing.score.caret() == Caret{});
    CHECK(editing.score.inputValue() == 4);
}

TEST_CASE("editing: a shorter song opened over another does not keep its selection") {
    Editing editing;
    editing.score.selectAll(); // it started at the eighth measure
    editing.score.open(QUrl::fromLocalFile(QStringLiteral(ELARA_CORPUS_DIR "/gp/melody.gp3")));
    REQUIRE(settle([&editing] {
        return editing.score.status() == ScoreDocument::Status::Ready && editing.measures() < 8;
    }));
    CHECK_FALSE(editing.score.hasSelection());
    CHECK(editing.score.caret().measure == 0);
    // Copy takes the caret's measure, which the song has.
    editing.score.copy();
    CHECK(editing.notices == QStringList{QStringLiteral("Copied measure 1.")});
}

// ---- Copy, paste and transpose ---------------------------------------------

TEST_CASE("editing: copy takes the caret's measure, and paste puts it at the caret's") {
    Editing editing;
    editing.fret(3);
    CHECK_FALSE(editing.score.canPaste());
    editing.score.copy();
    CHECK(editing.score.canPaste());
    CHECK(editing.notices == QStringList{QStringLiteral("Copied measure 1.")});

    editing.move(Move::NextMeasure, 2);
    editing.score.paste(false);
    CHECK(editing.beats(2) == editing.beats(0));
    CHECK(editing.measures() == 8);
    // What was pasted is selected, the caret on its last measure.
    CHECK(editing.score.selectionFirst() == 2);
    CHECK(editing.score.selectionLast() == 2);
    CHECK(editing.score.caret().measure == 2);
    editing.score.undo();
    CHECK(editing.beats(2).empty());

    // Inserted, it goes into new measures before the caret's.
    editing.score.clearSelection();
    editing.score.paste(true);
    CHECK(editing.measures() == 9);
    CHECK(editing.beats(2) == editing.beats(0));
    CHECK(editing.beats(3).empty());
}

TEST_CASE("editing: copy takes the selection, and paste goes over the selection's first measure") {
    Editing editing;
    editing.fret(3);
    editing.move(Move::NextMeasure);
    editing.fret(5);
    editing.move(Move::PreviousMeasure);
    editing.score.extendSelection(1);
    editing.score.copy();
    CHECK(editing.notices == QStringList{QStringLiteral("Copied measures 1-2.")});

    editing.move(Move::NextMeasure, 4);
    REQUIRE(editing.score.caret().measure == 5);
    editing.score.extendSelection(-1); // measures 5 and 6, the caret on the first
    editing.score.paste(false);
    CHECK(editing.beats(4) == editing.beats(0));
    CHECK(editing.beats(5) == editing.beats(1));
    CHECK(editing.score.selectionFirst() == 4);
    CHECK(editing.score.selectionLast() == 5);
    CHECK(editing.score.caret().measure == 5);

    // Past the end, a paste over adds the measures it needs.
    editing.score.clearSelection();
    editing.move(Move::NextMeasure, 2);
    REQUIRE(editing.score.caret().measure == 7);
    editing.score.paste(false);
    CHECK(editing.measures() == 9);
    CHECK(editing.beats(8) == editing.beats(1));
}

TEST_CASE("editing: a paste that cannot be says why and leaves the song and the selection") {
    Editing editing;
    editing.score.paste(false);
    CHECK(editing.failures == QStringList{QStringLiteral("Copy some measures first.")});
    CHECK_FALSE(editing.score.modified());

    // A guitar's measure does not go to the drums.
    editing.fret(3);
    editing.score.copy();
    editing.score.addTrack(QStringLiteral("drum-kit"));
    REQUIRE(editing.score.caret().track == 1);
    editing.failures.clear();
    const auto before = editing.score.song();
    editing.score.paste(false);
    CHECK(editing.failures == QStringList{QStringLiteral("Those measures do not fit this track.")});
    CHECK(editing.score.song() == before);
    CHECK_FALSE(editing.score.hasSelection());
}

TEST_CASE("editing: transposing moves the notes of the selection, or of the caret's measure") {
    Editing editing;
    editing.fret(3);
    editing.move(Move::NextMeasure);
    editing.fret(5);
    const int first = editing.beats(0)[0].notes[0].pitch.midiNumber();
    const int second = editing.beats(1)[0].notes[0].pitch.midiNumber();

    editing.score.transposeSelection(2); // the caret's measure, the second
    CHECK(editing.beats(0)[0].notes[0].pitch.midiNumber() == first);
    CHECK(editing.beats(1)[0].notes[0].pitch.midiNumber() == second + 2);
    editing.score.selectAll();
    editing.score.transposeSelection(-1);
    CHECK(editing.beats(0)[0].notes[0].pitch.midiNumber() == first - 1);
    CHECK(editing.beats(1)[0].notes[0].pitch.midiNumber() == second + 1);
    CHECK(editing.score.hasSelection());

    editing.score.transposeSelection(0);
    editing.score.transposeSelection(-60); // below the lowest string
    CHECK(editing.failures ==
          QStringList{QStringLiteral("A note would leave the range or the neck.")});
    CHECK(editing.beats(0)[0].notes[0].pitch.midiNumber() == first - 1);
}

// ---- Marks on the pages ----------------------------------------------------

TEST_CASE("editing: measure spans are a rectangle a system, over the measures asked for") {
    using Catch::Matchers::WithinAbs;
    Editing editing;
    ScoreDocument& score = editing.score;
    score.setMeasureCount(40);
    REQUIRE(
        settle([&] { return !score.busy() && score.pageCount() > 0 && editing.measures() == 40; }));
    const auto page = score.page(0);
    REQUIRE(page);
    REQUIRE(page->systems.size() >= 2);
    const int second = static_cast<int>(page->systems[1].firstMeasure);
    // From the second measure of the first system to the second of the next.
    const int from = static_cast<int>(page->systems[0].firstMeasure) + 1;
    const int to = second + 1;
    const QVariantList spans = score.measureSpans(0, from, to);
    REQUIRE(spans.size() == 2);
    const QRectF first = spans[0].toRectF();
    const QRectF next = spans[1].toRectF();
    CHECK_THAT(first.left(), WithinAbs(score.measureBox(0, from).left(), 1e-9));
    CHECK_THAT(first.right(), WithinAbs(score.measureBox(0, second - 1).right(), 1e-9));
    CHECK_THAT(first.top(), WithinAbs(score.measureBox(0, from).top(), 1e-9));
    CHECK_THAT(first.height(), WithinAbs(score.measureBox(0, from).height(), 1e-9));
    CHECK_THAT(next.left(), WithinAbs(score.measureBox(0, second).left(), 1e-9));
    CHECK_THAT(next.right(), WithinAbs(score.measureBox(0, to).right(), 1e-9));
    CHECK(next.top() > first.top());
    // One measure; none for a range the wrong way round or a page not there.
    CHECK(score.measureSpans(0, from, from).size() == 1);
    CHECK(score.measureSpans(0, to, from).isEmpty());
    CHECK(score.measureSpans(99, 0, 3).isEmpty());
}

TEST_CASE("editing: the song's tempo keeps the beat it counts in") {
    // ADR-0042: in 6/8 counted in dotted quarters, the song pane's tempo and
    // the first measure's set the dotted quarters a minute.
    using elara::core::Tempo;
    const elara::core::Duration dottedQuarter(NoteValue::Quarter, 1);
    elara::core::Song song;
    song.appendMeasures(2);
    song.addTrack(elara::core::Track::fromPreset(*elara::core::findInstrumentPreset("piano")));
    song.setStartTempo(Tempo(60, dottedQuarter));
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QString file = folder.filePath(QStringLiteral("dotted.elara"));
    elara::io::saveNative(song, std::filesystem::path(file.toStdU16String()));

    ScoreDocument score;
    score.open(QUrl::fromLocalFile(file));
    REQUIRE(
        settle([&] { return score.status() == ScoreDocument::Status::Ready && !score.busy(); }));
    score.setTempo(80);
    CHECK(score.song()->startTempo() == Tempo(80, dottedQuarter));
    score.setTempoFrom(0, 70);
    CHECK(score.song()->startTempo() == Tempo(70, dottedQuarter));
    CHECK(score.undoName() == QStringLiteral("Change tempo"));
}
