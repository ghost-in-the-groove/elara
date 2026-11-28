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
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJSValue>
#include <QList>
#include <QMetaType>
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
#include <QWheelEvent>
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
// The title of the song app.smoke_recovery_crash leaves a copy of.
constexpr char kCrashedTitle[] = "Left by a crash";

// The tips of the tiles lit among the items `item` shows, as the note effects
// palette lights what the caret has.
void litTips(const QQuickItem* item, QStringList& tips) {
    if (item == nullptr || !item->isVisible()) {
        return;
    }
    if (const QVariant tip = item->property("tip");
        tip.isValid() && item->property("active").toBool()) {
        tips.append(tip.toString());
    }
    for (const QQuickItem* child : item->childItems()) {
        litTips(child, tips);
    }
}

// A map a QML item holds in a `var` property, as the document gave it.
QVariantMap mapOf(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>()
               ? value.value<QJSValue>().toVariant().toMap()
               : value.toMap();
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

// The items named `name` that `item` shows, itself included, searching what
// it draws as symbolHighlight() does: the pages' items are no QObject
// children of the window.
void shownItems(QQuickItem* item, QLatin1StringView name, QList<QQuickItem*>& found) {
    if (item == nullptr || !item->isVisible()) {
        return;
    }
    if (item->objectName() == name) {
        found.append(item);
    }
    for (QQuickItem* child : item->childItems()) {
        shownItems(child, name, found);
    }
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
    songs_ = window_ != nullptr ? window_->findChild<DocumentSet*>() : nullptr;
    score_ = songs_ != nullptr ? songs_->current() : nullptr;
    if (songs_ != nullptr) {
        // A file that does not open closes its song again (ADR-0160).
        connect(songs_, &DocumentSet::failed, this, [this](const QString& message) {
            if (openFailure_.isEmpty()) {
                openFailure_ = message;
            }
        });
    }
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
    return options_.smokeTest || !options_.screenshot.isEmpty() || options_.hold.has_value();
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
        if (options_.smokeTest && options_.holdStill) {
            queueHoldStill();
            return;
        }
        if (options_.smokeTest && options_.follow) {
            queueFollow();
            return;
        }
        // Without a file the smoke test edits a new song.
        if (options_.smokeTest) {
            steps_.emplace_back([this] {
                score_->newSong(QStringLiteral("electric-guitar"));
                return true;
            });
            queueRecoveryCopy();
            queueSave();
            queueSelection();
            queueMix();
            queueSymbols();
            queueLyricsPopup();
            queueEffectsPalette();
            queueCommands();
            queueContinuous();
            queueLayoutIssues();
        }
        return;
    }
    if (options_.smokeTest && options_.files.size() > 1) {
        queueSongs();
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
                return false;
            }
            // Zooming to fit left the view at the top of the first page, its
            // title whole (ADR-0173).
            const auto* pages = view->findChild<QQuickItem*>(QStringLiteral("pages"));
            const double top = pages != nullptr ? pages->property("originY").toDouble() -
                                                      pages->property("topMargin").toDouble()
                                                : 0.0;
            const double at = pages != nullptr ? pages->property("contentY").toDouble() : -1e9;
            if (std::abs(at - top) > 1.0) {
                failure_ =
                    QStringLiteral("the song opened scrolled %1 px from its top").arg(at - top);
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
        queueMeasurePopup();
        queueSelection();
        queueMix();
        queueTuningFields();
        queueSymbols();
        queueLyricsPopup();
        queueEffectsPalette();
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
        queueParking();
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

// The layout issues in the status bar (ADR-0150): a song with a measure wider
// than the page counts one there, its list names the measure, and choosing it
// puts the caret on it. Opens the song in place of the one edited.
void Startup::queueLayoutIssues() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("layout issues check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this, check] {
        check(window_->findChild<QQuickItem*>(QStringLiteral("layoutIssuesLabel")) == nullptr,
              "the status bar counts layout issues before there are any");
        // 128 sixteenths with accidentals in one measure of 32/4.
        core::Song song;
        song.appendMeasures(3);
        song.addTrack(core::Track("Part", core::Instrument{.name = "Part"},
                                  {core::Staff(core::Clef::Treble)}));
        song.measureHeader(1).timeSignature = core::TimeSignature(32, 4);
        std::vector<core::Beat>& beats = song.track(0).staff(0).measure(1).voices[0].beats;
        for (int b = 0; b < 128; ++b) {
            beats.push_back(core::Beat{
                .duration = core::Duration(core::NoteValue::Sixteenth),
                .notes = {core::Note{.pitch = core::Pitch::fromMidi(b % 2 == 0 ? 73 : 63)}}});
        }
        if (!scratch_) {
            scratch_.emplace();
        }
        const QString file = scratch_->filePath(QStringLiteral("too-wide.elara"));
        io::saveNative(song, std::filesystem::path(file.toStdU16String()));
        score_->open(QUrl::fromLocalFile(file));
        return true;
    });
    steps_.emplace_back([this, check] {
        const QStringList issues = score_->layoutIssueList();
        QStringList found;
        for (const PageIssues& page : score_->pageIssues()) {
            for (const notation::LayoutIssue& issue : *page) {
                found.append(QStringLiteral("%1 in measure %2")
                                 .arg(QLatin1StringView(notation::issueKindName(issue.kind)))
                                 .arg(issue.measure + 1));
            }
        }
        std::printf("layout issues: %d (%s)\n", score_->layoutIssueCount(),
                    qPrintable(found.join(QStringLiteral(", "))));
        check(score_->layoutIssueCount() == static_cast<int>(issues.size()) &&
                  issues.contains(ScoreDocument::tr("Measure %1 is wider than the page").arg(2)),
              "the measure too wide is not among the issues");
        check(window_->findChild<QQuickItem*>(QStringLiteral("layoutIssuesLabel")) != nullptr,
              "the status bar does not count the layout issues");
        QMetaObject::invokeMethod(window_, "showLayoutIssues");
        auto* dialog = window_->findChild<QObject*>(QStringLiteral("layoutIssuesDialog"));
        check(dialog != nullptr && dialog->property("visible").toBool(),
              "the list of layout issues did not open");
        const auto at = static_cast<int>(
            issues.indexOf(ScoreDocument::tr("Measure %1 is wider than the page").arg(2)));
        QMetaObject::invokeMethod(window_, "showLayoutIssue", Q_ARG(int, std::max(at, 0)));
        check(score_->caretMeasure() == 1, "choosing the issue did not take the caret to it");
        return false;
    });
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
        // A new song asked for while a save is written opens beside the song
        // saved at once (ADR-0160), and the save goes on (ADR-0107).
        saved_.reset();
        score_->save(QUrl::fromLocalFile(scratch_->filePath(QStringLiteral("waiting"))));
        QMetaObject::invokeMethod(window_, "newSong");
        window_->setProperty("showSongPane", false);
        return false;
    });
    steps_.emplace_back([this, check] {
        if (!saved_ || songs_->current()->busy()) {
            again_ = true;
            return false;
        }
        const auto* unsaved = window_->findChild<QObject*>(QStringLiteral("unsavedDialog"));
        check(unsaved != nullptr && !unsaved->property("visible").toBool(),
              "a song saved while a new one opened was asked to be saved");
        check(songs_->count() == 2 && songs_->current() != score_ &&
                  songs_->current()->song() != nullptr && !songs_->current()->modified(),
              "the new song asked for while saving did not open beside it");
        check(score_->songTitle() == QStringLiteral("Waiting") && !score_->modified(),
              "the song saved while a new one opened is not saved");
        // The new song, unchanged, closes without asking; the saved one shows.
        QMetaObject::invokeMethod(window_, "closeSong");
        return false;
    });
    steps_.emplace_back([this, check] {
        check(songs_->count() == 1 && songs_->current() == score_,
              "closing the new song did not show the song before it");
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

// Several songs open at once (ADR-0160), given as files on the command line:
// they load together, each in a tab; a measure copied in the first is pasted
// in the second, shown by "Next song"; the second, changed, closes after the
// unsaved changes are discarded, and the first shows again. Prints
// "songs: ..." at each stage.
void Startup::queueSongs() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("songs check failed: %1").arg(QLatin1StringView(what));
        }
    };
    const auto shown = [this](const char* name) {
        const QQuickItem* item = findItem(window_->contentItem(), QString::fromLatin1(name));
        return item != nullptr ? item->property("document").value<QObject*>() : nullptr;
    };
    steps_.emplace_back([this, check] {
        // Every file was asked for at once: wait for the last to be laid out.
        bool ready = songs_ != nullptr && songs_->count() == options_.files.size();
        for (int i = 0; ready && i < songs_->count(); ++i) {
            const ScoreDocument* song = songs_->at(i);
            ready = song->status() == ScoreDocument::Status::Ready && !song->busy() &&
                    song->pageCount() > 0;
        }
        if (!ready) {
            again_ = true;
            return false;
        }
        const QQuickItem* tabs = findItem(window_->contentItem(), QStringLiteral("songTabs"));
        check(tabs != nullptr && tabs->isVisible(), "the tabs do not show with two songs open");
        check(songs_->current() == score_, "the first file named is not the one shown");
        std::printf("songs: %d open: %s | %s\n", songs_->count(),
                    qPrintable(songs_->at(0)->title()), qPrintable(songs_->at(1)->title()));
        // The caret's measure, the first, of the first song.
        score_->copy();
        QMetaObject::invokeMethod(window_, "nextSong");
        return false;
    });
    steps_.emplace_back([this, check, shown] {
        ScoreDocument* other = songs_->at(1);
        check(songs_->current() == other, "Next song did not show the second song");
        check(shown("scoreView") == other, "the score does not show the song shown");
        const auto* audio = window_->findChild<AudioController*>();
        check(audio != nullptr && audio->document() == other,
              "the transport does not play the song shown");
        check(other->canPaste(), "a measure copied in one song cannot be pasted in another");
        other->moveCaret(ScoreDocument::CaretMove::NextMeasure);
        other->paste(false);
        const core::Song& to = *other->song();
        const core::Song& from = *score_->song();
        check(to.track(0).staff(0).measure(1) == from.track(0).staff(0).measure(0) &&
                  other->modified(),
              "the measure pasted from the other song is not the one copied");
        std::printf("songs: pasted measure 1 of %s into measure 2 of %s\n",
                    qPrintable(score_->title()), qPrintable(other->title()));
        QMetaObject::invokeMethod(window_, "closeSong");
        return false;
    });
    steps_.emplace_back([this, check] {
        // Its unsaved changes are asked about first, and discarded here.
        auto* unsaved = window_->findChild<QObject*>(QStringLiteral("unsavedDialog"));
        check(unsaved != nullptr && unsaved->property("visible").toBool() && songs_->count() == 2,
              "closing a song with unsaved changes did not ask first");
        if (unsaved != nullptr) {
            QMetaObject::invokeMethod(unsaved, "discarded");
        }
        return false;
    });
    steps_.emplace_back([this, check] {
        check(songs_->count() == 1 && songs_->current() == score_ && !songs_->unsaved(),
              "the song whose changes were discarded did not close");
        const QQuickItem* tabs = findItem(window_->contentItem(), QStringLiteral("songTabs"));
        check(tabs == nullptr || !tabs->isVisible(), "the tabs still show with one song open");
        std::printf("songs: closed the second, %d open\n", songs_->count());
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

// The measure popup open at the caret (`--measure`, ADR-0033) shows the
// caret's measure as the song changes under it: a measure inserted after the
// caret takes the popup with it, its measure repeat comes and goes, and the
// signatures, signs, repeats, section and tempo set on it show in the popup
// until they are undone. The edits themselves are the document's, checked in
// tests/app/test_score_editing.cpp (ADR-0181).
void Startup::queueMeasurePopup() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("measure popup check failed: %1").arg(QLatin1StringView(what));
        }
    };
    const auto popup = [this]() -> const QObject* {
        const auto* found = window_->findChild<QObject*>(QStringLiteral("measurePopup"));
        return found != nullptr && found->property("opened").toBool() ? found : nullptr;
    };
    // Whether the popup shows what the document says of the caret's measure.
    const auto showsCaret = [this](const QObject* open) {
        return open != nullptr && mapOf(open->property("marks")) == score_->caretNavigation() &&
               mapOf(open->property("repeats")) == score_->caretRepeats() &&
               mapOf(open->property("tempo")) == score_->caretTempo();
    };
    steps_.emplace_back([this, popup] {
        measuring_ = popup() != nullptr;
        if (!measuring_) {
            return false;
        }
        score_->insertMeasure(true);
        editor_->setMeasureRepeat(1);
        return true;
    });
    steps_.emplace_back([this, popup, showsCaret, check] {
        if (!measuring_) {
            return false;
        }
        check(popup() != nullptr && score_->caretMeasure() == 1 && editor_->caretRepeat() == 1,
              "it did not stay open on the measure inserted after the caret");
        editor_->setMeasureRepeat(0);
        const int measure = score_->caretMeasure();
        score_->setTimeSignatureFrom(measure, 3, 4);
        score_->setKeySignatureFrom(measure, 2, false);
        score_->setNavigation(measure, {{QStringLiteral("segno"), true}});
        score_->setRepeats(measure, {{QStringLiteral("start"), true},
                                     {QStringLiteral("times"), 3},
                                     {QStringLiteral("doubleBar"), true}});
        score_->setSection(measure, QStringLiteral("Chorus"));
        score_->setTempoFrom(measure, 90);
        score_->setTempoGradual(measure, true);
        score_->setTripletFeelFrom(measure, 1);
        check(showsCaret(popup()), "it does not show the measure's edits");
        return true;
    });
    steps_.emplace_back([this, popup, showsCaret, check] {
        if (!measuring_) {
            return false;
        }
        const QObject* open = popup();
        check(showsCaret(open) &&
                  mapOf(open->property("marks")).value(QStringLiteral("segno")).toBool() &&
                  mapOf(open->property("repeats")).value(QStringLiteral("times")).toInt() == 3 &&
                  mapOf(open->property("tempo")).value(QStringLiteral("bpm")).toInt() == 90,
              "it does not show the segno, the repeat and the tempo set on its measure");
        for (int i = 0; i < 16 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, popup, showsCaret, check] {
        if (!measuring_) {
            return false;
        }
        check(!score_->modified() && showsCaret(popup()),
              "it does not show the caret's measure once the edits are undone");
        return false;
    });
}

