// SPDX-License-Identifier: AGPL-3.0-or-later
#include "startup.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>
#include <elara/notation/display_list.hpp>

#include "audio_controller.hpp"
#include "quit_as_crash.hpp"
#include "recent_scores.hpp"
#include "score_page.hpp"
#include "tuner_controller.hpp"
#include "window_state.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QMouseEvent>
#include <QPointF>
#include <QQuickItem>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <utility>

// Types a key into a window as the system does, shortcuts included: what Qt's
// own tests type with (QTest::keyClick), exported by Qt GUI.
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow* window, QEvent::Type type, int key,
                                    Qt::KeyboardModifiers modifiers, const QString& text,
                                    bool autorepeat, ushort count);

namespace elara::app {

namespace {

// How long one step may take to be laid out and painted.
constexpr qint64 kStepTimeoutMilliseconds = 30000;
// How long a screenshot waits for animations once the window is ready.
constexpr qint64 kSettleMilliseconds = 900;
constexpr int kTickMilliseconds = 30;
constexpr int kSnareKey = 38;
// The title of the song app.smoke_recovery_crash leaves a copy of.
constexpr char kCrashedTitle[] = "Left by a crash";

// The drum lane (1 at the top) of the kit piece with General MIDI `key`.
int laneOf(int key) {
    const auto pieces = core::drumPieces();
    const auto found = std::ranges::find(pieces, key, &core::DrumPiece::key);
    return static_cast<int>(found - pieces.begin()) + 1;
}

// Pages brought up to date so far: painted, or kept because a relayout left
// them as they were.
int pagesUpdated() {
    return ScorePage::paintCount() + ScorePage::keptCount();
}

// The highlight of the symbol under the pointer on page `index`, searching the
// items `item` shows: it lies over that page's paper, whose coordinates it
// shares. Null while the view has not made that page.
QQuickItem* symbolHighlight(QQuickItem* item, int index) {
    if (item == nullptr) {
        return nullptr;
    }
    if (item->objectName() == QLatin1StringView("symbolHighlight")) {
        // The page's delegate, some items above, says which page it is.
        for (const QQuickItem* above = item->parentItem(); above != nullptr;
             above = above->parentItem()) {
            if (const QVariant page = above->property("index"); page.isValid()) {
                return page.toInt() == index ? item : nullptr;
            }
        }
        return nullptr;
    }
    for (QQuickItem* child : item->childItems()) {
        if (QQuickItem* found = symbolHighlight(child, index)) {
            return found;
        }
    }
    return nullptr;
}

// The first item named `name` under `item`, searching the items it shows: a
// Repeater's delegates are there, not among its QObject children.
QQuickItem* findItem(QQuickItem* item, const QString& name) {
    if (item == nullptr || item->objectName() == name) {
        return item;
    }
    for (QQuickItem* child : item->childItems()) {
        if (QQuickItem* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
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
    // Resized once it is on its screen: a size given as the window is created
    // is multiplied when it opens on a screen of another scale.
    if (options_.size && window_ != nullptr) {
        window_->resize(*options_.size);
    }
    score_ = window_ != nullptr ? window_->findChild<ScoreDocument*>() : nullptr;
    editor_ = window_ != nullptr ? window_->findChild<CaretEditor*>() : nullptr;
    if (score_ != nullptr) {
        connect(score_, &ScoreDocument::layoutChanged, this, [this] {
            laidOut_ = true;
            ++layouts_;
            layoutSteps_ += QStringLiteral(" %1").arg(stepsRun_);
        });
        connect(score_, &ScoreDocument::recoveryWritten, this, [this] { recoveryWritten_ = true; });
        connect(score_, &ScoreDocument::saveFinished, this,
                [this](const QUrl& /*file*/, bool saved) { saved_ = saved; });
        connect(score_, &ScoreDocument::exportFinished, this,
                [this](const QUrl& /*file*/, bool written) { exported_ = written; });
    }
    if (options_.effects && window_ != nullptr) {
        effectsPanel_ = window_->findChild<QObject*>(QStringLiteral("effectsPanel"));
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
        if (options_.smokeTest && options_.crashAfterRecoveryCopy) {
            queueCrashAfterRecoveryCopy();
            return;
        }
        if (options_.smokeTest && !options_.recoveryFolder.isEmpty()) {
            queueRecover();
            return;
        }
        if (options_.smokeTest && !options_.windowState.isEmpty()) {
            queueWindowState();
            return;
        }
        // Without a file the smoke test edits a new song.
        if (options_.smokeTest) {
            steps_.emplace_back([this] {
                score_->newSong(QStringLiteral("electric-guitar"));
                return true;
            });
            queueEdits();
            queueRecoveryCopy();
            queueSave();
            queueEntry();
            queueTuplets();
            queueMeasures();
            queuePitches();
            queueVoices();
            queueDrums();
            queueSelection();
            queueMix();
            queueNavigation();
            queueRepeats();
            queueTuning();
            queueTempo();
            queueSection();
            queueSymbols();
            queueMeasureRepeat();
            queueEffects();
            queueCommands();
            queueContinuous();
        }
        return;
    }
    if (options_.smokeTest && options_.tuner) {
        queueTunerFollowsTrack();
    }
    if (options_.smokeTest) {
        // The song opened with its page across the view, at most at the
        // reading zoom (ADR-0133).
        steps_.emplace_back([this] {
            auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
            if (view == nullptr) {
                failure_ = QStringLiteral("no score view");
                return false;
            }
            if (view->property("settling").toBool()) {
                again_ = true;
                return false;
            }
            double across = 0.0;
            QMetaObject::invokeMethod(view, "widthZoom", Q_RETURN_ARG(double, across));
            const double expected = std::min(view->property("readingZoom").toDouble(), across);
            const double zoom = view->property("zoom").toDouble();
            if (expected <= 0.0 || std::abs(zoom - expected) > 1e-6) {
                failure_ = QStringLiteral("the song opened at %1 %, not %2 %")
                               .arg(zoom * 100.0)
                               .arg(expected * 100.0);
            }
            return false;
        });
        // The file opened went to the top of the recent scores (kept only in
        // memory during a check), and the welcome pane's list shows it.
        steps_.emplace_back([this] {
            const auto* recent = window_->findChild<RecentScores*>();
            QStringList names;
            for (const QVariant& entry : recent != nullptr ? recent->scores() : QVariantList{}) {
                const QVariantMap map = entry.toMap();
                names.append(
                    QStringLiteral("%1 (%2)").arg(map.value(QStringLiteral("name")).toString(),
                                                  map.value(QStringLiteral("format")).toString()));
            }
            std::printf("recent: %s\n", qPrintable(names.join(QStringLiteral(", "))));
            // What opening the file changed or left out stays with it.
            std::printf("import: %lld warnings\n",
                        static_cast<long long>(score_->importWarnings().size()));
            return false;
        });
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
        queueContinuous();
        queueEdits();
        queueEntry();
        queueTuplets();
        queueMeasures();
        queuePitches();
        queueVoices();
        queueDrums();
        queueSelection();
        queueMix();
        queueNavigation();
        queueRepeats();
        queueTuning();
        queueTempo();
        queueSection();
        queueSymbols();
        queueMeasureRepeat();
        queueEffects();
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
        queueSymbolClick();
        seekTo(score_->duration() / 2.0);
        steps_.emplace_back([this] {
            clickCursor();
            return false;
        });
        queuePdf();
        queueExport();
        queuePractice();
        queueLoop();
        if (!options_.soundFont.isEmpty()) {
            queueSoundFont();
        }
        if (!options_.guitarSfz.isEmpty() || !options_.bassSfz.isEmpty()) {
            queueSfz();
        }
        queueKeepPosition();
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
    if (options_.continuous) {
        steps_.emplace_back([this] {
            if (score_->continuous()) {
                return false;
            }
            score_->setContinuous(true);
            return true;
        });
    }
    if (options_.seek) {
        seekTo(*options_.seek);
    }
}

// The continuous view (ADR-0057): the pages give way to strips of one endless
// page, no higher than a page, stacked without gaps, then come back as they
// were.
void Startup::queueContinuous() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("continuous view check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        pagesBefore_ = score_->pageCount();
        check(!score_->continuous() && !score_->showsStrips(), "the score starts in strips");
        score_->setContinuous(true);
        check(score_->continuous(), "the continuous view was not asked for");
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->showsStrips(), "the score is not shown in strips");
        const QList<qreal> tops = score_->pageTops();
        check(tops.size() == score_->pageCount() + 1 && tops.front() == 0.0,
              "the strips' tops do not match them");
        const double height = score_->pageSize().height();
        for (qsizetype s = 1; s < tops.size(); ++s) {
            check(tops[s] > tops[s - 1] && tops[s] - tops[s - 1] <= height + 1e-6,
                  "a strip is empty or higher than a page");
        }
        score_->setContinuous(false);
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->showsStrips() && score_->pageCount() == pagesBefore_,
              "the pages did not come back");
        const QList<qreal> tops = score_->pageTops();
        const double height = score_->pageSize().height();
        check(tops.size() == pagesBefore_ + 1 &&
                  std::abs(tops.back() - height * pagesBefore_) < 1e-6,
              "the pages are not a page high each");
        return false;
    });
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
    // A line of the credits (ADR-0059) is one edit, and undo takes it back.
    steps_.emplace_back([this, check] {
        const std::uint64_t sequences = score_->sequenceWork().asked;
        score_->setCredit(static_cast<int>(core::Credit::Subtitle),
                          QStringLiteral("Smoke credits"));
        // Drawn, but not heard (ADR-0111).
        check(score_->sequenceWork().asked == sequences, "a credit was sequenced");
        return true;
    });
    steps_.emplace_back([this, check] {
        const QStringList credits = score_->credits();
        check(credits.size() == static_cast<int>(core::Credit::Notes) + 1 &&
                  credits.at(static_cast<int>(core::Credit::Subtitle)) ==
                      QStringLiteral("Smoke credits"),
              "the subtitle did not change");
        check(score_->undoName() == ScoreDocument::tr("Edit song info"),
              "the credit is not one edit");
        score_->undo();
        return true;
    });
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

// The song's recovery copy (ADR-0106): written once the song has unsaved
// changes, holding the song as it is then, and gone once they are undone.
void Startup::queueRecoveryCopy() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("recovery check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        recoveryWritten_ = false;
        score_->setSongTitle(QStringLiteral("Recovery copy"));
        score_->writeRecovery();
        return true;
    });
    steps_.emplace_back([this, check] {
        const RecoveryStore* store = score_->recovery();
        if (store == nullptr) {
            check(false, "no recovery folder");
            return false;
        }
        if (!recoveryWritten_ || !store->hasCopy()) {
            again_ = true;
            return false;
        }
        const auto copy = io::loadNative(std::filesystem::path(store->copyFile().toStdU16String()));
        check(copy && copy->info().title == "Recovery copy", "the copy does not hold the song");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() && !score_->recovery()->hasCopy(),
              "the copy outlived the changes");
        return false;
    });
}

