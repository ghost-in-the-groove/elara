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
#include <optional>

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
    void finish(bool ok, const QString& detail);
    void clickCursor();
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
    bool laidOut_ = false;
    int paintsBefore_ = 0;
    int layouts_ = 0;
    qint64 settledAt_ = -1;
    bool queued_ = false;
    QString failure_; ///< set by a step that went wrong
};

} // namespace elara::app
