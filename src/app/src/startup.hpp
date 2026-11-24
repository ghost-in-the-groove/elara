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
    bool smokeTest = false;     ///< check the window (and the file), then quit
    QString screenshot;         ///< save the window here once shown, then quit
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
    void queueMix();
    void queueNavigation();
    void queueTuning();
    void finish(bool ok, const QString& detail);
    void clickCursor();
    void queueKeepPosition();
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
    qint64 settledAt_ = -1;
    bool queued_ = false;
    qsizetype tracksBefore_ = 0;  ///< tracks when the smoke test's edits start
    double durationBefore_ = 0.0; ///< the song's length in seconds before the edits
    /// What playback played before the mix changed.
    std::shared_ptr<const audio::Sequence> sequenceBefore_;
    QVariantMap tuningBefore_;    ///< the first track's tuning before it changed
    QString editProblem_;         ///< the first edit check that failed
    QString failure_;             ///< set by a step that went wrong
    bool entering_ = false;       ///< whether the shown track takes frets
    bool tupleting_ = false;      ///< whether the tuplet steps run
    int measuresBefore_ = 0;      ///< measures when the measure edits start
    int firstNumerator_ = 0;      ///< the first time signature then
    double positionBefore_ = 0.0; ///< playback's position before an edit
    bool pitching_ = false;       ///< whether the caret takes note names
    int typedMidi_ = -1;          ///< the note name typed, as a MIDI number
};

} // namespace elara::app