// The song panel open on a track's tuning (`--tuning`, ADR-0046) shows it as
// it changes: a capo a fret higher shows in its fields, and goes on undo. The
// tunings themselves are the document's, checked in
// tests/app/test_score_editing.cpp (ADR-0181).
void Startup::queueTuningFields() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("tuning fields check failed: %1").arg(QLatin1StringView(what));
        }
    };
    const auto fields = [this]() -> const QQuickItem* {
        QList<QQuickItem*> found;
        shownItems(window_->contentItem(), QLatin1StringView("tuningFields"), found);
        return found.isEmpty() ? nullptr : found.front();
    };
    const auto capo = [fields] {
        const QQuickItem* shown = fields();
        return shown != nullptr ? shown->property("capo").toInt() : -1;
    };
    steps_.emplace_back([this, fields, capo] {
        const QQuickItem* shown = fields();
        capoBefore_.reset();
        if (shown == nullptr) {
            return false;
        }
        capoBefore_ = capo();
        const int track = shown->property("track").toInt();
        const QVariantMap tuning = score_->trackTuning(track);
        score_->setTrackTuning(track, tuning.value(QStringLiteral("notes")).toString(),
                               tuning.value(QStringLiteral("capo")).toInt() + 1, true);
        return true;
    });
    steps_.emplace_back([this, capo, check] {
        if (!capoBefore_) {
            return false;
        }
        check(capo() == *capoBefore_ + 1, "they do not show the capo a fret higher");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, capo, check] {
        if (!capoBefore_) {
            return false;
        }
        check(capo() == *capoBefore_ && !score_->modified(),
              "they do not show the capo back where it was");
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

// The lyrics popup (ADR-0084) shows a verse typed from the caret's measure
// as typed. The verses themselves are checked in
// tests/app/test_caret_editor.cpp (ADR-0181).
void Startup::queueLyricsPopup() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("lyrics popup check failed: %1").arg(QLatin1StringView(what));
        }
    };
    // A note to sing.
    steps_.emplace_back([this] {
        singing_ = score_->caretPage() >= 0 && editor_->caretPitched() &&
                   editor_->caretVerseCount() < ScoreDocument::maxVerses();
        if (!singing_) {
            return false;
        }
        if (editor_->caretFretted()) {
            score_->typeDigit(5);
        } else {
            editor_->typeNoteName(0, false);
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!singing_) {
            return false;
        }
        const int verse = editor_->caretVerseCount();
        check(editor_->setVerse(verse, score_->caretMeasure(), QStringLiteral("la_")) == 0,
              "the verse did not go on whole");
        auto* popup = window_->findChild<QObject*>(QStringLiteral("lyricsPopup"));
        check(popup != nullptr, "there is none");
        if (popup != nullptr) {
            popup->setProperty("verse", verse);
            QMetaObject::invokeMethod(popup, "open");
            check(popup->property("text").toString() == QStringLiteral("la_"),
                  "it does not show the verse");
            QMetaObject::invokeMethod(popup, "close");
        }
        for (int i = 0; i < 4 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!singing_) {
            return false;
        }
        check(!score_->modified(), "undoing the verse did not restore the song");
        return false;
    });
}

