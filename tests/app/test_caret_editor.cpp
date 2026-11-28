// SPDX-License-Identifier: AGPL-3.0-or-later
// CaretEditor (ADR-0128): what is at the caret of a document, read and edited
// through the document it is given.
#include <elara/core/duration.hpp>
#include <elara/core/edit_session.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/names.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/refusal.hpp>

#include "caret_editor.hpp"
#include "score_document.hpp"
#include "songs_to_edit.hpp"
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace {

using elara::app::CaretEditor;
using elara::app::ScoreDocument;
using elara::app::testing::kSongsToEdit;
using elara::app::testing::SongToEdit;
using Move = ScoreDocument::CaretMove;

// How many times the editor said that what is at the caret changed.
struct Changes {
    int count = 0;

    explicit Changes(const CaretEditor& editor) {
        QObject::connect(&editor, &CaretEditor::caretChanged, [this] { ++count; });
    }
};

// The duration of the beat at the caret, if the caret is on one.
std::optional<elara::core::Duration> caretDuration(const ScoreDocument& score) {
    const elara::core::Caret caret = score.caret();
    const elara::core::Voice& voice = score.song()
                                          ->track(caret.track)
                                          .staff(caret.staff)
                                          .measure(caret.measure)
                                          .voices.at(caret.voice);
    return caret.beat < voice.beats.size() ? std::optional(voice.beats[caret.beat].duration)
                                           : std::nullopt;
}

// The drum lane (1 at the top) of the kit piece with General MIDI `key`.
int laneOf(int key) {
    const auto pieces = elara::core::drumPieces();
    const auto found = std::ranges::find(pieces, key, &elara::core::DrumPiece::key);
    return static_cast<int>(found - pieces.begin()) + 1;
}

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

    // The fingering buttons step through the fingers, the thumb last on a
    // fretted staff, and show the one a press gives while there is none.
    CHECK(editor.caretFingeringLabel() == QStringLiteral("1"));
    QStringList fingers;
    for (int i = 0; i < 6; ++i) {
        editor.nextFingering();
        fingers.append(editor.caretFingering() == 0 ? QStringLiteral("-")
                                                    : editor.caretFingeringLabel());
    }
    CHECK(fingers.join(QString()) == QStringLiteral("1234T-"));
    CHECK(score.undoName() == QStringLiteral("Change fingering"));
    CHECK(editor.caretPluckLabel() == QStringLiteral("p"));
    editor.nextPluck();
    editor.nextPluck();
    CHECK(editor.caretPluckLabel() == QStringLiteral("i"));
    CHECK(failures.isEmpty());

    // What the song refuses is told by the document, as its own edits are.
    score.moveCaret(Move::NextBeat); // the free slot: no note to bend
    editor.setBend(1, 100);
    REQUIRE(failures.size() == 1);
    CHECK(failures.front() == ScoreDocument::refusalText(elara::core::Refusal::NothingAtCaret));

    // The caret is the document's: on another track, the editor reads that one.
    score.addTrack(QStringLiteral("drum-kit"));
    CHECK(editor.caretOnDrums());
    CHECK_FALSE(editor.caretFretted());
}

