// SPDX-License-Identifier: AGPL-3.0-or-later
// What the app does after its window loads, from the command line.
#pragma once

#include "score_document.hpp"
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QString>
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
    QUrl file;                ///< opened by the window as it loads
    std::optional<int> track; ///< a track to show, or ScoreDocument::kAllTracks
    std::optional<ScoreDocument::View> view;
    std::optional<double> seek; ///< seconds to move the transport to, paused
    bool continuous = false;    ///< show the continuous view (ADR-0057)
    bool smokeTest = false;     ///< check the window (and the file), then quit
    bool effects = false;       ///< the note effects panel opens at the caret
    QString screenshot;         ///< save the window here once shown, then quit
    QString soundFont;          ///< a SoundFont the window plays through (`--soundfont`,
                                ///< or the included one with `--included-soundfont`)
    QString guitarSfz;          ///< an SFZ instrument for the guitars (`--guitar-sfz`,
                                ///< or the included one with `--included-guitar-sfz`)
    QString bassSfz;            ///< an SFZ instrument for the basses (`--bass-sfz`)
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
    void queueEffects();
    void queueContinuous();
    void finish(bool ok, const QString& detail);
    void clickCursor();
    void queueKeepPosition();
    void queuePractice();
    void queueLoop();
    void queueSoundFont();
    void queueSfz();
    [[nodiscard]] QString overlappingBars() const;
    [[nodiscard]] bool checking() const;

    QPointer<QQuickWindow> window_;
    StartupOptions options_;
    const bool& qmlWarnings_;
    QPointer<ScoreDocument> score_;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 stepStarted_ = 0;
    std::deque<std::function<bool()>> steps_; ///< each returns whether it changed the score
    bool waiting_ = false;                    ///< for a layout and a paint
    bool again_ = false;     ///< set by a step that must run again on the next tick
    qint64 againSince_ = -1; ///< when that step first asked to run again
    bool laidOut_ = false;
    int paintsBefore_ = 0;
    int layouts_ = 0;
    int stepsRun_ = 0;    ///< steps started so far, retries not counted
    QString layoutSteps_; ///< the steps after which each layout came, for the report
    qint64 settledAt_ = -1;
    bool queued_ = false;
    qsizetype tracksBefore_ = 0;  ///< tracks when the smoke test's edits start
    double durationBefore_ = 0.0; ///< the song's length in seconds before the edits
    /// What playback played before the mix changed.
    std::shared_ptr<const audio::Sequence> sequenceBefore_;
    QPointer<QObject> effectsPanel_; ///< the note effects panel, watched for `--effects`
    bool effectsOpened_ = false;     ///< whether it has opened
    QPointer<QObject> trackRow_;     ///< a row of the song panel, which a mix change keeps
    QVariantMap tuningBefore_;       ///< the first track's tuning before it changed
    QVariantMap repeatsBefore_;      ///< the first measure's repeats before queueRepeats()
    QVariantMap tempoBefore_;        ///< the second measure's tempo before it changed
    QString sectionBefore_;          ///< the first measure's section before it changed
    QString editProblem_;            ///< the first edit check that failed
    QString failure_;                ///< set by a step that went wrong
    bool entering_ = false;          ///< whether the shown track takes frets
    bool tupleting_ = false;         ///< whether the tuplet steps run
    int measuresBefore_ = 0;         ///< measures when the measure edits start
    int firstNumerator_ = 0;         ///< the first time signature then
    double positionBefore_ = 0.0;    ///< playback's position before an edit
    bool pitching_ = false;          ///< whether the caret takes note names
    int typedMidi_ = -1;             ///< the note name typed, as a MIDI number
    bool voicing_ = false;           ///< whether the voice steps run
    bool drumming_ = false;          ///< whether the drum steps run
    bool selecting_ = false;         ///< whether the selection steps run
    int lengthBefore_ = 0;           ///< the song's length before pasting
    int pasteAt_ = 0;                ///< the measure pasted at
    bool effecting_ = false;         ///< whether the note effect steps run
    int effectsBefore_ = 0;          ///< the caret's effects before they were toggled
    int pagesBefore_ = 0;            ///< pages before the continuous view was shown
};

} // namespace elara::app