// The note effects palette open at the caret (`--effects`, ADR-0109) lights
// the tiles of what the caret's note has: staccato, a wide vibrato, a
// diminuendo and a strum set on a note typed there, out again once undone.
// The effects themselves are checked in tests/app/test_caret_editor.cpp
// (ADR-0181).
void Startup::queueEffectsPalette() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ =
                QStringLiteral("effects palette check failed: %1").arg(QLatin1StringView(what));
        }
    };
    constexpr int kStaccato = static_cast<int>(core::NoteEffect::Staccato);
    const auto lit = [this] {
        QStringList tips;
        litTips(qobject_cast<const QQuickItem*>(effectsPanel_.data()), tips);
        tips.sort();
        return tips;
    };
    const auto tip = [](const char* text) {
        return QCoreApplication::translate("EffectsPalette", text);
    };
    steps_.emplace_back([this, lit] {
        litBefore_.reset();
        if (effectsPanel_ == nullptr || !effectsPanel_->property("opened").toBool() ||
            score_->caretPage() < 0 || !editor_->caretPitched()) {
            return false;
        }
        litBefore_ = lit();
        if (editor_->caretFretted()) {
            score_->typeDigit(5);
        } else {
            editor_->typeNoteName(0, false);
        }
        return true;
    });
    steps_.emplace_back([this] {
        if (!litBefore_) {
            return false;
        }
        editor_->toggleEffect(kStaccato);
        editor_->setVibrato(static_cast<int>(core::Vibrato::Wide));
        editor_->setHairpin(static_cast<int>(core::Hairpin::Diminuendo));
        editor_->setStrum(1);
        return true;
    });
    steps_.emplace_back([this, lit, tip, check] {
        if (!litBefore_) {
            return false;
        }
        const QStringList tips = lit();
        const bool staccato = (editor_->caretEffects() & (1 << kStaccato)) != 0;
        check(tips.contains(tip("%1 (%2)")
                                .arg(editor_->effectName(kStaccato))
                                .arg(editor_->effectKey(kStaccato))) == staccato,
              "the staccato tile is not lit as the note is");
        check(tips.contains(tip("Wide vibrato")) && tips.contains(tip("Diminuendo")) &&
                  tips.contains(editor_->caretFretted() ? tip("Strum down") : tip("Roll upwards")),
              "the tiles of the vibrato, the hairpin and the strum are not lit");
        for (int i = 0; i < 8 && score_->modified() && score_->canUndo(); ++i) {
            score_->undo();
        }
        return true;
    });
    steps_.emplace_back([this, lit, check] {
        if (!litBefore_) {
            return false;
        }
        check(!score_->modified() && lit() == *litBefore_,
              "the tiles lit did not go out with the effects undone");
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
                popups << QStringLiteral("%1 %2").arg(
                    QLatin1StringView(object->metaObject()->className()), object->objectName());
            }
        }
        return QStringLiteral("keys to %1, focus on %2 %3, popups: %4")
            .arg(focused == window_   ? QStringLiteral("the window")
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
    if (!options_.file.isEmpty() && !openFailure_.isEmpty()) {
        finish(false, openFailure_);
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
    if (options_.play && !played_) {
        played_ = true;
        auto* audio = window_ != nullptr ? window_->findChild<AudioController*>() : nullptr;
        if (audio != nullptr && !audio->playing()) {
            audio->togglePlayback();
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
    // `--hold`: the window stays open with the check's timer stopped, so
    // that only the app's own work runs, then the check ends as usual.
    if (options_.hold && !held_) {
        held_ = true;
        timer_.stop();
        std::printf("hold: %g s\n", *options_.hold);
        std::fflush(stdout);
        QTimer::singleShot(static_cast<int>(std::lround(*options_.hold * 1000.0)), this,
                           [this] { timer_.start(); });
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

// The view holds still while the layout changes: on a new song whose first
// page is full, the caret's staff in a system down that page stays at its
// height on screen while an edit makes the first system taller and while
// undo takes that back. A measure inserted after the caret's, which takes
// the caret on to the next system, shows it there, as the keys reveal it.
void Startup::queueHoldStill() {
    const auto view = [this] {
        return window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    };
    // The caret's bottom on screen, in the view's pixels.
    const auto screenY = [view] {
        QPointF point(0.0, -1.0);
        QMetaObject::invokeMethod(view(), "caretPoint", Q_RETURN_ARG(QPointF, point));
        return point.y();
    };
    // The caret's staff, in staff spaces from the top of the first page.
    const auto staffTop = [this] {
        const int page = score_->caretPage();
        const QList<qreal> tops = score_->pageTops();
        return page >= 0 && page < tops.size() ? tops[page] + score_->caretRect().y() : -1.0;
    };
    // How far the pages are scrolled, in the view's pixels.
    const auto scroll = [view] {
        const auto* pages = view()->findChild<QQuickItem*>(QStringLiteral("pages"));
        return pages != nullptr ? pages->property("contentY").toDouble() : -1.0;
    };
    // What a failure shows of the view, to tell why the caret moved.
    const auto scene = [this, view, screenY, scroll] {
        return QStringLiteral(
                   "the caret at %1 px of %2 by %3, scrolled from %4 to %5 px, zoom from "
                   "%6 to %7, held %8, a reveal pending: %9")
            .arg(screenY())
            .arg(view()->height())
            .arg(view()->width())
            .arg(heldScroll_)
            .arg(scroll())
            .arg(heldZoom_)
            .arg(view()->property("shownZoom").toDouble())
            .arg(heldBy_)
            .arg(view()->property("revealPending").toBool());
    };
    const auto problem = [this](const QString& what) {
        if (editProblem_.isEmpty()) {
            editProblem_ = what;
        }
    };
    steps_.emplace_back([this] {
        score_->newSong(QStringLiteral("electric-guitar"));
        connect(score_, &ScoreDocument::caretHeld, score_, [this](double spaces, int pages) {
            heldBy_ = QStringLiteral("%1 staff spaces and %2 pages").arg(spaces).arg(pages);
        });
        return true;
    });
    steps_.emplace_back([this] {
        score_->setMeasureCount(150);
        return true;
    });
    // The caret at the start of the fourth system of the first page, in view.
    steps_.emplace_back([this, view] {
        const auto first = score_->page(0);
        if (!first || first->systems.size() < 5 || score_->pageCount() < 2) {
            failure_ = QStringLiteral("the new song does not fill its first page: %1 systems on "
                                      "it, %2 pages")
                           .arg(first ? static_cast<qsizetype>(first->systems.size()) : 0)
                           .arg(score_->pageCount());
            return false;
        }
        const auto target = static_cast<int>(first->systems[3].firstMeasure);
        for (int i = 0; i < 150 && score_->caretMeasure() > target; ++i) {
            score_->moveCaret(ScoreDocument::CaretMove::PreviousMeasure);
        }
        for (int i = 0; i < 150 && score_->caretMeasure() < target; ++i) {
            score_->moveCaret(ScoreDocument::CaretMove::NextMeasure);
        }
        QMetaObject::invokeMethod(view(), "reveal");
        return false;
    });
    // A section mark makes the first system taller.
    steps_.emplace_back([this, view, screenY, staffTop, scroll] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        // The caret well inside the view first: a window that got shorter
        // since the reveal can leave it under the bottom edge, where the view
        // does not hold it and the reveal still pending scrolls to it.
        const double height = view()->height();
        if (const double y = screenY(); y > height - 120.0 && height > 240.0) {
            auto* pages = view()->findChild<QQuickItem*>(QStringLiteral("pages"));
            pages->setProperty("contentY", scroll() + y - (height / 2.0));
            again_ = true;
            return false;
        }
        heldY_ = screenY();
        heldTop_ = staffTop();
        heldScroll_ = scroll();
        heldZoom_ = view()->property("shownZoom").toDouble();
        heldBy_ = QStringLiteral("nothing");
        score_->setSection(0, QStringLiteral("Intro"));
        return true;
    });
    steps_.emplace_back([this, view, screenY, staffTop, scroll, scene, problem] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        std::printf("hold still: the staff moved %.2f staff spaces, the caret %.2f px\n",
                    staffTop() - heldTop_, screenY() - heldY_);
        if (std::abs(staffTop() - heldTop_) < 1.0) {
            problem(QStringLiteral("the section moved the staff below it only %1 staff spaces")
                        .arg(staffTop() - heldTop_));
        } else if (std::abs(screenY() - heldY_) > 1.0) {
            problem(QStringLiteral("the caret moved %1 px on screen as a system above it grew (%2)")
                        .arg(screenY() - heldY_)
                        .arg(scene()));
        }
        heldScroll_ = scroll();
        heldZoom_ = view()->property("shownZoom").toDouble();
        heldBy_ = QStringLiteral("nothing");
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, view, screenY, scene, problem] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (std::abs(screenY() - heldY_) > 1.0) {
            problem(
                QStringLiteral("the caret moved %1 px on screen as undo took the section back (%2)")
                    .arg(screenY() - heldY_)
                    .arg(scene()));
        }
        return false;
    });
    // A measure inserted after the last of the caret's system, the caret in
    // it, until the caret goes on to another system: it is shown there, as the
    // keys show it, instead of held where it was.
    steps_.emplace_back([this, view, screenY, problem] {
        if (view()->property("settling").toBool() || score_->busy()) {
            again_ = true;
            return false;
        }
        // The system holding `measure` on the pages shown: its page and its
        // first measure, or -1.
        const auto systemOf = [this](int measure) {
            for (int p = 0; p < score_->pageCount(); ++p) {
                for (const notation::SystemBox& box : score_->page(p)->systems) {
                    const auto first = static_cast<int>(box.firstMeasure);
                    if (measure >= first && measure < first + static_cast<int>(box.measureCount)) {
                        return std::pair{p, first};
                    }
                }
            }
            return std::pair{-1, -1};
        };
        if (heldMeasure_ > 0 && score_->caretMeasure() == heldMeasure_ &&
            systemOf(heldMeasure_) != systemOf(heldMeasure_ - 1)) {
            const double y = screenY();
            if (y < 0.0 || y > view()->height()) {
                problem(QStringLiteral("the caret on another system is out of view, at %1 px of %2")
                            .arg(y)
                            .arg(view()->height()));
            }
            return false;
        }
        if (++insertions_ > 12) {
            problem(QStringLiteral("inserted measures never took the caret to another system"));
            return false;
        }
        const auto [page, first] = systemOf(score_->caretMeasure());
        int last = first;
        while (systemOf(last + 1) == std::pair{page, first}) {
            ++last;
        }
        for (int i = 0; i < 150 && score_->caretMeasure() < last; ++i) {
            score_->moveCaret(ScoreDocument::CaretMove::NextMeasure);
        }
        heldMeasure_ = score_->caretMeasure() + 1;
        score_->insertMeasure(true);
        QMetaObject::invokeMethod(view(), "reveal");
        again_ = true;
        return false;
    });
}

// Following playback (ADR-0170), in a new song several pages long: playback is
// moved every tick as it moves while it plays. The view must bring the
// cursor's system in while the cursor goes on moving, hold still once scrolled
// by hand until the cursor goes into another system, and not follow at all
// with following off.
void Startup::queueFollow() {
    const auto view = [this] {
        return window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    };
    const auto pages = [view] {
        return view()->findChild<QQuickItem*>(QStringLiteral("pages"));
    };
    const auto audio = [this] {
        return window_->findChild<AudioController*>();
    };
    // Where the playback cursor is in the view, in its pixels; empty when hidden.
    const auto cursor = [this, view]() -> QRectF {
        auto* playhead = window_->findChild<QQuickItem*>(QStringLiteral("playhead"));
        if (playhead == nullptr || !playhead->isVisible()) {
            return {};
        }
        return playhead->mapRectToItem(view(),
                                       QRectF(0.0, 0.0, playhead->width(), playhead->height()));
    };
    const auto inView = [view, cursor] {
        const QRectF at = cursor();
        return !at.isEmpty() && at.top() >= 0.0 && at.bottom() <= view()->height();
    };
    const auto scene = [view, pages, cursor] {
        const QRectF at = cursor();
        return QStringLiteral("the cursor from %1 to %2 px in a view %3 px high, scrolled to %4")
            .arg(at.top())
            .arg(at.bottom())
            .arg(view()->height())
            .arg(pages()->property("contentY").toDouble());
    };
    const auto seek = [audio](double seconds) {
        if (auto* controller = audio(); controller != nullptr) {
            controller->seek(seconds);
        }
    };
    // The first measure of system `system` of page `page`, from 0; -1 when
    // there is none.
    const auto measureOf = [this](int page, std::size_t system) {
        const auto list = score_->page(page);
        return list && list->systems.size() > system
                   ? static_cast<int>(list->systems[system].firstMeasure)
                   : -1;
    };
    steps_.emplace_back([this] {
        score_->newSong(QStringLiteral("electric-guitar"));
        return true;
    });
    steps_.emplace_back([this] {
        score_->setMeasureCount(300);
        return true;
    });
    // Playback moves to the second system of the second page, out of view.
    steps_.emplace_back([this, view, measureOf, seek] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        const int measure = measureOf(1, 1);
        if (measure < 0 || measureOf(1, 2) < 0 || score_->pageCount() < 3) {
            failure_ = QStringLiteral("the new song has %1 pages").arg(score_->pageCount());
            return false;
        }
        followSeconds_ = score_->measureStart(measure);
        followTicks_ = 0;
        seek(followSeconds_);
        return false;
    });
    // It goes on moving every tick, as it does while playing, a little longer
    // than the view takes to scroll: the cursor's system must be in view by then.
    // The measure playing is shaded across its system, where the pages say it is.
    steps_.emplace_back([this, view, audio, inView, scene, seek] {
        if (++followTicks_ < 40) {
            seek(followSeconds_ + followTicks_ * 0.01);
            again_ = true;
            return false;
        }
        if (!inView()) {
            failure_ = QStringLiteral("the view did not follow playback: %1").arg(scene());
            return false;
        }
        const double position = audio()->position();
        const int playing = score_->measureAt(position);
        const QRectF expected = score_->measureBox(
            score_->cursorAt(position).value(QStringLiteral("page")).toInt(), playing);
        QList<QQuickItem*> shades;
        shownItems(view(), QLatin1StringView("measureShade"), shades);
        if (expected.isEmpty() || shades.size() != 1 ||
            shades.front()->property("box").toRectF() != expected) {
            failure_ = QStringLiteral("%1 shades show, none on measure %2, which plays")
                           .arg(shades.size())
                           .arg(playing + 1);
        }
        std::printf("follow: in view after %d ticks\n", followTicks_);
        return false;
    });
    // Scrolled by hand with the wheel, up and away from the cursor.
    steps_.emplace_back([this, view] {
        const QPointF at = view()->mapToScene(QPointF(view()->width() / 2, view()->height() / 2));
        QWheelEvent wheel(at, window_->mapToGlobal(at), QPoint(), QPoint(0, 1200), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window_, &wheel);
        followWheelAt_ = clock_.elapsed();
        return false;
    });
    steps_.emplace_back([this, view, pages, inView, scene] {
        if (pages()->property("moving").toBool() || view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (!view()->property("heldByHand").toBool()) {
            failure_ = QStringLiteral("the wheel did not count as a scroll by hand");
        } else if (inView()) {
            failure_ = QStringLiteral("the wheel left the cursor in view: %1").arg(scene());
        }
        followScroll_ = pages()->property("contentY").toDouble();
        followTicks_ = 0;
        return false;
    });
    // Playback goes on in the same system: the view stays where the hand left it.
    steps_.emplace_back([this, pages, seek] {
        if (++followTicks_ < 15) {
            seek(followSeconds_ + 0.5 + followTicks_ * 0.01);
            again_ = true;
            return false;
        }
        const double scrolled = pages()->property("contentY").toDouble();
        if (std::abs(scrolled - followScroll_) > 0.5) {
            failure_ = QStringLiteral("the view went from %1 to %2 px after a scroll by hand, "
                                      "with the cursor in the same system")
                           .arg(followScroll_)
                           .arg(scrolled);
        }
        return false;
    });
    // Once the pause is over, playback going into the next system brings the
    // view back to it.
    steps_.emplace_back([this, view, measureOf, seek] {
        if (clock_.elapsed() - followWheelAt_ <
            view()->property("followPause").toInt() + 2 * kTickMilliseconds) {
            again_ = true;
            return false;
        }
        seek(score_->measureStart(measureOf(1, 2)));
        return false;
    });
    steps_.emplace_back([this, view, inView, scene] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (!inView()) {
            failure_ = QStringLiteral("the view did not follow playback into the next system "
                                      "after a scroll by hand: %1")
                           .arg(scene());
        }
        return false;
    });
    // With following off, playback far away leaves the view where it is.
    steps_.emplace_back([this, view, pages, measureOf, seek] {
        view()->setProperty("followCursor", false);
        followScroll_ = pages()->property("contentY").toDouble();
        seek(score_->measureStart(measureOf(score_->pageCount() - 1, 0)));
        return false;
    });
    steps_.emplace_back([this, view, pages, scene] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (std::abs(pages()->property("contentY").toDouble() - followScroll_) > 0.5) {
            failure_ =
                QStringLiteral("the view followed playback with following off: %1").arg(scene());
        }
        // Back on, it goes to the cursor at once.
        view()->setProperty("followCursor", true);
        return false;
    });
    steps_.emplace_back([this, view, inView, scene] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (!inView()) {
            failure_ =
                QStringLiteral("following turned on did not go to the cursor: %1").arg(scene());
        }
        return false;
    });
    queueLoopOnScore();
}