// Saving on a worker (ADR-0107): the file holds the song as it was when the
// save started, and an edit made while it is written stays unsaved; undoing
// that edit returns to the state saved. A new song asked for in the window
// while a save is written waits for it, and a save that ends after another
// song replaced the one it wrote marks nothing. The steps after these edit a
// new song.
void Startup::queueSave() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("save check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        score_->setSongTitle(QStringLiteral("Saved"));
        return true;
    });
    steps_.emplace_back([this] {
        if (!scratch_) {
            scratch_.emplace();
        }
        saved_.reset();
        score_->save(QUrl::fromLocalFile(scratch_->filePath(QStringLiteral("saved"))));
        // Before the save can be done: its result comes through the event loop.
        score_->setSongTitle(QStringLiteral("Edited while saving"));
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!saved_) {
            again_ = true;
            return false;
        }
        const QString file = scratch_->filePath(QStringLiteral("saved.elara"));
        check(*saved_ && !score_->saving(), "the save failed");
        check(score_->source() == QUrl::fromLocalFile(file), "the song is not the file saved");
        check(score_->modified(), "the edit made while saving was marked saved");
        const auto song = io::loadNative(std::filesystem::path(file.toStdU16String()));
        check(song && song->info().title == "Saved", "the file does not hold the song saved");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(score_->songTitle() == QStringLiteral("Saved") && !score_->modified(),
              "undoing the edit made while saving did not return to the state saved");
        score_->setSongTitle(QStringLiteral("Waiting"));
        return true;
    });
    steps_.emplace_back([this] {
        // A new song asked for while a save is written waits for it, and is
        // made without asking once the save holds every change (ADR-0107).
        // The edit before was laid out first, so that the new song's layout
        // never takes the place of the edit's, whenever the save ends.
        saved_.reset();
        score_->save(QUrl::fromLocalFile(scratch_->filePath(QStringLiteral("waiting"))));
        QMetaObject::invokeMethod(window_, "newSong");
        window_->setProperty("showSongPane", false);
        return true;
    });
    steps_.emplace_back([this, check] {
        // The save has ended and the new song made then is laid out, so that
        // the next edit does not take the place of its layout.
        if (!saved_ || score_->busy()) {
            again_ = true;
            return false;
        }
        const auto* unsaved = window_->findChild<QObject*>(QStringLiteral("unsavedDialog"));
        check(unsaved != nullptr && !unsaved->property("visible").toBool(),
              "a song saved while a new one waited was asked to be saved");
        check(score_->songTitle() != QStringLiteral("Waiting") && score_->source().isEmpty() &&
                  !score_->modified(),
              "the new song asked for while saving was not made once the save ended");
        // A song that replaces the one being saved is not marked saved by it.
        score_->setSongTitle(QStringLiteral("Replaced"));
        saved_.reset();
        score_->save(QUrl::fromLocalFile(scratch_->filePath(QStringLiteral("replaced"))));
        score_->newSong(QStringLiteral("electric-guitar"));
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!saved_) {
            again_ = true;
            return false;
        }
        check(*saved_ && score_->source().isEmpty() && !score_->modified(),
              "a save marked the song that replaced the one it wrote");
        return false;
    });
}

// Edits a new song and, once its recovery copy is written, quits at once as a
// crash would: the copy stays, with nobody holding its lock, for
// app.smoke_recovery to find (ADR-0106).
void Startup::queueCrashAfterRecoveryCopy() {
    steps_.emplace_back([this] {
        score_->newSong(QStringLiteral("electric-guitar"));
        return true;
    });
    steps_.emplace_back([this] {
        recoveryWritten_ = false;
        score_->setSongTitle(QString::fromLatin1(kCrashedTitle));
        score_->writeRecovery();
        return true;
    });
    steps_.emplace_back([this] {
        if (!recoveryWritten_) {
            again_ = true;
            return false;
        }
        reportSmokeTest(true, QStringLiteral("left a recovery copy"));
        std::fflush(stdout);
        std::fflush(stderr);
        quitAsCrash(EXIT_SUCCESS);
    });
}

// The copy app.smoke_recovery_crash left (ADR-0106): offered at start,
// recovered as the song it was with unsaved changes and no file, and offered
// no more once this run's own copy holds the song. Prints "recovery: <title>".
void Startup::queueRecover() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("recovery check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        auto* dialog = window_->findChild<QObject*>(QStringLiteral("recoveryDialog"));
        const QVariantList left = score_->recoverable();
        if (dialog == nullptr || !dialog->property("visible").toBool() ||
            dialog->property("count").toInt() != 1 || left.size() != 1) {
            failure_ = QStringLiteral("the recovery copy left is not offered");
            return false;
        }
        std::printf("recovery: %s\n",
                    qPrintable(left.front().toMap().value(QStringLiteral("title")).toString()));
        recoveryWritten_ = false;
        QMetaObject::invokeMethod(dialog, "recover", Q_ARG(int, 0));
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!recoveryWritten_) {
            again_ = true;
            return false;
        }
        check(score_->songTitle() == QString::fromLatin1(kCrashedTitle) && score_->modified() &&
                  score_->source().isEmpty(),
              "the song recovered is not the song left");
        check(score_->recoverable().isEmpty(), "the copy recovered is still offered");
        return false;
    });
}

// The window's state kept in `--window-state`'s file (ADR-0137). A run that
// finds nothing kept moves and resizes the window, closes the note effects
// palette, opens the song panel, zooms in by hand, lightens the page and
// shows the continuous view, then quits, which keeps them: "window state
// kept: ...". The next run finds the window as that one left it: "window
// state restored: ...".
void Startup::queueWindowState() {
    auto* state = window_->findChild<WindowState*>();
    auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    auto* palette = window_->findChild<QObject*>(QStringLiteral("effectsPanel"));
    if (state == nullptr || view == nullptr || palette == nullptr) {
        steps_.emplace_back([this] {
            failure_ = QStringLiteral("the window keeps no state");
            return false;
        });
        return;
    }
    const bool restored = state->restored();
    if (!restored) {
        steps_.emplace_back([this, view, palette] {
            // Inside the offscreen platform's one screen, 800 by 600.
            window_->setGeometry(QRect(20, 40, 760, 480));
            palette->setProperty("opened", false);
            window_->setProperty("showSongPane", true);
            window_->setProperty("darkPage", false);
            score_->setContinuous(true);
            QMetaObject::invokeMethod(view, "zoomIn");
            return false;
        });
        // The place and size are kept once they have settled.
        steps_.emplace_back([this, state] {
            if (state->geometry() != window_->geometry()) {
                again_ = true;
            }
            return false;
        });
    }
    steps_.emplace_back([this, view, palette, restored] {
        const QRect place = window_->geometry();
        const bool byHand = view->property("fit").toString().isEmpty();
        std::printf("window state %s: %dx%d at %d,%d, effects %s, song panel %s, zoom %s %% %s, "
                    "%s page, %s view\n",
                    restored ? "restored" : "kept", place.width(), place.height(), place.x(),
                    place.y(), palette->property("opened").toBool() ? "open" : "closed",
                    window_->property("showSongPane").toBool() ? "open" : "closed",
                    qPrintable(QString::number(view->property("zoom").toDouble() * 100.0, 'f', 0)),
                    byHand ? "by hand" : qPrintable(view->property("fit").toString()),
                    window_->property("darkPage").toBool() ? "dark" : "light",
                    score_->continuous() ? "continuous" : "page");
        return false;
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
        entering_ = editor_->caretFretted() && score_->caretPage() >= 0;
        if (!entering_) {
            return false;
        }
        const core::Caret before = score_->caret();
        const QPointF centre = score_->caretRect().center();
        check(score_->placeCaret(score_->caretPage(), centre.x(), centre.y()) &&
                  score_->caret() == before,
              "a click on the caret moved it");
        score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        // Both keys at one instant: the first digit's layout can outlast the
        // two-digit window on a loaded machine.
        const auto pressed = core::EditSession::Clock::now();
        score_->typeDigit(1, pressed);
        score_->typeDigit(2, pressed);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(editor_->fretAtCaret() == 12, "two digits did not type fret 12");
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
        check(editor_->fretAtCaret() == 12, "redo did not bring the fret back");
        score_->setInputValue(8);
        check(score_->inputValue() == 8, "the input duration did not change");
        editor_->insertRest();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(!editor_->fretAtCaret().has_value(), "the caret is not on the inserted rest");
        editor_->deleteAtCaret();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!entering_) {
            return false;
        }
        check(editor_->fretAtCaret() == 12, "deleting the rest did not bring the note back");
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
        tupleting_ = score_->caretPage() >= 0 && editor_->caretFretted();
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

// Note names at the caret: a C typed, raised a semitone, both undone.
void Startup::queuePitches() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("pitch check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        pitching_ = score_->caretPage() >= 0 && editor_->caretPitched();
        if (!pitching_) {
            return false;
        }
        editor_->typeNoteName(0, false);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!pitching_) {
            return false;
        }
        const std::vector<core::Pitch> pitches = editor_->pitchesAtCaret();
        check(pitches.size() == 1 && pitches.front().step() == core::Step::C,
              "the note name did not type a C");
        if (pitches.empty()) {
            return false;
        }
        check(editor_->caretStatus().endsWith(QStringLiteral("C%1").arg(pitches.front().octave())),
              "the status does not name the note");
        typedMidi_ = pitches.front().midiNumber();
        editor_->shiftPitch(1);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!pitching_) {
            return false;
        }
        const std::vector<core::Pitch> pitches = editor_->pitchesAtCaret();
        check(pitches.size() == 1 && pitches.front().midiNumber() == typedMidi_ + 1,
              "the note did not move up a semitone");
        for (int i = 0; i < 4 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!pitching_) {
            return false;
        }
        check(!score_->modified(), "undoing the note names did not restore the song");
        return false;
    });
}

// Drum entry on a new drum kit track (ADR-0035): the status names the lane,
// the caret box goes down with the lanes, and a snare is toggled on and off
// again. Then the track is undone and the song must end as it started.
void Startup::queueDrums() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("drum check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        drumming_ = score_->caretPage() >= 0;
        if (!drumming_) {
            return false;
        }
        score_->addTrack(QStringLiteral("drum-kit"));
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        check(editor_->caretOnDrums(), "the caret is not on the drum track");
        const int lanes = static_cast<int>(core::drumPieces().size());
        for (int lane = 1; lane < lanes; ++lane) {
            score_->moveCaret(ScoreDocument::CaretMove::StringUp);
        }
        check(score_->caret().string == 1 &&
                  editor_->caretStatus().endsWith(CaretEditor::laneName(1)),
              "the status does not name the top lane");
        const double crash = score_->caretRect().top();
        for (int lane = 1; lane < laneOf(kSnareKey); ++lane) {
            score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        }
        check(editor_->caretStatus().endsWith(CaretEditor::laneName(laneOf(kSnareKey))),
              "the status does not name the snare");
        check(score_->caretRect().top() > crash, "the caret did not go down to the snare");
        editor_->toggleDrum();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        const std::vector<core::Pitch> hits = editor_->pitchesAtCaret();
        check(hits.size() == 1 && hits.front().midiNumber() == kSnareKey,
              "the snare was not added");
        editor_->toggleDrum();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        check(editor_->pitchesAtCaret().empty(), "the snare was not removed");
        for (int i = 0; i < 3 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        check(!score_->modified(), "undoing the drums did not restore the song");
        if (score_->track() == 0) {
            return false;
        }
        score_->setTrack(0);
        return true;
    });
}

// Voices: the caret goes to voice 2, which the status names, and a note typed
// there goes into it; back in voice 1 the status no longer names a voice.
// Undone, the song must end as it started.
void Startup::queueVoices() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("voice check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        voicing_ = score_->caretPage() >= 0 && editor_->caretPitched();
        if (!voicing_) {
            return false;
        }
        score_->setCaretVoice(2);
        check(score_->caretVoice() == 2, "the caret did not go to voice 2");
        check(editor_->caretStatus().contains(CaretEditor::tr("Voice %1").arg(2)),
              "the status does not name voice 2");
        if (editor_->caretFretted()) {
            score_->typeDigit(7);
        } else {
            editor_->typeNoteName(2, false);
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!voicing_) {
            return false;
        }
        check(score_->caretVoice() == 2 && !editor_->pitchesAtCaret().empty(),
              "the note did not go into voice 2");
        score_->setCaretVoice(1);
        check(score_->caretVoice() == 1 &&
                  !editor_->caretStatus().contains(CaretEditor::tr("Voice %1").arg(2)),
              "the caret did not come back to voice 1");
        for (int i = 0; i < 3 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!voicing_) {
            return false;
        }
        check(!score_->modified(), "undoing the voice did not restore the song");
        return false;
    });
}

