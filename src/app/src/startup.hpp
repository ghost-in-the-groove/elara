// SPDX-License-Identifier: AGPL-3.0-or-later
// What the app does after its window loads, from the command line.
#pragma once

#include "caret_editor.hpp"
#include "document_set.hpp"
#include "score_document.hpp"
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickWindow>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <deque>
#include <functional>
#include <memory>
#include <optional>

namespace elara::audio {
class Sequence;
} // namespace elara::audio

namespace elara::app {

struct StartupOptions {
    QUrl file;                ///< opened by the window as it loads: the first of files
    QList<QUrl> files;        ///< every file named, each opened in a song of its own
    std::optional<int> track; ///< a track to show, or ScoreDocument::kAllTracks
    std::optional<ScoreDocument::View> view;
    std::optional<double> seek; ///< seconds to move the transport to, paused
    bool continuous = false;    ///< show the continuous view (ADR-0057)
    std::optional<QSize> size;  ///< the window's size, in place of its own
    bool smokeTest = false;     ///< check the window (and the file), then quit
    bool effects = false;       ///< the note effects palette opens at the caret
    bool commands = false;      ///< the command palette opens at start (`--commands`)
    QString menu;               ///< a menu opens as the window shows (`--menu`)
    bool tuner = false;         ///< the tuner opens at start
    QString screenshot;         ///< save the window here once shown, then quit
    QString soundFont;          ///< a SoundFont the window plays through (`--soundfont`,
                                ///< or the included one with `--included-soundfont`)
    QString guitarSfz;          ///< an SFZ instrument for the guitars (`--guitar-sfz`,
                                ///< or the included one with `--included-guitar-sfz`)
    QString bassSfz;            ///< an SFZ instrument for the basses (`--bass-sfz`)
    /// Where recovery copies go (`--recovery-folder`); the smoke test then
    /// recovers the copy a crash left there.
    QString recoveryFolder;
    /// The smoke test edits a new song and quits as a crash would once its
    /// recovery copy is written (`--crash-after-recovery-copy`).
    bool crashAfterRecoveryCopy = false;
    /// Where the window keeps its state (`--window-state`); the smoke test
    /// then changes it, or finds it as the run before left it (ADR-0137).
    QString windowState;
    /// Once the window is ready, stay open this many seconds doing nothing of
    /// the check's own, then quit (`--hold`): what the app costs meanwhile is
    /// measured from outside (scripts/usage.py).
    std::optional<double> hold;
    /// Start playback once the window is ready (`--play`).
    bool play = false;
    /// The smoke test edits a new song a page long above the caret and checks
    /// that the caret holds still on screen (`--hold-still`).
    bool holdStill = false;
    /// The smoke test moves playback through a new song several pages long
    /// as it would play and checks that the view follows it (`--follow`).
    bool follow = false;
};

/// Prints the outcome of `--smoke-test` ("smoke-test: ok" or "FAILED").
void reportSmokeTest(bool ok, const QString& detail);

/// Drives the window once it has loaded: waits for the file to be laid out
/// and painted, shows the track and view asked for and, for `--smoke-test`
/// and `--screenshot`, checks the outcome and quits. The smoke test also
/// shows the score in every view and with all its tracks, so that every
/// relayout path runs without a screen, moves the playback cursor halfway
/// through the song and clicks on it.
class Startup : public QObject {
public:
    Startup(QQuickWindow* window, StartupOptions options, const bool& qmlWarnings);

    void start();

private:
    void tick();
    void queueSteps();
    void queueEdits();
    void queueEntry();
    void queueTuplets();
    void queueMeasures();
    void queuePitches();
    void queueVoices();
    void queueDrums();
    void queueSelection();
    void queueMix();
    void queueNavigation();
    void queueRepeats();
    void queueTuning();
    void queueTempo();
    void queueSection();
    void queueSymbols();
    void queueSymbolClick();
    void queueMeasureRepeat();
    void queueEffects();
    void queueCommands();
    /// Presses and lets go of `key` as the system would, shortcuts included.
    void pressKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                  const QString& text = {});
    /// Types `text` a key at a time.
    void typeText(const QString& text);
    void queueContinuous();
    void queueLayoutIssues();
    void queueTunerFollowsTrack();
    void finish(bool ok, const QString& detail);
    void clickCursor();
    [[nodiscard]] bool uncoverScore();
    void queueKeepPosition();
    void queueHoldStill();
    void queueFollow();
    void queueParking();
    void queuePdf();
    void queueExport();
    void queuePractice();
    void queueLoop();
    void queueSoundFont();
    void queueSfz();
    void queueRecoveryCopy();
    void queueSave();
    void queueCrashAfterRecoveryCopy();
    void queueRecover();
    void queueWindowState();
    void queueSongs();
    [[nodiscard]] QString overlappingBars() const;
    [[nodiscard]] bool checking() const;