TEST_CASE("caret editor: an edit the song refuses says why, and changes nothing") {
    using elara::core::NoteEffect;
    using elara::core::Refusal;
    ScoreDocument score;
    CaretEditor editor;
    editor.setDocument(&score);
    score.newSong(QStringLiteral("electric-guitar"));
    QStringList failures;
    QObject::connect(&score, &ScoreDocument::editFailed,
                     [&failures](const QString& message) { failures.append(message); });
    const auto refused = [&failures](Refusal why) {
        const bool said = failures == QStringList{ScoreDocument::refusalText(why)};
        failures.clear();
        return said;
    };

    score.typeDigit(6);
    const QString typed = score.undoName();
    // The first note of the song: nothing before it to tie to.
    editor.toggleEffect(static_cast<int>(NoteEffect::Tie));
    CHECK(refused(Refusal::NoNoteToTie));
    // Fret 6 is near no node of the string.
    editor.toggleEffect(static_cast<int>(NoteEffect::Harmonic));
    CHECK(refused(Refusal::NoNaturalHarmonic));
    editor.setHarmonic(static_cast<int>(elara::core::HarmonicKind::Natural));
    CHECK(refused(Refusal::NoNaturalHarmonic));
    // A drum hit on a guitar.
    editor.toggleDrum();
    CHECK(refused(Refusal::NeedsDrums));
    // A grace note two frets below fret 1 leaves the neck: it comes from above.
    score.typeDigit(1);
    editor.toggleGraceNote();
    CHECK(failures.isEmpty());
    CHECK(editor.caretGraceNotes() == 1);
    score.undo();
    score.undo();
    CHECK(score.undoName() == typed);

    // The free slot after the note, with no note after it.
    score.moveCaret(ScoreDocument::CaretMove::NextBeat);
    editor.toggleEffect(static_cast<int>(NoteEffect::Vibrato));
    CHECK(refused(Refusal::NothingAtCaret));
    editor.setDynamic(0);
    CHECK(refused(Refusal::NothingAtCaret));
    editor.setStrum(1);
    CHECK(refused(Refusal::NothingAtCaret));
    CHECK(score.undoName() == typed);

    // On a drum track: no note names, no strings.
    score.addTrack(QStringLiteral("drum-kit"));
    REQUIRE(editor.caretOnDrums());
    editor.typeNoteName(0, false);
    CHECK(refused(Refusal::NoNoteNames));
    editor.toggleDrum();
    REQUIRE(failures.isEmpty());
    editor.toggleEffect(static_cast<int>(NoteEffect::PalmMute));
    CHECK(refused(Refusal::NeedsStrings));
    editor.setPluck(static_cast<int>(elara::core::Finger::Index));
    CHECK(refused(Refusal::NeedsStrings));
}

TEST_CASE("score document: each refusal has words of its own") {
    QStringList texts;
    for (const elara::core::Refusal why : elara::core::enumValues<elara::core::Refusal>()) {
        const QString text = ScoreDocument::refusalText(why);
        CHECK_FALSE(text.isEmpty());
        CHECK_FALSE(texts.contains(text));
        texts.append(text);
    }
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

// ---- Typing at the caret of each song the window opens ----------------------

TEST_CASE("caret editor: two digits are one fret and one undo step, and a rest goes in and out") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    CaretEditor& editor = song.editor;
    REQUIRE(editor.caretFretted());

    // A click on the caret keeps it.
    const elara::core::Caret before = score.caret();
    const QPointF centre = score.caretRect().center();
    CHECK(score.placeCaret(score.caretPage(), centre.x(), centre.y()));
    CHECK(score.caret() == before);

    // Both keys at one instant: the first digit's layout can outlast the
    // two-digit window on a loaded machine.
    score.moveCaret(Move::StringDown);
    const auto pressed = elara::core::EditSession::Clock::now();
    score.typeDigit(1, pressed);
    score.typeDigit(2, pressed);
    REQUIRE(song.laidOut());
    CHECK(editor.fretAtCaret() == 12);
    CHECK(score.caretPage() >= 0);
    score.undo();
    REQUIRE(song.laidOut());
    CHECK(song.unchanged());
    score.redo();
    REQUIRE(song.laidOut());
    CHECK(editor.fretAtCaret() == 12);

    score.setInputValue(8);
    CHECK(score.inputValue() == 8);
    editor.insertRest();
    REQUIRE(song.laidOut());
    CHECK_FALSE(editor.fretAtCaret().has_value());
    editor.deleteAtCaret();
    REQUIRE(song.laidOut());
    CHECK(editor.fretAtCaret() == 12);
    REQUIRE(song.undoAll());
    CHECK(song.unchanged());
}

