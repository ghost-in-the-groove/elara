// SPDX-License-Identifier: AGPL-3.0-or-later
#include "startup.hpp"

#include "audio_controller.hpp"
#include "score_page.hpp"
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QQuickItem>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace elara::app {

namespace {

// How long one step may take to be laid out and painted.
constexpr qint64 kStepTimeoutMilliseconds = 30000;
// How long a screenshot waits for animations once the window is ready.
constexpr qint64 kSettleMilliseconds = 900;
constexpr int kTickMilliseconds = 30;

// Pages brought up to date so far: painted, or kept because a relayout left
// them as they were.
int pagesUpdated() {
    return ScorePage::paintCount() + ScorePage::keptCount();
}

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
    paintsBefore_ = pagesUpdated();
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
            queueEntry();
            queueTuplets();
            queueMeasures();
            queueMix();
            queueTuning();
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
        queueEntry();
        queueTuplets();
        queueMeasures();
        queueMix();
        queueTuning();
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

// Keyboard entry on the first fretted track shown: a click on the caret keeps
// it, two digits make one fret as one undo step, a duration, a rest inserted
// and deleted; then everything is undone and the song must end as it started.
void Startup::queueEntry() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("entry check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        entering_ = score_->caretFretted() && score_->caretPage() >= 0;
        if (!entering_) {
            return false;
        }
        const core::Caret before = score_->caret();
        const QPointF centre = score_->caretRect().center();
        check(score_->placeCaret(score_->caretPage(), centre.x(), centre.y()) &&
                  score_->caret() == before,
              "a click on the caret moved it");
        score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        score_->typeDigit(1);
        score_->typeDigit(2);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(score_->fretAtCaret() == 12, "two digits did not type fret 12");
        check(score_->caretPage() >= 0, "the caret went away after typing");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(!score_->modified(), "a two-digit fret is not one undo step");
        score_->redo();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(score_->fretAtCaret() == 12, "redo did not bring the fret back");
        score_->setInputValue(8);
        check(score_->inputValue() == 8, "the input duration did not change");
        score_->insertRest();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(!score_->fretAtCaret().has_value(), "the caret is not on the inserted rest");
        score_->deleteAtCaret();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(score_->fretAtCaret() == 12, "deleting the rest did not bring the note back");
        for (int i = 0; i < 8 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(!score_->modified(), "undoing the entry did not restore the song");
        return false;
    });
}

// Tuplets at the caret (ADR-0050): a triplet eighth typed, which the caret's
// beat takes and keeps when the caret comes back to it, then the input back to
// plain eighths. Undone, the song must end as it started.
void Startup::queueTuplets() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("tuplet check failed: %1").arg(QLatin1StringView(what));
        }
    };
    const auto caretDuration = [this] {
        const core::Caret caret = score_->caret();
        const core::Voice& voice = score_->song()
                                       ->track(caret.track)
                                       .staff(caret.staff)
                                       .measure(caret.measure)
                                       .voices.at(caret.voice);
        return caret.beat < voice.beats.size() ? std::optional(voice.beats[caret.beat].duration)
                                               : std::nullopt;
    };
    steps_.emplace_back([this, check] {
        tupleting_ = score_->caretPage() >= 0 && score_->caretFretted();
        if (!tupleting_) {
            return false;
        }
        score_->setInputValue(8);
        score_->setInputTuplet(3);
        check(score_->inputTuplet() == 3, "the input is not a triplet");
        score_->typeDigit(5);
        return true;
    });
    steps_.emplace_back([this, check, caretDuration] {
        if (!tupleting_) {
            return false;
        }
        const std::optional<core::Duration> typed = caretDuration();
        check(typed && typed->tuplet() == core::Tuplet(3, 2) &&
                  typed->value() == core::NoteValue::Eighth,
              "the beat is not a triplet eighth");
        score_->setInputTuplet(3);
        check(score_->inputTuplet() == 0, "the triplet did not toggle off");
        return true;
    });
    steps_.emplace_back([this, check, caretDuration] {
        if (!tupleting_) {
            return false;
        }
        const std::optional<core::Duration> plain = caretDuration();
        check(plain && plain->tuplet().isNone(), "the beat kept its triplet");
        check(score_->undoName() == ScoreDocument::tr("Change duration"),
              "the tuplet is not an undo step");
        for (int i = 0; i < 6 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!tupleting_) {
            return false;
        }
        check(!score_->modified(), "undoing the tuplets did not restore the song");
        return false;
    });
}