// Selected measures (ADR-0036): two measures selected and tinted on the page,
// copied, let go, pasted into new measures before the caret's, which end up
// selected, and transposed. Then everything is undone and the song must end as it started.
void Startup::queueSelection() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("selection check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        selecting_ = score_->caretPage() >= 0 && score_->measureCount() > 1;
        if (!selecting_) {
            return false;
        }
        lengthBefore_ = score_->measureCount();
        const bool last = score_->caretMeasure() + 1 >= lengthBefore_;
        score_->extendSelection(last ? -1 : 1);
        check(score_->hasSelection() && score_->selectionLast() == score_->selectionFirst() + 1,
              "Shift+Right did not select two measures");
        check(score_->selectionBoxes(score_->caretPage()).size() == 2,
              "the page does not show the two measures selected");
        check(editor_->caretStatus().startsWith(CaretEditor::tr("Measures %1-%2 selected")
                                                    .arg(score_->selectionFirst() + 1)
                                                    .arg(score_->selectionLast() + 1)),
              "the status does not say what is selected");
        score_->copy();
        check(score_->canPaste(), "nothing was copied");
        score_->clearSelection();
        pasteAt_ = score_->caretMeasure();
        score_->paste(true);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!selecting_) {
            return false;
        }
        check(score_->measureCount() == lengthBefore_ + 2, "pasting did not insert two measures");
        check(score_->hasSelection() && score_->selectionFirst() == pasteAt_ &&
                  score_->selectionLast() == pasteAt_ + 1,
              "the pasted measures are not selected");
        score_->transposeSelection(2);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!selecting_) {
            return false;
        }
        check(score_->undoName() == ScoreDocument::tr("Transpose"),
              "the transposition is not undoable");
        score_->clearSelection();
        check(!score_->hasSelection() && score_->selectionBoxes(score_->caretPage()).isEmpty(),
              "Escape did not let go of the selection");
        for (int i = 0; i < 2 && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!selecting_) {
            return false;
        }
        check(!score_->modified() && score_->measureCount() == lengthBefore_,
              "undoing the paste did not restore the song");
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
        // A row of the song panel: the mix changes below change rows, but
        // must not make them again.
        trackRow_ = findItem(window_->contentItem(), QStringLiteral("trackRow"));
        check(trackRow_ != nullptr, "the song panel lists no track");
        const auto tracks = static_cast<std::size_t>(score_->tracks().size());
        score_->previewTrackMix(0, 0.25, 0.5);
        check(score_->previewMixes().size() == tracks &&
                  score_->previewMixes().front().volume == 0.25 && !score_->modified(),
              "the fader preview changed the song");
        const std::uint64_t layouts = score_->layoutWork().asked;
        const std::uint64_t sequences = score_->sequenceWork().asked;
        score_->setTrackMix(0, 0.25, -0.5, false, true);
        // Neither drawn nor part of the sequence (ADR-0111).
        check(score_->layoutWork().asked == layouts && score_->sequenceWork().asked == sequences,
              "a mix change was laid out or sequenced");
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
        check(trackRow_ != nullptr, "a mix change made the song panel's rows again");
        sequenceBefore_.reset();
        if (trackRow_ == nullptr) {
            return false;
        }
        // The track's name typed in its row, as a double click on it starts.
        trackName_ = score_->trackNames().front();
        check(QMetaObject::invokeMethod(trackRow_, "rename") &&
                  trackRow_->property("renaming").toBool(),
              "a track's row does not take another name");
        auto* field = trackRow_->findChild<QObject*>(QStringLiteral("trackNameField"));
        check(field != nullptr, "a track's row has no field for its name");
        if (field == nullptr) {
            return false;
        }
        check(field->property("text").toString() == trackName_,
              "the field does not start with the track's name");
        field->setProperty("text", QStringLiteral("Renamed in the panel"));
        QMetaObject::invokeMethod(field, "editingFinished");
        check(score_->trackNames().front() == QStringLiteral("Renamed in the panel") &&
                  !trackRow_->property("renaming").toBool(),
              "the name typed in the row was not given to the track");
        check(score_->undoName() == ScoreDocument::tr("Rename track"),
              "renaming a track is not undoable");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() && score_->trackNames().front() == trackName_,
              "undoing the rename did not restore the song");
        return false;
    });
}

// Signs and jumps from the measure popup (ADR-0049): a segno on the first
// measure, then a D.S. al Coda on it, each one undo step named for them; both
// undone, the song must end as it started.
void Startup::queueNavigation() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("navigation check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        score_->setNavigation(0, {{QStringLiteral("segno"), true}});
        check(score_->navigationAt(0).value(QStringLiteral("segno")).toBool(),
              "the segno was not set");
        check(score_->undoName() == ScoreDocument::tr("Change navigation marks"),
              "the marks are not one undo step");
        return true;
    });
    steps_.emplace_back([this, check] {
        score_->setNavigation(0, {{QStringLiteral("jump"), 2}, {QStringLiteral("jumpThen"), 2}});
        const QVariantMap marks = score_->navigationAt(0);
        check(marks.value(QStringLiteral("jump")).toInt() == 2 &&
                  marks.value(QStringLiteral("jumpThen")).toInt() == 2 &&
                  marks.value(QStringLiteral("segno")).toBool(),
              "the jump was not set beside the segno");
        score_->undo();
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        const QVariantMap marks = score_->navigationAt(0);
        check(!score_->modified() && !marks.value(QStringLiteral("segno")).toBool() &&
                  marks.value(QStringLiteral("jump")).toInt() == 0,
              "undoing the marks did not restore the song");
        return false;
    });
}

// Repeats from the measure popup (ADR-0016): the first measure's start
// barline toggled with its end repeat played a different number of times,
// then other endings and its double barline (ADR-0103), each one undo step;
// all undone, the song must end as it started.
void Startup::queueRepeats() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("repeats check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        repeatsBefore_ = score_->repeatsAt(0);
        const bool start = !repeatsBefore_.value(QStringLiteral("start")).toBool();
        const int times = repeatsBefore_.value(QStringLiteral("times")).toInt() == 3 ? 4 : 3;
        score_->setRepeats(0, {{QStringLiteral("start"), start}, {QStringLiteral("times"), times}});
        const QVariantMap repeats = score_->repeatsAt(0);
        check(repeats.value(QStringLiteral("start")).toBool() == start &&
                  repeats.value(QStringLiteral("times")).toInt() == times,
              "the repeat was not set");
        check(score_->undoName() == ScoreDocument::tr("Change repeats"),
              "the repeat is not one undo step");
        return true;
    });
    steps_.emplace_back([this, check] {
        const QVariantList first{1, 2};
        const QVariantList endings =
            repeatsBefore_.value(QStringLiteral("endings")).toList() == first ? QVariantList{2, 3}
                                                                              : first;
        const int times = score_->repeatsAt(0).value(QStringLiteral("times")).toInt();
        score_->setRepeats(0, {{QStringLiteral("endings"), endings}});
        const QVariantMap repeats = score_->repeatsAt(0);
        check(repeats.value(QStringLiteral("endings")).toList() == endings &&
                  repeats.value(QStringLiteral("times")).toInt() == times,
              "the endings were not set beside the repeat");
        // The double barline (ADR-0103) toggles on its own.
        const bool doubleBar = !repeats.value(QStringLiteral("doubleBar")).toBool();
        score_->setRepeats(0, {{QStringLiteral("doubleBar"), doubleBar}});
        check(score_->repeatsAt(0).value(QStringLiteral("doubleBar")).toBool() == doubleBar &&
                  score_->repeatsAt(0).value(QStringLiteral("endings")).toList() == endings,
              "the double barline was not set beside the endings");
        check(score_->undoName() == ScoreDocument::tr("Change barline"),
              "the double barline is not one undo step");
        score_->undo();
        score_->undo();
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() && score_->repeatsAt(0) == repeatsBefore_,
              "undoing the repeats did not restore the song");
        return false;
    });
}

// A section named from the measure popup (ADR-0062): the first measure's name
// set, trimmed, as one undo step that the score draws; undone, the song must
// end as it started.
// A symbol edited where it is drawn (ADR-0130): the time signature of the
// first page is found under a point of its ink, nothing is found in the page's
// corner, and what a double click on it calls opens its card for the measure
// the signature starts at. Nothing is changed.
void Startup::queueSymbols() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("symbol check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        const auto page = score_->page(0);
        auto* view = window_->findChild<QObject*>(QStringLiteral("scoreView"));
        if (!page || view == nullptr) {
            return false;
        }
        const auto digit = std::ranges::find(page->glyphs, notation::Role::TimeSignature,
                                             &notation::GlyphItem::role);
        if (digit == page->glyphs.end()) {
            return false; // a view that writes none on its first page
        }
        const QVariantMap symbol = score_->symbolAt(0, digit->origin.x + 0.5, digit->origin.y);
        check(symbol.value(QStringLiteral("kind")).toString() == QStringLiteral("time"),
              "the time signature is not found where it is drawn");
        check(symbol.value(QStringLiteral("rect"))
                  .toRectF()
                  .contains(QPointF(digit->origin.x + 0.5, digit->origin.y)),
              "the time signature's box is not round it");
        check(score_->symbolAt(0, 0.2, 0.2).isEmpty(), "a symbol was found in the page's corner");
        check(QMetaObject::invokeMethod(view, "editSymbol", Q_ARG(int, 0),
                                        Q_ARG(QVariant, QVariant(symbol))),
              "the score view does not edit a symbol");
        const auto* card = window_->findChild<QObject*>(QStringLiteral("symbolCard"));
        check(card != nullptr && card->property("visible").toBool(),
              "the time signature's card did not open");
        if (card != nullptr) {
            check(card->property("kind").toString() == QStringLiteral("time") &&
                      card->property("measure").toInt() ==
                          symbol.value(QStringLiteral("measure")).toInt(),
                  "the card is not for the time signature of that measure");
            const QVariantMap shown = card->property("signature").toMap();
            const QVariantMap written =
                score_->signatureAt(symbol.value(QStringLiteral("measure")).toInt());
            check(!written.isEmpty() && shown == written,
                  "the card does not show the measure's time signature");
        }
        return false;
    });
    steps_.emplace_back([this, check] {
        // It closes as it opened, and the song is as it was.
        if (auto* card = window_->findChild<QObject*>(QStringLiteral("symbolCard"));
            card != nullptr && card->property("visible").toBool()) {
            QMetaObject::invokeMethod(card, "close");
        }
        check(!score_->modified(), "opening a symbol's card changed the song");
        return false;
    });
}

// A symbol edited with the pointer, as a hand does it (ADR-0130): over the
// first page's time signature the page lights it, and a double click there
// opens its card. The clicks also put the caret and playback on that measure,
// as any click on the page does; what follows moves both again.
void Startup::queueSymbolClick() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("symbol click check failed: %1").arg(QLatin1StringView(what));
        }
    };
    const auto send = [this](QEvent::Type type, QPointF at) {
        QMouseEvent event(type, at, at, window_->mapToGlobal(at),
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(window_, &event);
    };
    // Where the time signature is in the window, and whether the song had
    // unsaved changes; no point for a view that writes none on its first page.
    struct Found {
        std::optional<QPointF> at;
        QPointF onPaper;
        bool modified = false;
    };
    const auto found = std::make_shared<Found>();
    steps_.emplace_back([this, found, send] {
        const auto page = score_->page(0);
        auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
        if (!page || view == nullptr) {
            return false;
        }
        const auto digit = std::ranges::find(page->glyphs, notation::Role::TimeSignature,
                                             &notation::GlyphItem::role);
        if (digit == page->glyphs.end()) {
            return false;
        }
        if (!uncoverScore()) {
            return false;
        }
        // The view shows the top of the first page, and stands still there.
        if (view->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        const QQuickItem* light = symbolHighlight(view, 0);
        const double scale =
            view->property("unit").toDouble() * view->property("shownZoom").toDouble();
        const QPointF onPaper((digit->origin.x + 0.5) * scale, digit->origin.y * scale);
        if (light == nullptr || light->parentItem() == nullptr ||
            !view->contains(view->mapFromScene(light->parentItem()->mapToScene(onPaper)))) {
            QMetaObject::invokeMethod(view, "toTop");
            again_ = true;
            return false;
        }
        found->onPaper = onPaper;
        found->at = light->parentItem()->mapToScene(onPaper);
        found->modified = score_->modified();
        send(QEvent::MouseMove, *found->at);
        return false;
    });
    steps_.emplace_back([this, found, send, check] {
        if (!found->at) {
            return false;
        }
        auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
        const QQuickItem* light = symbolHighlight(view, 0);
        check(light != nullptr && light->isVisible(),
              "the time signature under the pointer is not lit");
        if (light != nullptr) {
            check(QRectF(light->position(), light->size()).contains(found->onPaper),
                  "what is lit is not round the pointer");
        }
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                                        QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            send(type, *found->at);
        }
        return false;
    });
    steps_.emplace_back([this, found, send, check] {
        if (!found->at) {
            return false;
        }
        auto* card = window_->findChild<QObject*>(QStringLiteral("symbolCard"));
        check(card != nullptr && card->property("visible").toBool() &&
                  card->property("kind").toString() == QStringLiteral("time"),
              "a double click on the time signature did not open its card");
        if (card != nullptr) {
            const QVariantMap written = score_->signatureAt(card->property("measure").toInt());
            check(!written.isEmpty() && card->property("signature").toMap() == written,
                  "the card does not show the time signature that was clicked");
            QMetaObject::invokeMethod(card, "close");
        }
        // The pointer leaves the page.
        send(QEvent::MouseMove, QPointF(1, 1));
        return false;
    });
    steps_.emplace_back([this, found, check] {
        if (!found->at) {
            return false;
        }
        auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
        const QQuickItem* light = symbolHighlight(view, 0);
        check(light == nullptr || !light->isVisible(),
              "the time signature stays lit with the pointer away");
        check(score_->modified() == found->modified, "a double click on a symbol changed the song");
        return false;
    });
}