void Startup::queueLoopOnScore() {
    const auto view = [this] {
        return window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    };
    const auto audio = [this] {
        return window_->findChild<AudioController*>();
    };
    // The loop's bands the pages show, in staff spaces, and those its measures
    // span on the pages: a system at a time, top to bottom.
    const auto byPlace = [](QList<QRectF>& spans) {
        std::ranges::sort(spans, [](const QRectF& a, const QRectF& b) {
            return std::pair(a.y(), a.x()) < std::pair(b.y(), b.x());
        });
        return spans;
    };
    const auto shown = [view, byPlace] {
        QList<QQuickItem*> bands;
        shownItems(view(), QLatin1StringView("loopSpan"), bands);
        QList<QRectF> spans;
        for (const QQuickItem* band : bands) {
            spans.append(band->property("span").toRectF());
        }
        return byPlace(spans);
    };
    const auto spanned = [this, byPlace](int first, int last) {
        QList<QRectF> spans;
        for (int page = 0; page < score_->pageCount(); ++page) {
            for (const QVariant& span : score_->measureSpans(page, first, last)) {
                spans.append(span.toRectF());
            }
        }
        return byPlace(spans);
    };
    // A at the third measure: a band over it.
    steps_.emplace_back([this, audio] {
        audio()->seek(score_->measureStart(2));
        audio()->markLoop();
        return false;
    });
    steps_.emplace_back([this, view, shown, spanned] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        if (shown().isEmpty() || shown() != spanned(2, 2)) {
            failure_ = QStringLiteral("A at measure 3 shows %1 loop bands").arg(shown().size());
        }
        return false;
    });
    // B at the start of the seventh: a band over the third to the sixth.
    steps_.emplace_back([this, audio] {
        audio()->seek(score_->measureStart(6));
        audio()->markLoop();
        return false;
    });
    steps_.emplace_back([this, view, audio, shown, spanned] {
        if (view()->property("settling").toBool()) {
            again_ = true;
            return false;
        }
        const QList<QRectF> bands = shown();
        if (!audio()->looping() || bands.isEmpty() || bands != spanned(2, 5)) {
            failure_ = QStringLiteral("the loop of measures 3 to 6 shows %1 bands, not the %2 "
                                      "over the systems its measures span")
                           .arg(bands.size())
                           .arg(spanned(2, 5).size());
            return false;
        }
        std::printf("loop: measures 3 to 6, %lld %s\n", static_cast<long long>(bands.size()),
                    bands.size() == 1 ? "band" : "bands");
        audio()->markLoop(); // a third time ends it
        return false;
    });
    steps_.emplace_back([this, shown] {
        if (!shown().isEmpty()) {
            failure_ = QStringLiteral("the loop's band stays after the loop ended");
        }
        return false;
    });
}