    QPointer<QQuickWindow> window_;
    StartupOptions options_;
    const bool& qmlWarnings_;
    QPointer<ScoreDocument> score_; ///< the song the window opens with, its first
    QPointer<CaretEditor> editor_;  ///< what is at the shown song's caret
    QPointer<DocumentSet> songs_;   ///< every song open (ADR-0160)
    QString openFailure_;           ///< why a file named did not open
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 stepStarted_ = 0;
    std::deque<std::function<bool()>> steps_; ///< each returns whether it changed the score
    bool waiting_ = false;                    ///< for a layout and a paint
    bool again_ = false;     ///< set by a step that must run again on the next tick
    qint64 againSince_ = -1; ///< when that step first asked to run again
    bool laidOut_ = false;
    int paintsBefore_ = 0;
    std::uint64_t layoutsAskedBefore_ = 0; ///< by the step that runs: one waits only for those
    int layouts_ = 0;
    int stepsRun_ = 0;    ///< steps started so far, retries not counted
    QString layoutSteps_; ///< the steps after which each layout came, for the report
    qint64 settledAt_ = -1;
    bool queued_ = false;
    bool played_ = false;         ///< `--play` started playback
    bool held_ = false;           ///< `--hold` has held the window open
    qsizetype tracksBefore_ = 0;  ///< tracks when the smoke test's edits start
    double durationBefore_ = 0.0; ///< the song's length in seconds before the edits
    /// What playback played before the mix changed.
    std::shared_ptr<const audio::Sequence> sequenceBefore_;
    QPointer<QObject> effectsPanel_;       ///< the note effects palette, watched for `--effects`
    bool effectsOpened_ = false;           ///< whether it has opened
    bool menuOpened_ = false;              ///< whether the menu `--menu` names has opened
    bool commandsOpened_ = false;          ///< whether `--commands` opened the command palette
    QPointer<QObject> commandPalette_;     ///< the command palette, for its steps
    bool commanding_ = false;              ///< whether the command palette steps run
    int commandMeasure_ = 0;               ///< the measure the palette was asked to go to
    QString popupProblem_;                 ///< a popup of the caret that left its view
    std::optional<QPointF> cursorRest_;    ///< where the playback cursor stood a tick ago
    QPointer<QObject> trackRow_;           ///< a row of the song panel, which a mix change keeps
    QString trackName_;                    ///< the first track's name before it was renamed
    QVariantMap tuningBefore_;             ///< the first track's tuning before it changed
    QVariantMap repeatsBefore_;            ///< the first measure's repeats before queueRepeats()
    QVariantMap tempoBefore_;              ///< the second measure's tempo before it changed
    QString sectionBefore_;                ///< the first measure's section before it changed
    double heldY_ = 0.0;                   ///< the caret's height on screen before an edit
    double heldTop_ = 0.0;                 ///< its staff's, in staff spaces down the pages
    double heldScroll_ = 0.0;              ///< the view's scroll position before an edit
    double heldZoom_ = 0.0;                ///< and its zoom
    QString heldBy_;                       ///< what the document last held still, if anything
    double followSeconds_ = 0.0;           ///< where `--follow` moved playback to
    int followTicks_ = 0;                  ///< ticks it has moved playback since
    double followScroll_ = 0.0;            ///< the view's scroll once scrolled by hand
    qint64 followWheelAt_ = 0;             ///< when it was scrolled by hand
    int heldMeasure_ = 0;                  ///< the caret's measure before an edit
    int insertions_ = 0;                   ///< measures inserted to take the caret on
    QString editProblem_;                  ///< the first edit check that failed
    QString failure_;                      ///< set by a step that went wrong
    bool entering_ = false;                ///< whether the shown track takes frets
    bool tupleting_ = false;               ///< whether the tuplet steps run
    int measuresBefore_ = 0;               ///< measures when the measure edits start
    int firstNumerator_ = 0;               ///< the first time signature then
    double positionBefore_ = 0.0;          ///< playback's position before an edit
    bool pitching_ = false;                ///< whether the caret takes note names
    int typedMidi_ = -1;                   ///< the note name typed, as a MIDI number
    bool voicing_ = false;                 ///< whether the voice steps run
    bool drumming_ = false;                ///< whether the drum steps run
    bool selecting_ = false;               ///< whether the selection steps run
    int lengthBefore_ = 0;                 ///< the song's length before pasting
    int pasteAt_ = 0;                      ///< the measure pasted at
    bool effecting_ = false;               ///< whether the note effect steps run
    int effectsBefore_ = 0;                ///< the caret's effects before they were toggled
    int pagesBefore_ = 0;                  ///< pages before the continuous view was shown
    bool recoveryWritten_ = false;         ///< ScoreDocument::recoveryWritten() came
    std::optional<QTemporaryDir> scratch_; ///< where checks save and export, made once
    std::optional<bool> saved_;            ///< what ScoreDocument::saveFinished() last said
    std::optional<bool> exported_;         ///< what ScoreDocument::exportFinished() last said
};

} // namespace elara::app