void Startup::queueSection() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("section check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        sectionBefore_ = score_->sectionAt(0);
        const QString name = sectionBefore_ == QStringLiteral("Chorus") ? QStringLiteral("Verse")
                                                                        : QStringLiteral("Chorus");
        score_->setSection(0, QStringLiteral("  ") + name + QStringLiteral(" "));
        check(score_->sectionAt(0) == name, "the section was not named, trimmed");
        check(score_->undoName() == ScoreDocument::tr("Change section"),
              "the section is not one undo step");
        return true;
    });
    steps_.emplace_back([this, check] {
        // The song string marks it where it first plays: at the start.
        const QVariantList marks = score_->sectionMarks();
        const QVariantMap first = marks.isEmpty() ? QVariantMap{} : marks.front().toMap();
        check(first.value(QStringLiteral("name")).toString() == score_->sectionAt(0) &&
                  first.value(QStringLiteral("seconds"), -1.0).toDouble() == 0.0,
              "the song string does not mark where the section starts");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() && score_->sectionAt(0) == sectionBefore_,
              "undoing the section did not restore the song");
        return false;
    });
}

// A measure repeat from the measure popup (ADR-0102): a measure inserted after
// the caret's repeats it, one undo step; taking the sign away writes out what
// it played; all undone, the song must end as it started.
void Startup::queueMeasureRepeat() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("measure repeat check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        score_->insertMeasure(true);
        editor_->setMeasureRepeat(1);
        check(editor_->caretRepeat() == 1 &&
                  score_->undoName() == ScoreDocument::tr("Repeat measure"),
              "the new measure does not repeat the one before");
        editor_->setMeasureRepeat(0);
        check(editor_->caretRepeat() == 0 &&
                  score_->undoName() == ScoreDocument::tr("Write out measure repeat"),
              "taking the sign away did not write out its music");
        return true;
    });
    steps_.emplace_back([this] {
        score_->undo();
        score_->undo();
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified(), "undoing the measure repeat did not restore the song");
        return false;
    });
}

// A tempo change from the measure popup (ADR-0042): 90 beats per minute from
// the second measure, one undo step named for it that leaves the first
// measure's tempo alone, then taken away again; both undone, the song must end
// as it started.
void Startup::queueTempo() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("tempo check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        if (score_->measureCount() < 2) {
            return false;
        }
        tempoBefore_ = score_->tempoAt(1);
        const QVariantMap first = score_->tempoAt(0);
        score_->setTempoFrom(1, 90);
        const QVariantMap tempo = score_->tempoAt(1);
        check(tempo.value(QStringLiteral("bpm")).toInt() == 90 &&
                  tempo.value(QStringLiteral("own")).toBool(),
              "the second measure does not start at 90");
        check(score_->undoName() == ScoreDocument::tr("Change tempo"),
              "the tempo is not one undo step");
        check(score_->tempoAt(0) == first, "the first measure's tempo changed");
        // Gradual to the next change (ADR-0082), keeping its tempo.
        score_->setTempoGradual(1, true);
        const QVariantMap gradual = score_->tempoAt(1);
        check(gradual.value(QStringLiteral("gradual")).toBool() &&
                  gradual.value(QStringLiteral("bpm")).toInt() == 90,
              "the tempo change did not become gradual");
        check(score_->undoName() == ScoreDocument::tr("Change tempo"),
              "making the tempo gradual is not one undo step");
        return true;
    });
    steps_.emplace_back([this, check] {
        if (score_->measureCount() < 2) {
            return false;
        }
        score_->setTempoFrom(1, 0);
        check(!score_->tempoAt(1).value(QStringLiteral("own")).toBool(),
              "the tempo change was not taken away");
        // The triplet feel (ADR-0104) from the second measure on, one undo
        // step, leaving the first measure's.
        const int feelBefore = score_->tripletFeelAt(0);
        const int feel = score_->tripletFeelAt(1) == 1 ? 3 : 1;
        score_->setTripletFeelFrom(1, feel);
        check(score_->tripletFeelAt(1) == feel && score_->tripletFeelAt(0) == feelBefore,
              "the second measure did not take the triplet feel");
        check(score_->undoName() == ScoreDocument::tr("Change triplet feel"),
              "the triplet feel is not one undo step");
        score_->undo();
        score_->undo();
        score_->undo();
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (score_->measureCount() < 2) {
            return false;
        }
        check(!score_->modified() && score_->tempoAt(1) == tempoBefore_,
              "undoing the tempo did not restore the song");
        return false;
    });
}

// The first track's tuning from the song panel (ADR-0046): a capo a fret
// higher keeping the frets, then the capo taken off keeping the sound, each
// one undo step; both undone, the song must end as it started.
// The tuner open over a track with strings tunes to them: by its preset,
// or by an entry of the track's own for a tuning no preset has, and back
// when the tuning comes back. Prints "tuner: <strings>" each time.
void Startup::queueTunerFollowsTrack() {
    const auto strings = [this] {
        const auto* tuner = window_->findChild<TunerController*>();
        return tuner != nullptr ? tuner->stringNames().join(QLatin1Char(' ')) : QString();
    };
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("tuner check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, strings, check] {
        tuningBefore_ = score_->trackTuning(score_->track());
        std::printf("tuner: %s\n", qPrintable(strings()));
        check(tuningBefore_.value(QStringLiteral("fretted")).toBool() &&
                  strings() == tuningBefore_.value(QStringLiteral("notes")).toString(),
              "the tuner does not tune to the track's strings");
        // A whole step down, which no preset has.
        score_->setTrackTuning(score_->track(), QStringLiteral("D1 G1 C2 F2"), 0, false);
        return true;
    });
    steps_.emplace_back([this, strings, check] {
        const auto* tuner = window_->findChild<TunerController*>();
        const QString track = score_->trackNames().value(score_->track());
        std::printf("tuner: %s\n", qPrintable(strings()));
        check(tuner != nullptr &&
                  tuner->tuning() == static_cast<int>(tuner->tuningLabels().size()) - 1 &&
                  tuner->tuningLabels().last() == TunerController::tr("Track: %1").arg(track),
              "the tuner has no entry for the track's own tuning");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, strings, check] {
        const auto* tuner = window_->findChild<TunerController*>();
        std::printf("tuner: %s\n", qPrintable(strings()));
        check(strings() == tuningBefore_.value(QStringLiteral("notes")).toString(),
              "the tuner did not follow the tuning back");
        check(tuner != nullptr && !tuner->tuningLabels().last().startsWith(
                                      TunerController::tr("Track: %1").arg(QString())),
              "the track's own entry outlived its tuning");
        return false;
    });
}

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