// A click on the score while playback is stopped puts the caret there and
// playback with it (ADR-0170): the cursor hides, since the caret marks the
// place, and the caret's measure is shaded across its system. The arrow keys
// move playback with the caret too.
void Startup::queueParking() {
    const auto view = [this] {
        return window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
    };
    // The shaded measure, from the shade drawn on its page: its box in staff
    // spaces, or an empty one when no shade shows or several do.
    const auto shade = [view] {
        QList<QQuickItem*> shades;
        shownItems(view(), QLatin1StringView("measureShade"), shades);
        return shades.size() == 1 ? shades.front()->property("box").toRectF() : QRectF();
    };
    const auto check = [this, view, shade](const char* after) {
        const QRectF expected = score_->measureBox(score_->caretPage(), score_->caretMeasure());
        if (view()->property("cursorShown").toBool()) {
            failure_ = QStringLiteral("the playback cursor shows after %1 with playback stopped")
                           .arg(QLatin1StringView(after));
        } else if (expected.isEmpty() || shade() != expected) {
            failure_ = QStringLiteral("after %1 the shade is at %2, %3, %4 by %5, not on the "
                                      "caret's measure %6")
                           .arg(QLatin1StringView(after))
                           .arg(shade().x())
                           .arg(shade().y())
                           .arg(shade().width())
                           .arg(shade().height())
                           .arg(score_->caretMeasure() + 1);
        }
    };
    steps_.emplace_back([this, check] {
        check("a click on the score");
        pressKey(Qt::Key_Right);
        return false;
    });
    steps_.emplace_back([this, check] {
        check("the right arrow");
        const QRectF caret = score_->caretRect();
        const double expected =
            score_->secondsAt(score_->caretPage(), caret.center().x(), caret.center().y());
        const auto* audio = window_->findChild<AudioController*>();
        if (failure_.isEmpty() &&
            (audio == nullptr || expected < 0.0 || std::abs(audio->position() - expected) > 1e-6)) {
            failure_ = QStringLiteral("the right arrow left playback at %1 s, not at the caret's "
                                      "beat, %2 s")
                           .arg(audio != nullptr ? audio->position() : -1.0)
                           .arg(expected);
        }
        return false;
    });
    // The caret's cell frames the fret number or the note and never covers it
    // (ADR-0173): no fill, and a line of two pixels at most.
    steps_.emplace_back([this, view] {
        QList<QQuickItem*> cells;
        shownItems(view(), QLatin1StringView("caretCell"), cells);
        if (cells.size() != 1) {
            failure_ = QStringLiteral("the caret shows %1 cells").arg(cells.size());
            return false;
        }
        const QColor fill = cells.front()->property("color").value<QColor>();
        const auto* border = cells.front()->property("border").value<QObject*>();
        const double line = border != nullptr ? border->property("width").toDouble() : -1.0;
        if (fill.alpha() != 0 || line < 1.0 || line > 2.0) {
            failure_ = QStringLiteral("the caret's cell is filled %1 of 255 inside a line of %2 "
                                      "px: it covers what it marks")
                           .arg(fill.alpha())
                           .arg(line);
        }
        return false;
    });
    // Playback moved by the transport shows its cursor again; a key that
    // moves the caret, here to the next string, hides it and takes playback
    // to the caret's beat.
    steps_.emplace_back([this] {
        if (auto* audio = window_->findChild<AudioController*>()) {
            audio->seek(audio->position() > 1.0 ? audio->position() - 1.0
                                                : audio->position() + 1.0);
        }
        return false;
    });
    steps_.emplace_back([this, view] {
        if (view()->property("settling").toBool()) {
            again_ = true; // the view follows the cursor to its system first
            return false;
        }
        if (!view()->property("cursorShown").toBool()) {
            failure_ = QStringLiteral("the playback cursor stays hidden after the transport "
                                      "moved playback");
            return false;
        }
        pressKey(Qt::Key_Down);
        return false;
    });
    steps_.emplace_back([this, view, check] {
        if (view()->property("settling").toBool()) {
            again_ = true; // back to the caret
            return false;
        }
        check("the down arrow");
        const QRectF caret = score_->caretRect();
        const double expected =
            score_->secondsAt(score_->caretPage(), caret.center().x(), caret.center().y());
        const auto* audio = window_->findChild<AudioController*>();
        if (failure_.isEmpty() &&
            (audio == nullptr || expected < 0.0 || std::abs(audio->position() - expected) > 1e-6)) {
            failure_ = QStringLiteral("the down arrow left playback at %1 s, not at the caret's "
                                      "beat, %2 s")
                           .arg(audio != nullptr ? audio->position() : -1.0)
                           .arg(expected);
        }
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
        // The loop's marks moved playback: its cursor shows until an edit.
        const auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
        if (view != nullptr && !view->property("cursorShown").toBool() && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("the playback cursor does not show before an edit");
        }
        score_->addTrack(QStringLiteral("electric-bass"));
        return true;
    });
    steps_.emplace_back([this, check] {
        check("an edit");
        auto* audio = window_->findChild<AudioController*>();
        if (audio != nullptr && !audio->looping() && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("an edit ended the loop");
        }
        // An edit made while playback is stopped hides the cursor: the caret
        // marks the place while the score is worked on (ADR-0173).
        const auto* view = window_->findChild<QQuickItem*>(QStringLiteral("scoreView"));
        if (view != nullptr && view->property("cursorShown").toBool() && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("the playback cursor shows after an edit with "
                                          "playback stopped");
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