// Measures at the caret: one inserted after it, given its own time and key
// signatures, deleted again; then everything is undone.
void Startup::queueMeasures() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("measure check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        if (score_->caretPage() < 0) {
            return false;
        }
        measuresBefore_ = score_->measureCount();
        firstNumerator_ = score_->timeNumerator();
        score_->insertMeasure(true);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (measuresBefore_ == 0) {
            return false;
        }
        check(score_->measureCount() == measuresBefore_ + 1, "the measure was not inserted");
        check(score_->caretMeasure() == 1, "the caret is not on the new measure");
        score_->setTimeSignatureFrom(score_->caretMeasure(), 3, 4);
        score_->setKeySignatureFrom(score_->caretMeasure(), 2, false);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (measuresBefore_ == 0) {
            return false;
        }
        check(score_->caretTimeNumerator() == 3 && score_->caretTimeDenominator() == 4,
              "the time signature did not change at the caret");
        check(score_->caretKeyFifths() == 2, "the key signature did not change at the caret");
        check(score_->timeNumerator() == firstNumerator_, "the first measure changed");
        score_->removeMeasure();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (measuresBefore_ == 0) {
            return false;
        }
        check(score_->measureCount() == measuresBefore_, "the measure was not deleted");
        for (int i = 0; i < 8 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (measuresBefore_ == 0) {
            return false;
        }
        check(!score_->modified() && score_->measureCount() == measuresBefore_,
              "undoing the measure edits did not restore the song");
        return false;
    });
}

// The mixer (ADR-0039): a fader previewed without changing the song, then the
// first track's mix set, soloed, as one undo step that keeps what playback
// plays and silences the other tracks. Undone, the song must end as it started.
void Startup::queueMix() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("mix check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        sequenceBefore_ = score_->sequence();
        const auto tracks = static_cast<std::size_t>(score_->tracks().size());
        score_->previewTrackMix(0, 0.25, 0.5);
        check(score_->previewMixes().size() == tracks &&
                  score_->previewMixes().front().volume == 0.25 && !score_->modified(),
              "the fader preview changed the song");
        score_->setTrackMix(0, 0.25, -0.5, false, true);
        const QVariantMap first = score_->tracks().front().toMap();
        check(first.value(QStringLiteral("volume")).toDouble() == 0.25 &&
                  first.value(QStringLiteral("pan")).toDouble() == -0.5 &&
                  first.value(QStringLiteral("solo")).toBool(),
              "the mix was not set");
        check(score_->undoName() == ScoreDocument::tr("Change mix"), "the mix is not undoable");
        check(score_->sequence() == sequenceBefore_, "a mix change stops playback");
        return true;
    });
    steps_.emplace_back([this, check] {
        const QVariantList tracks = score_->tracks();
        check(tracks.size() < 2 || !tracks.at(1).toMap().value(QStringLiteral("audible")).toBool(),
              "the solo did not silence the other tracks");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() &&
                  !score_->tracks().front().toMap().value(QStringLiteral("solo")).toBool(),
              "undoing the mix did not restore the song");
        sequenceBefore_.reset();
        return false;
    });
}