// Note effects at the caret (ADR-0038): a note typed, then accent and
// staccato toggled on it, which the status names, and the accent off again.
// The keys map to the effects. Then everything is undone and the song must end
// as it started.
void Startup::queueEffects() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("effect check failed: %1").arg(QLatin1StringView(what));
        }
    };
    constexpr int kAccent = static_cast<int>(core::NoteEffect::Accent);
    constexpr int kStaccato = static_cast<int>(core::NoteEffect::Staccato);
    steps_.emplace_back([this, check] {
        effecting_ = score_->caretPage() >= 0 && editor_->caretPitched();
        if (!effecting_) {
            return false;
        }
        check(editor_->effectForKey(QStringLiteral(">")) == kAccent &&
                  editor_->effectForKey(QStringLiteral("'")) == kStaccato &&
                  editor_->effectForKey(QStringLiteral("_")) ==
                      static_cast<int>(core::NoteEffect::Tenuto) &&
                  editor_->effectForKey(QStringLiteral("\"")) ==
                      static_cast<int>(core::NoteEffect::Staccatissimo) &&
                  editor_->effectForKey(QStringLiteral("p")) ==
                      static_cast<int>(core::NoteEffect::PalmMute) &&
                  editor_->effectForKey(QStringLiteral("a")) < 0,
              "the keys do not map to the effects");
        // The panel shows the techniques of strings on fretted staves only.
        check(editor_->effectNeedsStrings(static_cast<int>(core::NoteEffect::PalmMute)) &&
                  editor_->effectNeedsStrings(static_cast<int>(core::NoteEffect::Harmonic)) &&
                  !editor_->effectNeedsStrings(kAccent) && !editor_->effectNeedsStrings(-1),
              "the techniques of strings are not told apart");
        if (editor_->caretFretted()) {
            score_->typeDigit(5);
        } else {
            editor_->typeNoteName(0, false);
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!effecting_) {
            return false;
        }
        effectsBefore_ = editor_->caretEffects();
        editor_->toggleEffect(kAccent);
        check((editor_->caretEffects() ^ effectsBefore_) == 1 << kAccent,
              "the accent did not toggle");
        check(score_->undoName() == ScoreDocument::tr("Change effect"),
              "the effect is not undoable");
        editor_->toggleEffect(kStaccato);
        check((editor_->caretEffects() ^ effectsBefore_) == ((1 << kAccent) | (1 << kStaccato)),
              "staccato did not toggle");
        const int on = editor_->caretEffects();
        check(((on & (1 << kStaccato)) != 0) ==
                  editor_->caretStatus().contains(editor_->effectName(kStaccato)),
              "the status does not name the effects");
        editor_->toggleEffect(kAccent);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!effecting_) {
            return false;
        }
        check((editor_->caretEffects() ^ effectsBefore_) == 1 << kStaccato,
              "the accent did not go off again");
        // Bends of any form (ADR-0055).
        constexpr int kRelease = static_cast<int>(core::BendForm::BendRelease);
        constexpr int kPreBend = static_cast<int>(core::BendForm::PreBend);
        editor_->setBend(kRelease, 100);
        check(editor_->caretBendForm() == kRelease && editor_->caretBendCents() == 100,
              "the bend and release did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change bend"), "the bend is not undoable");
        check(editor_->caretStatus().contains(editor_->bendName(kRelease, 100)) &&
                  editor_->bendName(kRelease, 100).endsWith(QStringLiteral("1/2")),
              "the status does not name the bend");
        editor_->setBend(kPreBend, 300);
        check(editor_->caretBendForm() == kPreBend && editor_->caretBendCents() == 300 &&
                  editor_->bendAmount(300) == QStringLiteral("1 1/2"),
              "the pre-bend did not replace it");
        editor_->setBend(0, 0);
        check(editor_->caretBendForm() == 0 && editor_->caretBendCents() == 0,
              "the bend did not go");
        // The beat's own effects (ADR-0056).
        editor_->setStrum(1);
        check(editor_->caretStrum() == 1, "the strum did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change beat effects"),
              "the beat effects are not undoable");
        check(editor_->caretStatus().contains(CaretEditor::tr("Strum down")),
              "the status does not name the strum");
        editor_->setTremolo(16);
        editor_->setPickStroke(2);
        check(editor_->caretTremolo() == 16 && editor_->caretPickStroke() == 2 &&
                  editor_->caretStrum() == 1,
              "tremolo picking and the pick stroke did not join the strum");
        editor_->setStrum(0);
        check(editor_->caretStrum() == 0 && editor_->caretTremolo() == 16,
              "the strum did not go on its own");
        // Kinds of note effects (ADR-0058).
        editor_->setVibrato(static_cast<int>(core::Vibrato::Wide));
        editor_->setAccent(static_cast<int>(core::Accent::Marcato));
        check(score_->undoName() == ScoreDocument::tr("Change accent"),
              "the accent's kind is not undoable");
        editor_->setSlideIn(static_cast<int>(core::SlideIn::FromBelow));
        editor_->setSlideOut(static_cast<int>(core::SlideOut::Downwards));
        check(editor_->caretVibrato() == static_cast<int>(core::Vibrato::Wide) &&
                  editor_->caretAccent() == static_cast<int>(core::Accent::Marcato) &&
                  editor_->caretSlideIn() == static_cast<int>(core::SlideIn::FromBelow) &&
                  editor_->caretSlideOut() == static_cast<int>(core::SlideOut::Downwards),
              "the kinds of effects did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change slide"),
              "the slides are not undoable");
        // A trill to the next step of the key, then off again (ADR-0090).
        editor_->toggleTrill();
        check(editor_->caretTrill() > 0 &&
                  score_->undoName() == ScoreDocument::tr("Change trill") &&
                  editor_->caretStatus().contains(CaretEditor::tr("Trill")),
              "the trill did not go on");
        editor_->toggleTrill();
        check(editor_->caretTrill() == 0, "the trill did not come off");
        // A turn, then none (ADR-0091).
        editor_->setOrnament(static_cast<int>(core::Ornament::Turn));
        check(editor_->caretOrnament() == static_cast<int>(core::Ornament::Turn) &&
                  score_->undoName() == ScoreDocument::tr("Change ornament") &&
                  editor_->caretStatus().contains(CaretEditor::tr("Turn")),
              "the turn did not go on");
        editor_->setOrnament(0);
        check(editor_->caretOrnament() == 0, "the turn did not come off");
        // A finger and a plucking finger, then none (ADR-0093).
        editor_->setFingering(static_cast<int>(core::Finger::Ring));
        check(
            editor_->caretFingering() == static_cast<int>(core::Finger::Ring) &&
                score_->undoName() == ScoreDocument::tr("Change fingering") &&
                editor_->caretStatus().contains(
                    CaretEditor::tr("Finger %1")
                        .arg(editor_->caretFretted() ? QStringLiteral("3") : QStringLiteral("4"))),
            "the fingering did not go on");
        editor_->setFingering(0);
        check(editor_->caretFingering() == 0, "the fingering did not come off");
        if (editor_->caretFretted()) {
            editor_->setPluck(static_cast<int>(core::Finger::Index));
            check(editor_->caretPluck() == static_cast<int>(core::Finger::Index) &&
                      score_->undoName() == ScoreDocument::tr("Change plucking finger") &&
                      editor_->caretStatus().contains(
                          CaretEditor::tr("Plucked with %1").arg(QStringLiteral("i"))),
                  "the plucking finger did not go on");
            editor_->setPluck(0);
            check(editor_->caretPluck() == 0, "the plucking finger did not come off");
        }
        const QString status = editor_->caretStatus();
        check(status.contains(CaretEditor::tr("Wide vibrato")) &&
                  status.contains(CaretEditor::tr("Marcato")) &&
                  status.contains(editor_->slideInName(1)) &&
                  status.contains(editor_->slideOutName(3)),
              "the status does not name the kinds of effects");
        // The whammy bar and the bass techniques (ADR-0060).
        constexpr int kDip = static_cast<int>(core::BendForm::BendRelease);
        editor_->setWhammy(kDip, -300);
        editor_->setBassTechnique(static_cast<int>(core::BassTechnique::Slap));
        check(editor_->caretWhammyForm() == kDip && editor_->caretWhammyCents() == -300 &&
                  editor_->whammyAmount(-300) == QStringLiteral("-1 1/2") &&
                  editor_->caretBassTechnique() == static_cast<int>(core::BassTechnique::Slap) &&
                  editor_->caretTremolo() == 16,
              "the whammy bar and the slap did not join the beat's effects");
        check(editor_->caretStatus().contains(editor_->whammyName(kDip, -300)) &&
                  editor_->caretStatus().contains(CaretEditor::tr("Slap")),
              "the status does not name the whammy bar and the slap");
        editor_->setWhammy(0, 0);
        check(editor_->caretWhammyForm() == 0 && editor_->caretBassTechnique() != 0,
              "the whammy bar did not go on its own");
        // Hairpins (ADR-0078).
        editor_->setHairpin(static_cast<int>(core::Hairpin::Diminuendo));
        check(editor_->caretHairpin() == static_cast<int>(core::Hairpin::Diminuendo) &&
                  editor_->caretBassTechnique() != 0,
              "the diminuendo did not join the beat's effects");
        check(editor_->caretStatus().contains(CaretEditor::tr("Diminuendo")),
              "the status does not name the diminuendo");
        editor_->setHairpin(0);
        check(editor_->caretHairpin() == 0, "the diminuendo did not go");
        // Fermatas (ADR-0085).
        editor_->setFermata(static_cast<int>(core::Fermata::Long));
        check(editor_->caretFermata() == static_cast<int>(core::Fermata::Long) &&
                  editor_->caretBassTechnique() != 0,
              "the long fermata did not join the beat's effects");
        check(editor_->caretStatus().contains(CaretEditor::tr("Long fermata")),
              "the status does not name the long fermata");
        editor_->setFermata(0);
        check(editor_->caretFermata() == 0, "the fermata did not go");
        // Octave lines (ADR-0089).
        editor_->setOttava(static_cast<int>(core::Ottava::QuindicesimaAlta));
        check(editor_->caretOttava() == static_cast<int>(core::Ottava::QuindicesimaAlta) &&
                  editor_->caretBassTechnique() != 0,
              "the octave line did not join the beat's effects");
        check(editor_->caretStatus().contains(
                  CaretEditor::tr("Octave line %1").arg(QStringLiteral("15ma"))),
              "the status does not name the octave line");
        editor_->setOttava(0);
        check(editor_->caretOttava() == 0, "the octave line did not go");
        // The sustain pedal (ADR-0092).
        editor_->setPedal(static_cast<int>(core::Pedal::Change));
        check(editor_->caretPedal() == static_cast<int>(core::Pedal::Change) &&
                  editor_->caretBassTechnique() != 0,
              "the pedal did not join the beat's effects");
        check(editor_->caretStatus().contains(CaretEditor::tr("Pedal change")),
              "the status does not name the pedal change");
        editor_->setPedal(0);
        check(editor_->caretPedal() == 0, "the pedal did not come up");
        // A caesura after the beat, then none (ADR-0095).
        editor_->setBreath(static_cast<int>(core::Breath::Caesura));
        check(editor_->caretBreath() == static_cast<int>(core::Breath::Caesura) &&
                  editor_->caretBassTechnique() != 0 &&
                  editor_->caretStatus().contains(CaretEditor::tr("Caesura")),
              "the caesura did not join the beat's effects");
        editor_->setBreath(0);
        check(editor_->caretBreath() == 0, "the caesura did not go");
        // Slurs (ADR-0086).
        editor_->setSlur(true);
        check(editor_->caretSlur() && editor_->caretBassTechnique() != 0,
              "the slur did not join the beat's effects");
        check(editor_->caretStatus().contains(CaretEditor::tr("Slur")),
              "the status does not name the slur");
        editor_->setSlur(false);
        check(!editor_->caretSlur(), "the slur did not go");
        // Grace notes (ADR-0079): the beat may already lead in with some,
        // which the toggle takes away first.
        if (editor_->caretGraceNotes() > 0) {
            editor_->toggleGraceNote();
            check(editor_->caretGraceNotes() == 0 &&
                      score_->undoName() == ScoreDocument::tr("Remove grace notes"),
                  "the beat's grace notes did not go");
        }
        editor_->toggleGraceNote();
        check(editor_->caretGraceNotes() == 1 &&
                  score_->undoName() == ScoreDocument::tr("Add grace note") &&
                  editor_->caretStatus().contains(CaretEditor::tr("Grace notes")),
              "no grace note joined the caret's beat");
        editor_->toggleGraceNote();
        check(editor_->caretGraceNotes() == 0, "the grace note did not go");
        // Chord symbols (ADR-0080): named, offered again, taken away.
        const QString chordBefore = editor_->caretChord();
        editor_->setChordSymbol(QStringLiteral("Am7"));
        check(editor_->caretChord() == QStringLiteral("Am7"), "the caret's beat was not named Am7");
        check(score_->undoName() == ScoreDocument::tr("Set chord symbol"),
              "naming a chord was not one undo step");
        check(editor_->caretStatus().contains(CaretEditor::tr("Chord %1").arg("Am7")),
              "the status does not name the chord");
        check(editor_->songChords().contains(QStringLiteral("Am7")),
              "the popup does not offer the chord");
        editor_->setChordSymbol(QString());
        check(editor_->caretChord().isEmpty() &&
                  score_->undoName() == ScoreDocument::tr("Remove chord symbol"),
              "the chord symbol did not go");
        if (!chordBefore.isEmpty()) {
            editor_->setChordSymbol(chordBefore);
        }
        // Beat text (ADR-0081): written, offered again, taken away.
        const QString textBefore = editor_->caretText();
        editor_->setBeatText(QStringLiteral("let ring"));
        check(editor_->caretText() == QStringLiteral("let ring"),
              "the caret's beat did not get its text");
        check(score_->undoName() == ScoreDocument::tr("Set beat text"),
              "writing a text was not one undo step");
        check(editor_->caretStatus().contains(QStringLiteral("let ring")),
              "the status does not show the text");
        check(editor_->songTexts().contains(QStringLiteral("let ring")),
              "the popup does not offer the text");
        editor_->setBeatText(QString());
        check(editor_->caretText().isEmpty() &&
                  score_->undoName() == ScoreDocument::tr("Remove beat text"),
              "the text did not go");
        if (!textBefore.isEmpty()) {
            editor_->setBeatText(textBefore);
        }
        // Clef changes (ADR-0087): from the caret's measure on, then back.
        if (const int clefBefore = editor_->caretClef(); clefBefore >= 0) {
            const int other = clefBefore == 1 ? 0 : 1;
            editor_->setClef(other);
            check(editor_->caretClef() == other &&
                      score_->undoName() == ScoreDocument::tr("Change clef"),
                  "the clef did not change from the caret's measure");
            editor_->setClef(clefBefore);
            check(editor_->caretClef() == clefBefore, "the clef did not change back");
        }
        // Lyrics (ADR-0084): a new verse typed from the caret's measure, shown
        // by the lyrics popup as typed, then taken away.
        if (const int verse = editor_->caretVerseCount(); verse < ScoreDocument::maxVerses()) {
            const int leftover =
                editor_->setVerse(verse, score_->caretMeasure(), QStringLiteral("la_"));
            const QVariantMap typed = editor_->caretVerse(verse);
            check(leftover == 0 &&
                      typed.value(QStringLiteral("text")).toString() == QStringLiteral("la_") &&
                      typed.value(QStringLiteral("first")).toInt() >= score_->caretMeasure() &&
                      editor_->caretVerseCount() == verse + 1 &&
                      score_->undoName() == ScoreDocument::tr("Set lyrics"),
                  "the verse did not go on as typed");
            auto* popup = window_->findChild<QObject*>(QStringLiteral("lyricsPopup"));
            check(popup != nullptr, "there is no lyrics popup");
            if (popup != nullptr) {
                popup->setProperty("verse", verse);
                QMetaObject::invokeMethod(popup, "open");
                check(popup->property("text").toString() == QStringLiteral("la_"),
                      "the lyrics popup does not show the verse");
                QMetaObject::invokeMethod(popup, "close");
            }
            editor_->setVerse(verse, 0, QString());
            check(editor_->caretVerseCount() == verse &&
                      score_->undoName() == ScoreDocument::tr("Remove lyrics"),
                  "the verse did not go");
        }
        // A dynamic from the caret's notes on (ADR-0047).
        constexpr int kLoud = static_cast<int>(core::Dynamic::FF);
        const int dynamic = editor_->caretDynamic() == kLoud ? kLoud - 1 : kLoud;
        editor_->setDynamic(dynamic);
        check(editor_->caretDynamic() == dynamic &&
                  score_->undoName() == ScoreDocument::tr("Change dynamic"),
              "the dynamic did not go on as one undo step");
        if (editor_->caretFretted()) {
            constexpr int kArtificial = static_cast<int>(core::HarmonicKind::Artificial);
            editor_->setHarmonic(kArtificial);
            check(editor_->caretHarmonic() == kArtificial &&
                      editor_->caretStatus().contains(editor_->harmonicName(kArtificial)),
                  "the artificial harmonic did not go on");
        }
        for (int i = 0; i < 64 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!effecting_) {
            return false;
        }
        check(!score_->modified(), "undoing the effects did not restore the song");
        return false;
    });
}

void Startup::pressKey(int key, Qt::KeyboardModifiers modifiers, const QString& text) {
    for (const QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        qt_handleKeyEvent(window_, type, key, modifiers, text, false, 1);
    }
}

void Startup::typeText(const QString& text) {
    for (const QChar c : text) {
        // A printable key's code is its character, in capitals.
        pressKey(static_cast<int>(c.toUpper().unicode()), Qt::NoModifier, QString(c));
    }
}

