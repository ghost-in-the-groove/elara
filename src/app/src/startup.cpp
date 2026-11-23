// SPDX-License-Identifier: AGPL-3.0-or-later
#include "startup.hpp"

#include "audio_controller.hpp"
#include "score_page.hpp"
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQuickItem>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace elara::app {

namespace {

// How long one step may take to be laid out and painted.
constexpr qint64 kStepTimeoutMilliseconds = 30000;
// How long a screenshot waits for animations once the window is ready.
constexpr qint64 kSettleMilliseconds = 900;
constexpr int kTickMilliseconds = 30;

} // namespace

void reportSmokeTest(bool ok, const QString& detail) {
    std::fprintf(ok ? stdout : stderr, "smoke-test: %s%s\n", ok ? "ok" : "FAILED",
                 detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
}

Startup::Startup(QQuickWindow* window, StartupOptions options, const bool& qmlWarnings)
    : QObject(window), window_(window), options_(std::move(options)), qmlWarnings_(qmlWarnings) {
    timer_.setInterval(kTickMilliseconds);
    connect(&timer_, &QTimer::timeout, this, &Startup::tick);
}

void Startup::start() {
    score_ = window_ != nullptr ? window_->findChild<ScoreDocument*>() : nullptr;
    if (score_ != nullptr) {
        connect(score_, &ScoreDocument::layoutChanged, this, [this] {
            laidOut_ = true;
            ++layouts_;
        });
    }
    clock_.start();
    // The window opens the file itself; wait for its first layout and paint.
    waiting_ = !options_.file.isEmpty();
    paintsBefore_ = ScorePage::paintCount();
    timer_.start();
}

bool Startup::checking() const {
    return options_.smokeTest || !options_.screenshot.isEmpty();
}

void Startup::queueSteps() {
    using View = ScoreDocument::View;
    if (score_ == nullptr) {
        return;
    }
    if (options_.file.isEmpty()) {
        // Without a file the smoke test edits a new song.
        if (options_.smokeTest) {
            steps_.emplace_back([this] {
                score_->newSong(QStringLiteral("electric-guitar"));
                return true;
            });
            queueEdits();
        }
        return;
    }
    if (options_.smokeTest) {
        for (const View view : std::array{View::Standard, View::Tablature, View::Both}) {
            steps_.emplace_back([this, view] {
                if (score_->view() == view || (view != View::Standard && !score_->hasTablature())) {
                    return false;
                }
                score_->setView(view);
                return true;
            });
        }
        steps_.emplace_back([this] {
            if (score_->trackNames().size() < 2) {
                return false;
            }
            score_->setTrack(ScoreDocument::kAllTracks);
            return true;
        });
        steps_.emplace_back([this] {
            if (score_->track() == 0) {
                return false;
            }
            score_->setTrack(0);
            return true;
        });
        queueEdits();
    }
    // Moving the transport shows the cursor; it needs no relayout. The device
    // stays closed while paused.
    const auto seekTo = [this](double seconds) {
        steps_.emplace_back([this, seconds] {
            auto* audio = window_ != nullptr ? window_->findChild<AudioController*>() : nullptr;
            if (audio != nullptr) {
                audio->seek(seconds);
            }
            return false;
        });
    };
    if (options_.smokeTest) {
        seekTo(score_->duration() / 2.0);
        steps_.emplace_back([this] {
            clickCursor();
            return false;
        });
    }
    if (options_.track) {
        steps_.emplace_back([this, track = *options_.track] {
            const int before = score_->track();
            score_->setTrack(track);
            return score_->track() != before;
        });
    }
    if (options_.view) {
        steps_.emplace_back([this, view = *options_.view] {
            if (score_->view() == view) {
                return false;
            }
            score_->setView(view);
            return true;
        });
    }
    if (options_.seek) {
        seekTo(*options_.seek);
    }
}

// Edits, undone and redone: a track added and shown, the time signature
// changed, both undone, the track redone and undone again. Each step is laid
// out and painted; the song must end as it started.
void Startup::queueEdits() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("edit check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        tracksBefore_ = score_->trackNames().size();
        score_->addTrack(QStringLiteral("electric-bass"));
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->trackNames().size() == tracksBefore_ + 1, "the track was not added");
        check(score_->track() == tracksBefore_, "the new track is not shown");
        durationBefore_ = score_->duration();
        score_->setTimeSignature(5, 8);
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->timeNumerator() == 5 && score_->timeDenominator() == 8,
              "the time signature did not change");
        check(score_->duration() != durationBefore_, "playback did not follow the edit");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this] {
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->trackNames().size() == tracksBefore_, "undo left the track");
        check(score_->timeNumerator() != 5 && !score_->canUndo() && score_->canRedo(),
              "undo did not restore the song");
        score_->redo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->trackNames().size() == tracksBefore_ + 1, "redo did not add the track");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->trackNames().size() == tracksBefore_ && !score_->modified(),
              "the song did not end as it started");
        if (score_->track() == 0) {
            return false;
        }
        score_->setTrack(0);
        return true;
    });
}