// The first track's tuning from the song panel (ADR-0046): a capo a fret
// higher keeping the frets, then the capo taken off keeping the sound, each
// one undo step; both undone, the song must end as it started.
void Startup::queueTuning() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("tuning check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        tuningBefore_ = score_->trackTuning(0);
        if (!tuningBefore_.value(QStringLiteral("fretted")).toBool()) {
            return false;
        }
        const QString notes = tuningBefore_.value(QStringLiteral("notes")).toString();
        const int capo = tuningBefore_.value(QStringLiteral("capo")).toInt() + 1;
        score_->setTrackTuning(0, notes, capo, true);
        const QVariantMap tuning = score_->trackTuning(0);
        check(tuning.value(QStringLiteral("notes")).toString() == notes &&
                  tuning.value(QStringLiteral("capo")).toInt() == capo,
              "the capo was not moved");
        const bool changed = score_->undoName() == ScoreDocument::tr("Change tuning");
        check(changed, "the tuning is not one undo step");
        return changed;
    });
    steps_.emplace_back([this, check] {
        if (!tuningBefore_.value(QStringLiteral("fretted")).toBool()) {
            return false;
        }
        // By its preset when it has one, else by its notes.
        const QString notes = tuningBefore_.value(QStringLiteral("notes")).toString();
        const QString preset = tuningBefore_.value(QStringLiteral("preset")).toString();
        score_->setTrackTuning(0, preset.isEmpty() ? notes : preset, 0, false);
        const QVariantMap tuning = score_->trackTuning(0);
        const bool changed = tuning.value(QStringLiteral("notes")).toString() == notes &&
                             tuning.value(QStringLiteral("capo")).toInt() == 0;
        check(changed, "the capo was not taken off");
        if (!changed) {
            return false;
        }
        score_->undo();
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!tuningBefore_.value(QStringLiteral("fretted")).toBool()) {
            return false;
        }
        check(!score_->modified() && score_->trackTuning(0) == tuningBefore_,
              "undoing the tunings did not restore the song");
        return false;
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
                             (score_->pageCount() == 0 || pagesUpdated() > paintsBefore_);
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
        auto step = std::move(steps_.front());
        steps_.pop_front();
        laidOut_ = false;
        paintsBefore_ = pagesUpdated();
        const bool changed = step();
        if (!failure_.isEmpty()) {
            finish(false, failure_);
            return;
        }
        if (again_) {
            again_ = false;
            if (againSince_ < 0) {
                againSince_ = clock_.elapsed();
            } else if (clock_.elapsed() - againSince_ > kStepTimeoutMilliseconds) {
                finish(false, QStringLiteral("timed out"));
                return;
            }
            steps_.push_front(std::move(step));
            return;
        }
        againSince_ = -1;
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
    if (const QString overlap = overlappingBars(); !overlap.isEmpty()) {
        finish(false, overlap);
        return;
    }
    QString detail;
    if (score_ != nullptr && score_->pageCount() > 0) {
        std::printf("pages kept: %d\n", ScorePage::keptCount());
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
    // Nothing may cover the cursor, as the measure popup open at the caret can:
    // it closes first, and the click waits until it is gone.
    if (auto* popup = window_->findChild<QObject*>(QStringLiteral("measurePopup"));
        popup != nullptr && popup->property("visible").toBool()) {
        QMetaObject::invokeMethod(popup, "close");
        again_ = true;
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

// The bars that float over the bottom of the score, the transport and the
// note entry bar shown with the caret, must not cover each other.
QString Startup::overlappingBars() const {
    auto* transport = window_->findChild<QQuickItem*>(QStringLiteral("transport"));
    auto* entry = window_->findChild<QQuickItem*>(QStringLiteral("entryBar"));
    if (transport == nullptr || entry == nullptr) {
        return QStringLiteral("no transport or note entry bar");
    }
    if (!transport->isVisible() || !entry->isVisible()) {
        return {};
    }
    const auto sceneRect = [](const QQuickItem* item) {
        return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
    };
    if (sceneRect(transport).intersects(sceneRect(entry))) {
        return QStringLiteral("the note entry bar overlaps the transport");
    }
    return {};
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