// The command palette (ADR-0135), typed into as a person would: Ctrl+E opens
// it, a few letters find a note effect, Enter puts it on the caret's note and
// closes; Ctrl+E and Enter run the command used last again. A number goes to
// its measure, with playback; "tempo 90" sets the tempo there, and the space
// typed into it plays nothing; Escape closes it with nothing done, and Ctrl+E
// opens it again as it fades and closes it. Undone, the song must end as it
// started.
void Startup::queueCommands() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("command palette check failed: %1").arg(QLatin1StringView(what));
        }
    };
    constexpr int kPalmMute = static_cast<int>(core::NoteEffect::PalmMute);
    const auto palmMuted = [this] {
        return (editor_->caretEffects() & (1 << kPalmMute)) != 0;
    };
    const auto chosen = [this] {
        return commandPalette_ != nullptr ? commandPalette_->property("chosenId").toString()
                                          : QString();
    };
    const auto shows = [this] {
        return commandPalette_ != nullptr && commandPalette_->property("visible").toBool();
    };
    // Another popup holds the window's shortcuts while it shows, fading out
    // included (the lyrics popup the effects opened and closed): Ctrl+E waits
    // for it to go, as a person sees it go. Tooltips hold nothing.
    const auto anotherPopupShows = [this] {
        const auto objects = window_->findChildren<QObject*>();
        return std::ranges::any_of(objects, [this](const QObject* object) {
            return object != commandPalette_ && object->inherits("QQuickPopup") &&
                   !object->inherits("QQuickToolTip") && object->property("visible").toBool();
        });
    };
    // Where the keys went, for a failure: the window with the keyboard, the
    // item with the focus and the popups that show.
    const auto keyboard = [this] {
        const QWindow* focused = QGuiApplication::focusWindow();
        const QQuickItem* item = window_->activeFocusItem();
        QStringList popups;
        for (const QObject* object : window_->findChildren<QObject*>()) {
            if (object->inherits("QQuickPopup") && object->property("visible").toBool()) {
                popups << QStringLiteral("%1 %2").arg(QLatin1StringView(object->metaObject()->className()),
                                                      object->objectName());
            }
        }
        return QStringLiteral("keys to %1, focus on %2 %3, popups: %4")
            .arg(focused == window_    ? QStringLiteral("the window")
                 : focused == nullptr ? QStringLiteral("no window")
                                       : QLatin1StringView(focused->metaObject()->className()),
                 item != nullptr ? QLatin1StringView(item->metaObject()->className())
                                 : QLatin1StringView("nothing"),
                 item != nullptr ? item->objectName() : QString(),
                 popups.isEmpty() ? QStringLiteral("none") : popups.join(QStringLiteral(", ")));
    };
    // A note of its own, typed once the wait for a second digit has ended.
    steps_.emplace_back([this, check] {
        commandPalette_ = window_->findChild<QObject*>(QStringLiteral("commandPalette"));
        check(commandPalette_ != nullptr, "the window has none");
        commanding_ =
            commandPalette_ != nullptr && score_->caretPage() >= 0 && editor_->caretFretted();
        if (!commanding_) {
            return false;
        }
        score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        score_->moveCaret(ScoreDocument::CaretMove::StringUp);
        score_->typeDigit(7);
        score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        score_->moveCaret(ScoreDocument::CaretMove::StringUp);
        return true;
    });
    // Its command runs once it has closed: the step waits for the edit.
    steps_.emplace_back([this, check, palmMuted, chosen, shows, anotherPopupShows, keyboard] {
        if (!commanding_) {
            return false;
        }
        if (anotherPopupShows()) {
            again_ = true;
            return false;
        }
        check(!palmMuted(), "the note typed is already palm muted");
        pressKey(Qt::Key_E, Qt::ControlModifier);
        if (!shows()) {
            // What would be typed into it must not go into the score.
            check(false, qPrintable(QStringLiteral("Ctrl+E did not open it (%1)").arg(keyboard())));
            commanding_ = false;
            return false;
        }
        check(commandPalette_->property("commandCount").toInt() > 150,
              "it offers fewer commands than the window has");
        typeText(QStringLiteral("palm"));
        check(chosen() == QStringLiteral("effect.%1").arg(kPalmMute),
              "palm did not find palm mute");
        pressKey(Qt::Key_Return);
        return true;
    });
    steps_.emplace_back([this, check, palmMuted, chosen, shows] {
        if (!commanding_) {
            return false;
        }
        check(palmMuted() && score_->undoName() == ScoreDocument::tr("Change effect"),
              "Enter did not palm mute the caret's note");
        check(!commandPalette_->property("opened").toBool(), "Enter did not close it");
        pressKey(Qt::Key_E, Qt::ControlModifier);
        check(shows() && chosen() == QStringLiteral("effect.%1").arg(kPalmMute),
              "it did not open on the command used last");
        pressKey(Qt::Key_Return);
        return true;
    });
    steps_.emplace_back([this, check, palmMuted, chosen] {
        if (!commanding_) {
            return false;
        }
        check(!palmMuted(), "Ctrl+E and Enter did not run the command used last again");
        commandMeasure_ = std::min(3, score_->measureCount());
        pressKey(Qt::Key_E, Qt::ControlModifier);
        typeText(QString::number(commandMeasure_));
        check(chosen() == QStringLiteral("typed.measure"), "a number did not offer its measure");
        pressKey(Qt::Key_Return);
        // The caret moves without a layout: this waits a tick for the command.
        return true;
    });
    steps_.emplace_back([this, check, chosen] {
        if (!commanding_) {
            return false;
        }
        const auto* audio = window_->findChild<AudioController*>();
        check(score_->caretMeasure() == commandMeasure_ - 1,
              "the number did not take the caret to its measure");
        check(audio != nullptr &&
                  std::abs(audio->position() - score_->measureStart(commandMeasure_ - 1)) < 1e-6,
              "the number did not take playback to its measure");
        pressKey(Qt::Key_E, Qt::ControlModifier);
        typeText(QStringLiteral("tempo 90"));
        check(audio != nullptr && !audio->playingSong(), "a space typed into it started playback");
        check(chosen() == QStringLiteral("typed.tempo"), "tempo 90 was not offered");
        pressKey(Qt::Key_Return);
        return true;
    });
    steps_.emplace_back([this, check, shows] {
        if (!commanding_) {
            return false;
        }
        check(score_->caretTempo().value(QStringLiteral("bpm")).toInt() == 90,
              "tempo 90 did not set the tempo at the caret's measure");
        const QString undo = score_->undoName();
        pressKey(Qt::Key_E, Qt::ControlModifier);
        typeText(QStringLiteral("zqzqzq"));
        check(commandPalette_->property("rowCount").toInt() == 0, "nonsense found commands");
        pressKey(Qt::Key_Escape);
        check(!commandPalette_->property("opened").toBool() && score_->undoName() == undo,
              "Escape did not close it with nothing done");
        // Its keys open it again as it fades, and close it.
        pressKey(Qt::Key_E, Qt::ControlModifier);
        check(shows() && !commandPalette_->property("leaving").toBool() &&
                  commandPalette_->property("rowCount").toInt() > 0,
              "Ctrl+E did not open it again as it closed");
        pressKey(Qt::Key_E, Qt::ControlModifier);
        check(commandPalette_->property("leaving").toBool(), "Ctrl+E did not close it");
        for (int i = 0; i < 6 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!commanding_) {
            return false;
        }
        check(!score_->modified(), "undoing what it did did not restore the song");
        return false;
    });
}