void Startup::tick() {
    if (!options_.file.isEmpty() && score_ != nullptr &&
        score_->status() == ScoreDocument::Status::Error) {
        finish(false, score_->errorMessage());
        return;
    }
    if (waiting_) {
        const bool settled = score_ != nullptr && laidOut_ && !score_->busy() &&
                             score_->status() == ScoreDocument::Status::Ready &&
                             (score_->pageCount() == 0 || ScorePage::paintCount() > paintsBefore_);
        if (!settled) {
            if (clock_.elapsed() - stepStarted_ > kStepTimeoutMilliseconds) {
                finish(false, QStringLiteral("timed out"));
            }
            return;
        }
        waiting_ = false;
    }
    if (!queued_) {
        queued_ = true;
        queueSteps();
    }
    while (!steps_.empty()) {
        const auto step = std::move(steps_.front());
        steps_.pop_front();
        laidOut_ = false;
        paintsBefore_ = ScorePage::paintCount();
        const bool changed = step();
        if (!failure_.isEmpty()) {
            finish(false, failure_);
            return;
        }
        if (changed) {
            waiting_ = true;
            stepStarted_ = clock_.elapsed();
            return;
        }
    }
    if (!checking()) {
        timer_.stop();
        return;
    }
    if (!options_.screenshot.isEmpty()) {
        // Let animations settle first.
        if (settledAt_ < 0) {
            settledAt_ = clock_.elapsed();
        }
        if (clock_.elapsed() - settledAt_ < kSettleMilliseconds) {
            return;
        }
    }
    if (!editProblem_.isEmpty()) {
        finish(false, editProblem_);
        return;
    }
    QString detail;
    if (score_ != nullptr && score_->pageCount() > 0) {
        detail = QStringLiteral("%1 pages, %2 painted, %3 layouts")
                     .arg(score_->pageCount())
                     .arg(ScorePage::paintCount())
                     .arg(layouts_);
    }
    finish(true, detail);
}

// Clicks in the middle of the playback cursor, as a user would on the score:
// playback must move to where ScoreDocument::secondsAt puts that point, which
// checks that the cursor is drawn where the pages say and that the click is
// read back in the same coordinates.
void Startup::clickCursor() {
    auto* audio = window_->findChild<AudioController*>();
    auto* playhead = window_->findChild<QQuickItem*>(QStringLiteral("playhead"));
    if (audio == nullptr || playhead == nullptr || !playhead->isVisible()) {
        failure_ = QStringLiteral("no playback cursor");
        return;
    }
    const QVariantMap cursor = score_->cursorAt(audio->position());
    const double expected = score_->secondsAt(
        cursor.value(QStringLiteral("page")).toInt(), cursor.value(QStringLiteral("x")).toDouble(),
        cursor.value(QStringLiteral("y")).toDouble() +
            cursor.value(QStringLiteral("height")).toDouble() / 2.0);
    const QPointF at = playhead->mapToScene(QPointF(playhead->width() / 2, playhead->height() / 2));
    for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, at, at, window_->mapToGlobal(at), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(window_, &event);
    }
    if (expected < 0.0 || std::abs(audio->position() - expected) > 1e-6) {
        failure_ = QStringLiteral("a click on the cursor moved playback to %1 s, not %2 s")
                       .arg(audio->position())
                       .arg(expected);
    }
}

void Startup::finish(bool ok, const QString& detail) {
    timer_.stop();
    QString outcome = detail;
    if (ok && qmlWarnings_) {
        ok = false;
        outcome = QStringLiteral("QML warnings");
    }
    if (ok && !options_.screenshot.isEmpty() && window_ != nullptr) {
        const QImage image = window_->grabWindow();
        if (image.isNull() || !image.save(options_.screenshot)) {
            ok = false;
            outcome = QStringLiteral("could not save %1").arg(options_.screenshot);
        }
    }
    reportSmokeTest(ok, outcome);
    QCoreApplication::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

} // namespace elara::app