TEST_CASE("caret editor: a triplet eighth typed is the caret's beat until the input is plain") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    REQUIRE(song.editor.caretFretted());

    score.setInputValue(8);
    score.setInputTuplet(3);
    CHECK(score.inputTuplet() == 3);
    score.typeDigit(5);
    REQUIRE(song.laidOut());
    const std::optional<elara::core::Duration> typed = caretDuration(score);
    REQUIRE(typed);
    CHECK(typed->tuplet() == elara::core::Tuplet(3, 2));
    CHECK(typed->value() == elara::core::NoteValue::Eighth);

    // The same tuplet again toggles it off, on the caret's beat too.
    score.setInputTuplet(3);
    CHECK(score.inputTuplet() == 0);
    REQUIRE(song.laidOut());
    const std::optional<elara::core::Duration> plain = caretDuration(score);
    REQUIRE(plain);
    CHECK(plain->tuplet().isNone());
    CHECK(score.undoName() == ScoreDocument::tr("Change duration"));
    REQUIRE(song.undoAll());
    CHECK(song.unchanged());
}

TEST_CASE("caret editor: a note name types its note, and the status names it") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    CaretEditor& editor = song.editor;
    REQUIRE(editor.caretPitched());

    editor.typeNoteName(0, false);
    REQUIRE(song.laidOut());
    const std::vector<elara::core::Pitch> typed = editor.pitchesAtCaret();
    REQUIRE(typed.size() == 1);
    CHECK(typed.front().step() == elara::core::Step::C);
    // Before the beat's effects, which a file's beat may have.
    INFO(editor.caretStatus().toStdString());
    CHECK(editor.caretStatus().contains(QStringLiteral("C%1").arg(typed.front().octave())));

    // A semitone up.
    editor.shiftPitch(1);
    REQUIRE(song.laidOut());
    const std::vector<elara::core::Pitch> raised = editor.pitchesAtCaret();
    REQUIRE(raised.size() == 1);
    CHECK(raised.front().midiNumber() == typed.front().midiNumber() + 1);
    REQUIRE(song.undoAll());
    CHECK(song.unchanged());
}

TEST_CASE("caret editor: a note typed in voice 2 goes into it, which the status names") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    CaretEditor& editor = song.editor;
    REQUIRE(editor.caretPitched());
    const QString voice2 = CaretEditor::tr("Voice %1").arg(2);

    score.setCaretVoice(2);
    CHECK(score.caretVoice() == 2);
    CHECK(editor.caretStatus().contains(voice2));
    if (editor.caretFretted()) {
        score.typeDigit(7);
    } else {
        editor.typeNoteName(2, false);
    }
    REQUIRE(song.laidOut());
    CHECK(score.caretVoice() == 2);
    CHECK_FALSE(editor.pitchesAtCaret().empty());

    // Back in voice 1, the status no longer names a voice.
    score.setCaretVoice(1);
    CHECK(score.caretVoice() == 1);
    CHECK_FALSE(editor.caretStatus().contains(voice2));
    REQUIRE(song.undoAll());
    CHECK(song.unchanged());
}

TEST_CASE("caret editor: on a new drum kit track the caret goes down the lanes, naming each") {
    constexpr int kSnareKey = 38;
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    CaretEditor& editor = song.editor;

    score.addTrack(QStringLiteral("drum-kit"));
    REQUIRE(song.laidOut());
    CHECK(editor.caretOnDrums());
    const int lanes = static_cast<int>(elara::core::drumPieces().size());
    for (int lane = 1; lane < lanes; ++lane) {
        score.moveCaret(Move::StringUp);
    }
    CHECK(score.caret().string == 1);
    CHECK(editor.caretStatus().endsWith(CaretEditor::laneName(1)));
    const double top = score.caretRect().top();
    for (int lane = 1; lane < laneOf(kSnareKey); ++lane) {
        score.moveCaret(Move::StringDown);
    }
    CHECK(editor.caretStatus().endsWith(CaretEditor::laneName(laneOf(kSnareKey))));
    CHECK(score.caretRect().top() > top);

    // The snare, on and off again.
    editor.toggleDrum();
    REQUIRE(song.laidOut());
    const std::vector<elara::core::Pitch> hits = editor.pitchesAtCaret();
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().midiNumber() == kSnareKey);
    editor.toggleDrum();
    REQUIRE(song.laidOut());
    CHECK(editor.pitchesAtCaret().empty());
    REQUIRE(song.undoAll());
    CHECK(song.unchanged());
}