void Startup::tick() {
    // The panel closes again once the smoke test clicks the score; it must
    // have opened at some point.
    if (effectsPanel_ != nullptr && effectsPanel_->property("opened").toBool()) {
        effectsOpened_ = true;
    }
    if (options_.commands && !commandsOpened_ && window_ != nullptr) {
        const auto* palette = window_->findChild<QObject*>(QStringLiteral("commandPalette"));
        commandsOpened_ = palette != nullptr && palette->property("opened").toBool();
    }
    if (!options_.menu.isEmpty() && !menuOpened_ && window_ != nullptr) {
        const auto* menu = window_->findChild<QObject*>(options_.menu + QStringLiteral("Menu"));
        menuOpened_ = menu != nullptr && menu->property("visible").toBool();
    }
    // A popup the caret opened lies inside the view it belongs to, however
    // short the window is: the measure popup scrolls rather than leave it.
    if (window_ != nullptr && popupProblem_.isEmpty()) {
        for (const QString& name : {QStringLiteral("measurePopup"), QStringLiteral("chordPopup"),
                                    QStringLiteral("textPopup"), QStringLiteral("lyricsPopup"),
                                    QStringLiteral("symbolCard")}) {
            const auto* popup = window_->findChild<QObject*>(name);
            if (popup == nullptr || !popup->property("opened").toBool()) {
                continue;
            }
            const auto* view = popup->property("parent").value<QQuickItem*>();
            const double top = popup->property("y").toDouble();
            const double bottom = top + popup->property("height").toDouble();
            if (view != nullptr && (top < 0.0 || bottom > view->height() + 0.5)) {
                popupProblem_ = QStringLiteral("%1 (from %2 to %3) leaves its view, %4 high")
                                    .arg(name)
                                    .arg(top)
                                    .arg(bottom)
                                    .arg(view->height());
            }
        }
    }
    if (!options_.file.isEmpty() && score_ != nullptr &&
        score_->status() == ScoreDocument::Status::Error) {
        finish(false, score_->errorMessage());
        return;
    }
    if (waiting_) {
        // An edit that changes nothing drawn asks for no layout (ADR-0111).
        const bool laidOut =
            score_ != nullptr &&
            (score_->layoutWork().asked == layoutsAskedBefore_ ||
             (laidOut_ && (score_->pageCount() == 0 || pagesUpdated() > paintsBefore_)));
        const bool settled =
            laidOut && !score_->busy() && score_->status() == ScoreDocument::Status::Ready;
        if (!settled) {
            if (clock_.elapsed() - stepStarted_ > kStepTimeoutMilliseconds) {
                finish(false, QStringLiteral("timed out"));
            }
            return;
        }
        waiting_ = false;
        // Playback plays the song the pages show, once its layout is done:
        // an edit's sequence is built with it (ADR-0031).
        if (score_ != nullptr && score_->song() && editProblem_.isEmpty() &&
            (!score_->sequence() ||
             *score_->sequence() != audio::Sequence(*score_->song(), score_->practice()))) {
            editProblem_ = QStringLiteral("playback does not play the song the pages show");
        }
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
        layoutsAskedBefore_ = score_ != nullptr ? score_->layoutWork().asked : 0;
        if (againSince_ < 0) {
            ++stepsRun_;
        }
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
        // Let animations settle first, the note effects palette's too once
        // `--effects` opened it, and the menu `--menu` names.
        if (((effectsPanel_ != nullptr && !effectsOpened_) ||
             (!options_.menu.isEmpty() && !menuOpened_) ||
             (options_.commands && !commandsOpened_)) &&
            clock_.elapsed() - stepStarted_ < kStepTimeoutMilliseconds) {
            return;
        }
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
    // A window given another size lays its items out at that size once its
    // resize event comes, which a loaded machine can deliver after the last
    // step: the bars are measured then.
    if (const QQuickItem* content = window_->contentItem();
        content != nullptr && content->size() != QSizeF(window_->size()) &&
        clock_.elapsed() - stepStarted_ < kStepTimeoutMilliseconds) {
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
    if (score_ != nullptr) {
        // What the edits asked of the workers and what they ran (ADR-0110),
        // after the outcome, which the checks read beside the lines before it.
        for (const auto& [name, work] : {std::pair{"layouts", score_->layoutWork()},
                                         std::pair{"sequences", score_->sequenceWork()}}) {
            std::printf("%s: %llu asked, %llu run, %llu skipped, %llu stale\n", name,
                        static_cast<unsigned long long>(work.asked),
                        static_cast<unsigned long long>(work.run),
                        static_cast<unsigned long long>(work.skipped),
                        static_cast<unsigned long long>(work.stale));
        }
        std::fflush(stdout);
    }
}

// Nothing may cover the score where the pointer goes, as a popup open at the
// caret or a menu can: they close first. False while one was still there, and
// the step runs again once it is gone.
bool Startup::uncoverScore() {
    for (const QString& name :
         {QStringLiteral("measurePopup"), QStringLiteral("chordPopup"), QStringLiteral("textPopup"),
          QStringLiteral("lyricsPopup"), QStringLiteral("symbolCard"), QStringLiteral("fileMenu"),
          QStringLiteral("editMenu"), QStringLiteral("viewMenu"), QStringLiteral("soundMenu"),
          QStringLiteral("helpMenu")}) {
        if (auto* popup = window_->findChild<QObject*>(name);
            popup != nullptr && popup->property("visible").toBool()) {
            QMetaObject::invokeMethod(popup, "close");
            again_ = true;
            return false;
        }
    }
    // Nor may a notice: in a short window it lies over the foot of the score.
    if (auto* toast = window_->findChild<QQuickItem*>(QStringLiteral("toast"));
        toast != nullptr && toast->isVisible()) {
        toast->setProperty("shown", false);
        again_ = true;
        return false;
    }
    return true;
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
    if (!uncoverScore()) {
        return;
    }
    const QVariantMap cursor = score_->cursorAt(audio->position());
    const double expected = score_->secondsAt(
        cursor.value(QStringLiteral("page")).toInt(), cursor.value(QStringLiteral("x")).toDouble(),
        cursor.value(QStringLiteral("y")).toDouble() +
            cursor.value(QStringLiteral("height")).toDouble() / 2.0);
    const QPointF at = playhead->mapToScene(QPointF(playhead->width() / 2, playhead->height() / 2));
    // The view scrolls after the cursor when it stands out of sight, as in a
    // short window: the click waits until the view has stopped moving and the
    // cursor has stood in one place for two ticks.
    const auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    if (view == nullptr || view->property("settling").toBool() || !cursorRest_ ||
        *cursorRest_ != at) {
        cursorRest_ = at;
        again_ = true;
        return;
    }
    for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, at, at, window_->mapToGlobal(at), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(window_, &event);
    }
    if (expected < 0.0 || std::abs(audio->position() - expected) > 1e-6) {
        // What lay under the click, to tell a covered cursor from a misplaced one.
        QStringList under;
        QPointF point = at;
        for (QQuickItem* item = window_->contentItem(); item != nullptr;) {
            QQuickItem* child = item->childAt(point.x(), point.y());
            if (child == nullptr) {
                break;
            }
            point = item->mapToItem(child, point);
            under.append(child->objectName().isEmpty()
                             ? QString::fromLatin1(child->metaObject()->className())
                             : child->objectName());
            item = child;
        }
        failure_ = QStringLiteral("a click on the cursor at %1, %2 moved playback to %3 s, not "
                                  "%4 s; under it: %5")
                       .arg(at.x())
                       .arg(at.y())
                       .arg(audio->position())
                       .arg(expected)
                       .arg(under.join(QStringLiteral(" > ")));
        return;
    }
    // The transport reads the measure playing there, from 1, and the song
    // string carries it.
    const QString playing = QString::number(score_->measureAt(audio->position()) + 1);
    const auto* readout = window_->findChild<QQuickItem*>(QStringLiteral("measureReadout"));
    const auto* string = window_->findChild<QQuickItem*>(QStringLiteral("songString"));
    if (readout == nullptr || readout->property("value").toString() != playing) {
        failure_ = QStringLiteral("the transport does not read measure %1").arg(playing);
    } else if (string == nullptr || string->property("marker").toString() != playing) {
        failure_ = QStringLiteral("the song string does not carry measure %1").arg(playing);
    } else {
        // It has a tick where the measures start, every one or one in so
        // many, and a taller one where the caret's measure does.
        const int starts = static_cast<int>(score_->measureStarts().size());
        const int step = string->property("tickStep").toInt();
        const double caret = score_->measureStart(score_->caretMeasure());
        if (starts == 0 || step < 1 ||
            string->property("tickCount").toInt() != (starts - 1) / step) {
            failure_ = QStringLiteral("the song string has %1 ticks for %2 measures played")
                           .arg(string->property("tickCount").toInt())
                           .arg(starts);
        } else if (std::abs(string->property("caretSeconds").toDouble() - caret) > 1e-9) {
            failure_ = QStringLiteral("the song string does not mark the caret's measure");
        }
    }
}

// Practice (ADR-0024): half speed with the metronome, then back. Each builds
// the sequence again without a relayout, and playback keeps its place in the
// music: at half speed every time doubles.
void Startup::queuePractice() {
    const auto check = [this](bool ok, const QString& what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("practice check failed: %1").arg(what);
        }
    };
    // Waits for a sequence other than sequenceBefore_; false while there is none.
    const auto arrived = [this] {
        if (score_->sequence() == sequenceBefore_ || score_->busy()) {
            again_ = true;
            return false;
        }
        return true;
    };
    steps_.emplace_back([this] {
        auto* audio = window_->findChild<AudioController*>();
        if (audio == nullptr || !score_->sequence()) {
            return false;
        }
        sequenceBefore_ = score_->sequence();
        positionBefore_ = audio->position();
        score_->setSpeed(50);
        score_->setMetronome(true);
        return false;
    });
    steps_.emplace_back([this, check, arrived] {
        if (!sequenceBefore_ || !arrived()) {
            return false;
        }
        const auto sequence = score_->sequence();
        const double position = window_->findChild<AudioController*>()->position();
        check(score_->speed() == 50 && score_->metronome() && sequence->speed() == 0.5 &&
                  sequence->metronome(),
              QStringLiteral("half speed with the metronome was not played"));
        check(std::abs(position - 2.0 * positionBefore_) < 1e-6,
              QStringLiteral("half speed moved playback from %1 s to %2 s, not %3 s")
                  .arg(positionBefore_)
                  .arg(position)
                  .arg(2.0 * positionBefore_));
        sequenceBefore_ = sequence;
        score_->setSpeed(100);
        score_->setMetronome(false);
        return false;
    });
    steps_.emplace_back([this, check, arrived] {
        if (!sequenceBefore_ || !arrived()) {
            return false;
        }
        const double position = window_->findChild<AudioController*>()->position();
        check(*score_->sequence() == audio::Sequence(*score_->song()),
              QStringLiteral("full speed did not play the song as written"));
        check(std::abs(position - positionBefore_) < 1e-6,
              QStringLiteral("full speed left playback at %1 s, not %2 s")
                  .arg(position)
                  .arg(positionBefore_));
        sequenceBefore_.reset();
        return false;
    });
}

// The A-B loop is marked at playback's position, A then B; B before A marks A
// again, and a third mark ends the loop. It keeps its place in the music when
// the speed changes. The loop is left marked, for queueKeepPosition() to check
// that an edit keeps it and a new song forgets it, and playback where it was.
void Startup::queueLoop() {
    const auto check = [this](bool ok, const QString& what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("loop check failed: %1").arg(what);
        }
    };
    const auto arrived = [this] {
        if (score_->sequence() == sequenceBefore_ || score_->busy()) {
            again_ = true;
            return false;
        }
        return true;
    };
    const auto near = [](double a, double b) {
        return std::abs(a - b) < 1e-6;
    };
    steps_.emplace_back([this, check, near] {
        auto* audio = window_->findChild<AudioController*>();
        if (audio == nullptr || !score_->sequence()) {
            return false;
        }
        positionBefore_ = audio->position();
        const double quarter = score_->duration() / 4.0;
        audio->seek(quarter);
        audio->markLoop();
        check(audio->loopMarked() && !audio->looping() && near(audio->loopFrom(), quarter),
              QStringLiteral("A was not marked at %1 s").arg(quarter));
        audio->seek(2.0 * quarter);
        audio->markLoop();
        check(audio->looping() && near(audio->loopFrom(), quarter) &&
                  near(audio->loopTo(), 2.0 * quarter),
              QStringLiteral("A-B is %1 s to %2 s, not %3 s to %4 s")
                  .arg(audio->loopFrom())
                  .arg(audio->loopTo())
                  .arg(quarter)
                  .arg(2.0 * quarter));
        sequenceBefore_ = score_->sequence();
        score_->setSpeed(50);
        return false;
    });
    steps_.emplace_back([this, check, arrived, near] {
        if (!sequenceBefore_ || !arrived()) {
            return false;
        }
        auto* audio = window_->findChild<AudioController*>();
        const double quarter = score_->duration() / 8.0; // the song lasts twice as long
        check(audio->looping() && near(audio->loopFrom(), 2.0 * quarter) &&
                  near(audio->loopTo(), 4.0 * quarter),
              QStringLiteral("at half speed A-B is %1 s to %2 s, not %3 s to %4 s")
                  .arg(audio->loopFrom())
                  .arg(audio->loopTo())
                  .arg(2.0 * quarter)
                  .arg(4.0 * quarter));
        sequenceBefore_ = score_->sequence();
        score_->setSpeed(100);
        return false;
    });
    steps_.emplace_back([this, check, arrived, near] {
        if (!sequenceBefore_ || !arrived()) {
            return false;
        }
        sequenceBefore_.reset();
        auto* audio = window_->findChild<AudioController*>();
        const double quarter = score_->duration() / 4.0;
        check(audio->looping() && near(audio->loopFrom(), quarter) &&
                  near(audio->loopTo(), 2.0 * quarter),
              QStringLiteral("back at full speed A-B is %1 s to %2 s")
                  .arg(audio->loopFrom())
                  .arg(audio->loopTo()));
        audio->markLoop();
        check(!audio->looping() && !audio->loopMarked(),
              QStringLiteral("a third mark did not end the loop"));
        audio->seek(2.0 * quarter);
        audio->markLoop();
        audio->seek(quarter);
        audio->markLoop();
        check(audio->loopMarked() && !audio->looping() && near(audio->loopFrom(), quarter),
              QStringLiteral("B before A did not mark A again"));
        audio->seek(2.0 * quarter);
        audio->markLoop();
        check(audio->looping(), QStringLiteral("A-B was not marked again"));
        audio->setCountIn(true);
        check(audio->countIn(), QStringLiteral("the count-in did not turn on"));
        audio->setCountIn(false);
        audio->seek(positionBefore_);
        return false;
    });
}

// The SoundFont given with `--soundfont` opens on a worker; once it has, the
// window plays through it and prints its name. A file that is no SoundFont
// then fails with a message and leaves it in place, and the built-in
// synthesizer comes back.
void Startup::queueSoundFont() {
    const auto check = [this](bool ok, const QString& what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("SoundFont check failed: %1").arg(what);
        }
    };
    const auto opened = [this](AudioController* audio) {
        if (audio == nullptr || audio->loadingSoundFont()) {
            again_ = true;
            return false;
        }
        return true;
    };
    steps_.emplace_back([this, check, opened] {
        auto* audio = window_->findChild<AudioController*>();
        if (!opened(audio)) {
            return false;
        }
        std::printf("sound: %s\n", qPrintable(audio->soundFontName()));
        check(audio->soundFont() == options_.soundFont && audio->errorMessage().isEmpty(),
              QStringLiteral("%1 did not open: %2").arg(options_.soundFont, audio->errorMessage()));
        audio->setSoundFont(options_.soundFont + QStringLiteral(".missing"));
        return false;
    });
    steps_.emplace_back([this, check, opened] {
        auto* audio = window_->findChild<AudioController*>();
        if (!opened(audio)) {
            return false;
        }
        check(!audio->errorMessage().isEmpty() && audio->soundFont() == options_.soundFont,
              QStringLiteral("a missing file did not fail, or replaced the SoundFont"));
        audio->useBuiltinSound();
        check(audio->soundFont().isEmpty() && audio->soundFontName().isEmpty(),
              QStringLiteral("the built-in synthesizer did not come back"));
        return false;
    });
}

// The SFZ instruments given with `--guitar-sfz` and `--bass-sfz` open on a
// worker; once they have, the window prints the sound of each track. A file
// that is no instrument then fails with a message and leaves the basses' in
// place; taking that away gives the basses the sound of the other tracks
// again, and the built-in synthesizer comes back for all.
void Startup::queueSfz() {
    const auto check = [this](bool ok, const QString& what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("SFZ check failed: %1").arg(what);
        }
    };
    const auto opened = [this](AudioController* audio) {
        if (audio == nullptr || audio->loadingSoundFont()) {
            again_ = true;
            return false;
        }
        return true;
    };
    steps_.emplace_back([this, check, opened] {
        auto* audio = window_->findChild<AudioController*>();
        if (!opened(audio)) {
            return false;
        }
        QStringList sounds;
        for (int track = 0; track < score_->trackNames().size(); ++track) {
            sounds << audio->trackSound(track);
        }
        const auto orNone = [](const QString& name) {
            return name.isEmpty() ? QStringLiteral("none") : name;
        };
        std::printf("sfz: guitars %s, basses %s\nsounds: %s\n",
                    qPrintable(orNone(audio->guitarSfzName())),
                    qPrintable(orNone(audio->bassSfzName())),
                    qPrintable(sounds.join(QStringLiteral(", "))));
        check(audio->guitarSfz() == options_.guitarSfz && audio->bassSfz() == options_.bassSfz &&
                  audio->errorMessage().isEmpty(),
              QStringLiteral("the instruments did not open: %1").arg(audio->errorMessage()));
        if (!options_.bassSfz.isEmpty()) {
            audio->setSfz(QStringLiteral("basses"), options_.bassSfz + QStringLiteral(".missing"));
        }
        return false;
    });
    steps_.emplace_back([this, check, opened] {
        auto* audio = window_->findChild<AudioController*>();
        if (!opened(audio)) {
            return false;
        }
        if (!options_.bassSfz.isEmpty()) {
            check(!audio->errorMessage().isEmpty() && audio->bassSfz() == options_.bassSfz,
                  QStringLiteral("a missing file did not fail, or replaced the instrument"));
            audio->clearSfz(QStringLiteral("basses"));
            check(audio->bassSfz().isEmpty() && audio->guitarSfz() == options_.guitarSfz,
                  QStringLiteral("the basses' instrument did not go, or took the guitars'"));
        }
        audio->useBuiltinSound();
        check(audio->guitarSfz().isEmpty() && audio->bassSfz().isEmpty() &&
                  audio->soundFont().isEmpty(),
              QStringLiteral("the built-in synthesizer did not come back"));
        return false;
    });
}

// An edit keeps playback where it is: a track added, then undone, each give
// playback a new sequence and leave it where the click on the cursor put it.
// A new song then starts playback from its beginning. The A-B loop that
// queueLoop() left stays through the edit and goes with the new song.
// The score exported as PDF (ADR-0074) into a folder of its own: a whole
// document with as many pages as the score shows.
void Startup::queuePdf() {
    steps_.emplace_back([this] {
        if (!scratch_) {
            scratch_.emplace();
        }
        exported_.reset();
        // ".pdf" is added.
        score_->exportPdf(QUrl::fromLocalFile(scratch_->filePath(QStringLiteral("score"))));
        return false;
    });
    // Laid out and written on a worker (ADR-0107).
    steps_.emplace_back([this] {
        const auto problem = [this](const QString& what) {
            if (editProblem_.isEmpty()) {
                editProblem_ = QStringLiteral("PDF export: %1").arg(what);
            }
        };
        if (!exported_) {
            again_ = true;
            return false;
        }
        if (!*exported_) {
            problem(QStringLiteral("it failed"));
            return false;
        }
        QFile written(scratch_->filePath(QStringLiteral("score.pdf")));
        if (!written.open(QIODevice::ReadOnly)) {
            problem(QStringLiteral("no file was written"));
            return false;
        }
        // Export as PDF… suggests the song's file with ".pdf" for its extension.
        if (score_->source().isLocalFile()) {
            const QFileInfo source(score_->source().toLocalFile());
            const QFileInfo suggested(score_->exportFile(QStringLiteral("pdf")).toLocalFile());
            if (suggested.dir() != source.dir() ||
                suggested.fileName() != source.completeBaseName() + QStringLiteral(".pdf")) {
                problem(QStringLiteral("%1 suggested for %2")
                            .arg(suggested.filePath(), source.filePath()));
            }
        } else if (!score_->exportFile(QStringLiteral("pdf"))
                        .toLocalFile()
                        .endsWith(QStringLiteral(".pdf"))) {
            problem(QStringLiteral("no PDF file suggested for a song without a file"));
        }
        const QByteArray pdf = written.readAll();
        const qsizetype pages = pdf.count("/Type /Page ");
        if (!pdf.startsWith("%PDF-1.7\n") || !pdf.endsWith("%%EOF\n")) {
            problem(QStringLiteral("the file is no whole PDF document"));
        } else if (pages != score_->pageCount()) {
            problem(
                QStringLiteral("%1 pages for a score of %2").arg(pages).arg(score_->pageCount()));
        }
        std::printf("pdf: %lld %s, %lld bytes\n", static_cast<long long>(pages),
                    pages == 1 ? "page" : "pages", static_cast<long long>(pdf.size()));
        return false;
    });
}

// The song exported as MusicXML and as MIDI, beside Export as PDF…: files that
// read back, MusicXML with every track, each export reporting what its format
// left out, and the song still unmodified. Prints "musicxml: <tracks> tracks,
// <warnings> warnings" and "midi: <bytes> bytes, <warnings> warnings".
void Startup::queueExport() {
    steps_.emplace_back([this] {
        const auto problem = [this](const QString& what) {
            if (editProblem_.isEmpty()) {
                editProblem_ = QStringLiteral("export: %1").arg(what);
            }
        };
        const bool modified = score_->modified();
        QStringList reported;
        int reports = 0;
        const auto connection = QObject::connect(
            score_, &ScoreDocument::report,
            [&reported, &reports](const QString& /*message*/, const QStringList& details) {
                reported = details;
                ++reports;
            });
        const QTemporaryDir folder;
        // ".musicxml" and ".mid" are added.
        const bool xmlWritten =
            score_->exportMusicXml(QUrl::fromLocalFile(folder.filePath(QStringLiteral("xml"))));
        const QStringList xmlWarnings = reported;
        reported.clear();
        const bool midiWritten =
            score_->exportMidi(QUrl::fromLocalFile(folder.filePath(QStringLiteral("midi"))));
        const QStringList midiWarnings = reported;
        QObject::disconnect(connection);
        if (!xmlWritten || !midiWritten || reports != 2) {
            problem(QStringLiteral("an export failed or reported nothing"));
            return false;
        }
        const auto xml = elara::io::importMusicXmlFile(
            folder.filePath(QStringLiteral("xml.musicxml")).toStdString());
        const auto midi =
            elara::io::importMidiFile(folder.filePath(QStringLiteral("midi.mid")).toStdString());
        if (!xml || !midi) {
            problem(QStringLiteral("an exported file does not read back"));
            return false;
        }
        if (xml->song.trackCount() != score_->song()->trackCount()) {
            problem(QStringLiteral("MusicXML read back with %1 tracks for %2")
                        .arg(xml->song.trackCount())
                        .arg(score_->song()->trackCount()));
        }
        if (score_->modified() != modified) {
            problem(QStringLiteral("exporting changed the song's modified state"));
        }
        // Each export suggests the song's file with its own extension.
        for (const QString& extension : {QStringLiteral("musicxml"), QStringLiteral("mid")}) {
            if (!score_->exportFile(extension).toLocalFile().endsWith(QLatin1Char('.') +
                                                                      extension)) {
                problem(QStringLiteral("no .%1 file suggested").arg(extension));
            }
        }
        std::printf("musicxml: %lld tracks, %lld warnings\n",
                    static_cast<long long>(xml->song.trackCount()),
                    static_cast<long long>(xmlWarnings.size()));
        std::printf(
            "midi: %lld bytes, %lld warnings\n",
            static_cast<long long>(QFileInfo(folder.filePath(QStringLiteral("midi.mid"))).size()),
            static_cast<long long>(midiWarnings.size()));
        return false;
    });
}

void Startup::queueKeepPosition() {
    const auto check = [this](const char* what) {
        auto* audio = window_->findChild<AudioController*>();
        if (audio != nullptr && audio->position() != positionBefore_ && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("%1 moved playback to %2 s, not %3 s")
                               .arg(QLatin1StringView(what))
                               .arg(audio->position())
                               .arg(positionBefore_);
        }
    };
    steps_.emplace_back([this] {
        auto* audio = window_->findChild<AudioController*>();
        if (audio == nullptr) {
            return false;
        }
        positionBefore_ = audio->position();
        score_->addTrack(QStringLiteral("electric-bass"));
        return true;
    });
    steps_.emplace_back([this, check] {
        check("an edit");
        auto* audio = window_->findChild<AudioController*>();
        if (audio != nullptr && !audio->looping() && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("an edit ended the loop");
        }
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check("an undo");
        // Made while an edit's sequence is still being built, the new song
        // must still be what playback plays once everything settles.
        score_->addTrack(QStringLiteral("electric-bass"));
        score_->newSong(QStringLiteral("electric-guitar"));
        return true;
    });
    // Another song plays from its start, without the warnings of the file
    // opened before it.
    steps_.emplace_back([this] {
        if (!score_->importWarnings().isEmpty() && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("a new song kept the import warnings");
        }
        auto* audio = window_->findChild<AudioController*>();
        if (audio != nullptr && audio->position() != 0.0 && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("a new song left playback at %1 s").arg(audio->position());
        }
        if (audio != nullptr && (audio->looping() || audio->loopMarked()) &&
            editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("a new song kept the loop");
        }
        return false;
    });
}

// The groups of the command bar (the menus, the file tools, the transport and
// the panels) and those of the tool row (the entry tools, the track picker and
// the view switch) must each lie inside the window, beside the others of their
// bar, however narrow the window is.
QString Startup::overlappingBars() const {
    const auto crowded = [this](const QStringList& names) -> QString {
        QList<std::pair<QString, QRectF>> shown;
        for (const QString& name : names) {
            auto* item = window_->findChild<QQuickItem*>(name);
            if (item == nullptr) {
                return QStringLiteral("the window has no %1").arg(name);
            }
            // Measured as laid out: a bar that has just changed is arranged
            // with the next frame.
            if (QQuickItem* bar = item->parentItem()) {
                bar->ensurePolished();
            }
            if (!item->isVisible() || item->width() <= 0.0) {
                continue;
            }
            const QRectF rect =
                item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
            if (rect.left() < -0.5 || rect.right() > window_->width() + 0.5) {
                return QStringLiteral("%1 (from %2 to %3) leaves the window (%4 wide)")
                    .arg(name)
                    .arg(rect.left())
                    .arg(rect.right())
                    .arg(window_->width());
            }
            for (const auto& [other, otherRect] : shown) {
                if (rect.intersects(otherRect)) {
                    return QStringLiteral("%1 (from %2 to %3) overlaps %4 (from %5 to %6)")
                        .arg(name)
                        .arg(rect.left())
                        .arg(rect.right())
                        .arg(other)
                        .arg(otherRect.left())
                        .arg(otherRect.right());
                }
            }
            shown.emplace_back(name, rect);
        }
        return {};
    };
    if (const QString problem =
            crowded({QStringLiteral("menus"), QStringLiteral("fileTools"),
                     QStringLiteral("transport"), QStringLiteral("panelTools")});
        !problem.isEmpty()) {
        return problem;
    }
    // The bar gives up the metronome before the measure it reads.
    const auto* measure = window_->findChild<QQuickItem*>(QStringLiteral("measureReadout"));
    const auto* metronome = window_->findChild<QQuickItem*>(QStringLiteral("metronomeButton"));
    if (measure != nullptr && metronome != nullptr && metronome->isVisible() &&
        !measure->isVisible()) {
        return QStringLiteral("the command bar shows the metronome and not the measure");
    }
    return crowded(
        {QStringLiteral("entryBar"), QStringLiteral("trackPicker"), QStringLiteral("viewSwitch")});
}

void Startup::finish(bool ok, const QString& detail) {
    timer_.stop();
    QString outcome = detail;
    if (ok && qmlWarnings_) {
        ok = false;
        outcome = QStringLiteral("QML warnings");
    }
    // `--effects` opened the note effects palette at the caret, and `--menu`
    // its menu.
    if (ok && options_.smokeTest && options_.effects && !effectsOpened_) {
        ok = false;
        outcome = QStringLiteral("the note effects palette did not open");
    }
    if (ok && options_.smokeTest && options_.commands && !commandsOpened_) {
        ok = false;
        outcome = QStringLiteral("the command palette did not open");
    }
    if (ok && options_.smokeTest && !options_.menu.isEmpty() && !menuOpened_) {
        ok = false;
        outcome = QStringLiteral("the %1 menu did not open").arg(options_.menu);
    }
    if (ok && !popupProblem_.isEmpty()) {
        ok = false;
        outcome = popupProblem_;
    }
    // The window carries the app icon up to the largest size a taskbar asks
    // for (ADR-0064).
    if (ok && options_.smokeTest &&
        !QGuiApplication::windowIcon().availableSizes().contains(QSize(256, 256))) {
        ok = false;
        outcome = QStringLiteral("the window has no app icon");
    }
    if (ok && !options_.screenshot.isEmpty() && window_ != nullptr) {
        const QImage image = window_->grabWindow();
        if (image.isNull() || !image.save(options_.screenshot)) {
            ok = false;
            outcome = QStringLiteral("could not save %1").arg(options_.screenshot);
        }
    }
    reportSmokeTest(ok, outcome);
    // Which steps each layout followed, after the result the tests read: when
    // the count of layouts they expect is off on one platform, this says where.
    if (!layoutSteps_.isEmpty()) {
        std::printf("layouts after steps:%s\n", qPrintable(layoutSteps_));
    }
    QCoreApplication::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

} // namespace elara::app
