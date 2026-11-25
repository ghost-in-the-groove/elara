// SPDX-License-Identifier: AGPL-3.0-or-later
#include "startup.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>

#include "audio_controller.hpp"
#include "recent_scores.hpp"
#include "score_page.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QQuickItem>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
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
constexpr int kSnareKey = 38;

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
    score_ = window_ != nullptr ? window_->findChild<ScoreDocument*>() : nullptr;
    if (score_ != nullptr) {
        connect(score_, &ScoreDocument::layoutChanged, this, [this] {
            laidOut_ = true;
            ++layouts_;
            layoutSteps_ += QStringLiteral(" %1").arg(stepsRun_);
        });
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
            queueEffects();
            queueContinuous();
        }
        return;
    }
    if (options_.smokeTest) {
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
        seekTo(score_->duration() / 2.0);
        steps_.emplace_back([this] {
            clickCursor();
            return false;
        });
        queuePdf();
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
    steps_.emplace_back([this] {
        score_->setCredit(static_cast<int>(core::Credit::Subtitle),
                          QStringLiteral("Smoke credits"));
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

// Note names at the caret: a C typed, raised a semitone, both undone.
void Startup::queuePitches() {
    const auto check = [this](bool ok, const char* what) {
        if (!ok && editProblem_.isEmpty()) {
            editProblem_ = QStringLiteral("pitch check failed: %1").arg(QLatin1StringView(what));
        }
    };
    steps_.emplace_back([this] {
        pitching_ = score_->caretPage() >= 0 && score_->caretPitched();
        if (!pitching_) {
            return false;
        }
        score_->typeNoteName(0, false);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!pitching_) {
            return false;
        }
        const std::vector<core::Pitch> pitches = score_->pitchesAtCaret();
        check(pitches.size() == 1 && pitches.front().step() == core::Step::C,
              "the note name did not type a C");
        if (pitches.empty()) {
            return false;
        }
        check(score_->caretStatus().endsWith(QStringLiteral("C%1").arg(pitches.front().octave())),
              "the status does not name the note");
        typedMidi_ = pitches.front().midiNumber();
        score_->shiftPitch(1);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!pitching_) {
            return false;
        }
        const std::vector<core::Pitch> pitches = score_->pitchesAtCaret();
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
        check(score_->caretOnDrums(), "the caret is not on the drum track");
        const int lanes = static_cast<int>(core::drumPieces().size());
        for (int lane = 1; lane < lanes; ++lane) {
            score_->moveCaret(ScoreDocument::CaretMove::StringUp);
        }
        check(score_->caret().string == 1 &&
                  score_->caretStatus().endsWith(ScoreDocument::laneName(1)),
              "the status does not name the top lane");
        const double crash = score_->caretRect().top();
        for (int lane = 1; lane < laneOf(kSnareKey); ++lane) {
            score_->moveCaret(ScoreDocument::CaretMove::StringDown);
        }
        check(score_->caretStatus().endsWith(ScoreDocument::laneName(laneOf(kSnareKey))),
              "the status does not name the snare");
        check(score_->caretRect().top() > crash, "the caret did not go down to the snare");
        score_->toggleDrum();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        const std::vector<core::Pitch> hits = score_->pitchesAtCaret();
        check(hits.size() == 1 && hits.front().midiNumber() == kSnareKey,
              "the snare was not added");
        score_->toggleDrum();
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!drumming_) {
            return false;
        }
        check(score_->pitchesAtCaret().empty(), "the snare was not removed");
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
        voicing_ = score_->caretPage() >= 0 && score_->caretPitched();
        if (!voicing_) {
            return false;
        }
        score_->setCaretVoice(2);
        check(score_->caretVoice() == 2, "the caret did not go to voice 2");
        check(score_->caretStatus().contains(ScoreDocument::tr("Voice %1").arg(2)),
              "the status does not name voice 2");
        if (score_->caretFretted()) {
            score_->typeDigit(7);
        } else {
            score_->typeNoteName(2, false);
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!voicing_) {
            return false;
        }
        check(score_->caretVoice() == 2 && !score_->pitchesAtCaret().empty(),
              "the note did not go into voice 2");
        score_->setCaretVoice(1);
        check(score_->caretVoice() == 1 &&
                  !score_->caretStatus().contains(ScoreDocument::tr("Voice %1").arg(2)),
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
        check(score_->caretStatus().startsWith(ScoreDocument::tr("Measures %1-%2 selected")
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
        check(trackRow_ != nullptr, "a mix change made the song panel's rows again");
        sequenceBefore_.reset();
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
    steps_.emplace_back([this] {
        score_->undo();
        return true;
    });
    steps_.emplace_back([this, check] {
        check(!score_->modified() && score_->sectionAt(0) == sectionBefore_,
              "undoing the section did not restore the song");
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
        effecting_ = score_->caretPage() >= 0 && score_->caretPitched();
        if (!effecting_) {
            return false;
        }
        check(score_->effectForKey(QStringLiteral(">")) == kAccent &&
                  score_->effectForKey(QStringLiteral("'")) == kStaccato &&
                  score_->effectForKey(QStringLiteral("_")) ==
                      static_cast<int>(core::NoteEffect::Tenuto) &&
                  score_->effectForKey(QStringLiteral("\"")) ==
                      static_cast<int>(core::NoteEffect::Staccatissimo) &&
                  score_->effectForKey(QStringLiteral("p")) ==
                      static_cast<int>(core::NoteEffect::PalmMute) &&
                  score_->effectForKey(QStringLiteral("a")) < 0,
              "the keys do not map to the effects");
        // The panel shows the techniques of strings on fretted staves only.
        check(score_->effectNeedsStrings(static_cast<int>(core::NoteEffect::PalmMute)) &&
                  score_->effectNeedsStrings(static_cast<int>(core::NoteEffect::Harmonic)) &&
                  !score_->effectNeedsStrings(kAccent) && !score_->effectNeedsStrings(-1),
              "the techniques of strings are not told apart");
        if (score_->caretFretted()) {
            score_->typeDigit(5);
        } else {
            score_->typeNoteName(0, false);
        }
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!effecting_) {
            return false;
        }
        effectsBefore_ = score_->caretEffects();
        score_->toggleEffect(kAccent);
        check((score_->caretEffects() ^ effectsBefore_) == 1 << kAccent,
              "the accent did not toggle");
        check(score_->undoName() == ScoreDocument::tr("Change effect"),
              "the effect is not undoable");
        score_->toggleEffect(kStaccato);
        check((score_->caretEffects() ^ effectsBefore_) == ((1 << kAccent) | (1 << kStaccato)),
              "staccato did not toggle");
        const int on = score_->caretEffects();
        check(((on & (1 << kStaccato)) != 0) ==
                  score_->caretStatus().contains(score_->effectName(kStaccato)),
              "the status does not name the effects");
        score_->toggleEffect(kAccent);
        return true;
    });
    steps_.emplace_back([this, check] {
        if (!effecting_) {
            return false;
        }
        check((score_->caretEffects() ^ effectsBefore_) == 1 << kStaccato,
              "the accent did not go off again");
        // Bends of any form (ADR-0055).
        constexpr int kRelease = static_cast<int>(core::BendForm::BendRelease);
        constexpr int kPreBend = static_cast<int>(core::BendForm::PreBend);
        score_->setBend(kRelease, 100);
        check(score_->caretBendForm() == kRelease && score_->caretBendCents() == 100,
              "the bend and release did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change bend"), "the bend is not undoable");
        check(score_->caretStatus().contains(score_->bendName(kRelease, 100)) &&
                  score_->bendName(kRelease, 100).endsWith(QStringLiteral("1/2")),
              "the status does not name the bend");
        score_->setBend(kPreBend, 300);
        check(score_->caretBendForm() == kPreBend && score_->caretBendCents() == 300 &&
                  score_->bendAmount(300) == QStringLiteral("1 1/2"),
              "the pre-bend did not replace it");
        score_->setBend(0, 0);
        check(score_->caretBendForm() == 0 && score_->caretBendCents() == 0, "the bend did not go");
        // The beat's own effects (ADR-0056).
        score_->setStrum(1);
        check(score_->caretStrum() == 1, "the strum did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change beat effects"),
              "the beat effects are not undoable");
        check(score_->caretStatus().contains(ScoreDocument::tr("Strum down")),
              "the status does not name the strum");
        score_->setTremolo(16);
        score_->setPickStroke(2);
        check(score_->caretTremolo() == 16 && score_->caretPickStroke() == 2 &&
                  score_->caretStrum() == 1,
              "tremolo picking and the pick stroke did not join the strum");
        score_->setStrum(0);
        check(score_->caretStrum() == 0 && score_->caretTremolo() == 16,
              "the strum did not go on its own");
        // Kinds of note effects (ADR-0058).
        score_->setVibrato(static_cast<int>(core::Vibrato::Wide));
        score_->setAccent(static_cast<int>(core::Accent::Marcato));
        check(score_->undoName() == ScoreDocument::tr("Change accent"),
              "the accent's kind is not undoable");
        score_->setSlideIn(static_cast<int>(core::SlideIn::FromBelow));
        score_->setSlideOut(static_cast<int>(core::SlideOut::Downwards));
        check(score_->caretVibrato() == static_cast<int>(core::Vibrato::Wide) &&
                  score_->caretAccent() == static_cast<int>(core::Accent::Marcato) &&
                  score_->caretSlideIn() == static_cast<int>(core::SlideIn::FromBelow) &&
                  score_->caretSlideOut() == static_cast<int>(core::SlideOut::Downwards),
              "the kinds of effects did not go on");
        check(score_->undoName() == ScoreDocument::tr("Change slide"),
              "the slides are not undoable");
        // A trill to the next step of the key, then off again (ADR-0090).
        score_->toggleTrill();
        check(score_->caretTrill() > 0 && score_->undoName() == ScoreDocument::tr("Change trill") &&
                  score_->caretStatus().contains(ScoreDocument::tr("Trill")),
              "the trill did not go on");
        score_->toggleTrill();
        check(score_->caretTrill() == 0, "the trill did not come off");
        // A turn, then none (ADR-0091).
        score_->setOrnament(static_cast<int>(core::Ornament::Turn));
        check(score_->caretOrnament() == static_cast<int>(core::Ornament::Turn) &&
                  score_->undoName() == ScoreDocument::tr("Change ornament") &&
                  score_->caretStatus().contains(ScoreDocument::tr("Turn")),
              "the turn did not go on");
        score_->setOrnament(0);
        check(score_->caretOrnament() == 0, "the turn did not come off");
        // A finger and a plucking finger, then none (ADR-0093).
        score_->setFingering(static_cast<int>(core::Finger::Ring));
        check(score_->caretFingering() == static_cast<int>(core::Finger::Ring) &&
                  score_->undoName() == ScoreDocument::tr("Change fingering") &&
                  score_->caretStatus().contains(
                      ScoreDocument::tr("Finger %1")
                          .arg(score_->caretFretted() ? QStringLiteral("3") : QStringLiteral("4"))),
              "the fingering did not go on");
        score_->setFingering(0);
        check(score_->caretFingering() == 0, "the fingering did not come off");
        if (score_->caretFretted()) {
            score_->setPluck(static_cast<int>(core::Finger::Index));
            check(score_->caretPluck() == static_cast<int>(core::Finger::Index) &&
                      score_->undoName() == ScoreDocument::tr("Change plucking finger") &&
                      score_->caretStatus().contains(
                          ScoreDocument::tr("Plucked with %1").arg(QStringLiteral("i"))),
                  "the plucking finger did not go on");
            score_->setPluck(0);
            check(score_->caretPluck() == 0, "the plucking finger did not come off");
        }
        const QString status = score_->caretStatus();
        check(status.contains(ScoreDocument::tr("Wide vibrato")) &&
                  status.contains(ScoreDocument::tr("Marcato")) &&
                  status.contains(score_->slideInName(1)) &&
                  status.contains(score_->slideOutName(3)),
              "the status does not name the kinds of effects");
        // The whammy bar and the bass techniques (ADR-0060).
        constexpr int kDip = static_cast<int>(core::BendForm::BendRelease);
        score_->setWhammy(kDip, -300);
        score_->setBassTechnique(static_cast<int>(core::BassTechnique::Slap));
        check(score_->caretWhammyForm() == kDip && score_->caretWhammyCents() == -300 &&
                  score_->whammyAmount(-300) == QStringLiteral("-1 1/2") &&
                  score_->caretBassTechnique() == static_cast<int>(core::BassTechnique::Slap) &&
                  score_->caretTremolo() == 16,
              "the whammy bar and the slap did not join the beat's effects");
        check(score_->caretStatus().contains(score_->whammyName(kDip, -300)) &&
                  score_->caretStatus().contains(ScoreDocument::tr("Slap")),
              "the status does not name the whammy bar and the slap");
        score_->setWhammy(0, 0);
        check(score_->caretWhammyForm() == 0 && score_->caretBassTechnique() != 0,
              "the whammy bar did not go on its own");
        // Hairpins (ADR-0078).
        score_->setHairpin(static_cast<int>(core::Hairpin::Diminuendo));
        check(score_->caretHairpin() == static_cast<int>(core::Hairpin::Diminuendo) &&
                  score_->caretBassTechnique() != 0,
              "the diminuendo did not join the beat's effects");
        check(score_->caretStatus().contains(ScoreDocument::tr("Diminuendo")),
              "the status does not name the diminuendo");
        score_->setHairpin(0);
        check(score_->caretHairpin() == 0, "the diminuendo did not go");
        // Fermatas (ADR-0085).
        score_->setFermata(static_cast<int>(core::Fermata::Long));
        check(score_->caretFermata() == static_cast<int>(core::Fermata::Long) &&
                  score_->caretBassTechnique() != 0,
              "the long fermata did not join the beat's effects");
        check(score_->caretStatus().contains(ScoreDocument::tr("Long fermata")),
              "the status does not name the long fermata");
        score_->setFermata(0);
        check(score_->caretFermata() == 0, "the fermata did not go");
        // Octave lines (ADR-0089).
        score_->setOttava(static_cast<int>(core::Ottava::QuindicesimaAlta));
        check(score_->caretOttava() == static_cast<int>(core::Ottava::QuindicesimaAlta) &&
                  score_->caretBassTechnique() != 0,
              "the octave line did not join the beat's effects");
        check(score_->caretStatus().contains(
                  ScoreDocument::tr("Octave line %1").arg(QStringLiteral("15ma"))),
              "the status does not name the octave line");
        score_->setOttava(0);
        check(score_->caretOttava() == 0, "the octave line did not go");
        // The sustain pedal (ADR-0092).
        score_->setPedal(static_cast<int>(core::Pedal::Change));
        check(score_->caretPedal() == static_cast<int>(core::Pedal::Change) &&
                  score_->caretBassTechnique() != 0,
              "the pedal did not join the beat's effects");
        check(score_->caretStatus().contains(ScoreDocument::tr("Pedal change")),
              "the status does not name the pedal change");
        score_->setPedal(0);
        check(score_->caretPedal() == 0, "the pedal did not come up");
        // A caesura after the beat, then none (ADR-0095).
        score_->setBreath(static_cast<int>(core::Breath::Caesura));
        check(score_->caretBreath() == static_cast<int>(core::Breath::Caesura) &&
                  score_->caretBassTechnique() != 0 &&
                  score_->caretStatus().contains(ScoreDocument::tr("Caesura")),
              "the caesura did not join the beat's effects");
        score_->setBreath(0);
        check(score_->caretBreath() == 0, "the caesura did not go");
        // Slurs (ADR-0086).
        score_->setSlur(true);
        check(score_->caretSlur() && score_->caretBassTechnique() != 0,
              "the slur did not join the beat's effects");
        check(score_->caretStatus().contains(ScoreDocument::tr("Slur")),
              "the status does not name the slur");
        score_->setSlur(false);
        check(!score_->caretSlur(), "the slur did not go");
        // Grace notes (ADR-0079): the beat may already lead in with some,
        // which the toggle takes away first.
        if (score_->caretGraceNotes() > 0) {
            score_->toggleGraceNote();
            check(score_->caretGraceNotes() == 0 &&
                      score_->undoName() == ScoreDocument::tr("Remove grace notes"),
                  "the beat's grace notes did not go");
        }
        score_->toggleGraceNote();
        check(score_->caretGraceNotes() == 1 &&
                  score_->undoName() == ScoreDocument::tr("Add grace note") &&
                  score_->caretStatus().contains(ScoreDocument::tr("Grace notes")),
              "no grace note joined the caret's beat");
        score_->toggleGraceNote();
        check(score_->caretGraceNotes() == 0, "the grace note did not go");
        // Chord symbols (ADR-0080): named, offered again, taken away.
        const QString chordBefore = score_->caretChord();
        score_->setChordSymbol(QStringLiteral("Am7"));
        check(score_->caretChord() == QStringLiteral("Am7"), "the caret's beat was not named Am7");
        check(score_->undoName() == ScoreDocument::tr("Set chord symbol"),
              "naming a chord was not one undo step");
        check(score_->caretStatus().contains(ScoreDocument::tr("Chord %1").arg("Am7")),
              "the status does not name the chord");
        check(score_->songChords().contains(QStringLiteral("Am7")),
              "the popup does not offer the chord");
        score_->setChordSymbol(QString());
        check(score_->caretChord().isEmpty() &&
                  score_->undoName() == ScoreDocument::tr("Remove chord symbol"),
              "the chord symbol did not go");
        if (!chordBefore.isEmpty()) {
            score_->setChordSymbol(chordBefore);
        }
        // Beat text (ADR-0081): written, offered again, taken away.
        const QString textBefore = score_->caretText();
        score_->setBeatText(QStringLiteral("let ring"));
        check(score_->caretText() == QStringLiteral("let ring"),
              "the caret's beat did not get its text");
        check(score_->undoName() == ScoreDocument::tr("Set beat text"),
              "writing a text was not one undo step");
        check(score_->caretStatus().contains(QStringLiteral("let ring")),
              "the status does not show the text");
        check(score_->songTexts().contains(QStringLiteral("let ring")),
              "the popup does not offer the text");
        score_->setBeatText(QString());
        check(score_->caretText().isEmpty() &&
                  score_->undoName() == ScoreDocument::tr("Remove beat text"),
              "the text did not go");
        if (!textBefore.isEmpty()) {
            score_->setBeatText(textBefore);
        }
        // Clef changes (ADR-0087): from the caret's measure on, then back.
        if (const int clefBefore = score_->caretClef(); clefBefore >= 0) {
            const int other = clefBefore == 1 ? 0 : 1;
            score_->setClef(other);
            check(score_->caretClef() == other &&
                      score_->undoName() == ScoreDocument::tr("Change clef"),
                  "the clef did not change from the caret's measure");
            score_->setClef(clefBefore);
            check(score_->caretClef() == clefBefore, "the clef did not change back");
        }
        // Lyrics (ADR-0084): a new verse typed from the caret's measure, shown
        // by the lyrics popup as typed, then taken away.
        if (const int verse = score_->caretVerseCount(); verse < ScoreDocument::maxVerses()) {
            const int leftover =
                score_->setVerse(verse, score_->caretMeasure(), QStringLiteral("la_"));
            const QVariantMap typed = score_->caretVerse(verse);
            check(leftover == 0 &&
                      typed.value(QStringLiteral("text")).toString() == QStringLiteral("la_") &&
                      typed.value(QStringLiteral("first")).toInt() >= score_->caretMeasure() &&
                      score_->caretVerseCount() == verse + 1 &&
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
            score_->setVerse(verse, 0, QString());
            check(score_->caretVerseCount() == verse &&
                      score_->undoName() == ScoreDocument::tr("Remove lyrics"),
                  "the verse did not go");
        }
        // A dynamic from the caret's notes on (ADR-0047).
        constexpr int kLoud = static_cast<int>(core::Dynamic::FF);
        const int dynamic = score_->caretDynamic() == kLoud ? kLoud - 1 : kLoud;
        score_->setDynamic(dynamic);
        check(score_->caretDynamic() == dynamic &&
                  score_->undoName() == ScoreDocument::tr("Change dynamic"),
              "the dynamic did not go on as one undo step");
        if (score_->caretFretted()) {
            constexpr int kArtificial = static_cast<int>(core::HarmonicKind::Artificial);
            score_->setHarmonic(kArtificial);
            check(score_->caretHarmonic() == kArtificial &&
                      score_->caretStatus().contains(score_->harmonicName(kArtificial)),
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

void Startup::tick() {
    // The panel closes again once the smoke test clicks the score; it must
    // have opened at some point.
    if (effectsPanel_ != nullptr && effectsPanel_->property("opened").toBool()) {
        effectsOpened_ = true;
    }
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
        // Let animations settle first, the note effects panel's too once
        // `--effects` opened it.
        if (effectsPanel_ != nullptr && !effectsOpened_ &&
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
    // Nothing may cover the cursor, as the measure popup or the note effects
    // panel open at the caret can: they close first, and the click waits until
    // they are gone.
    for (const QString& name : {QStringLiteral("measurePopup"), QStringLiteral("effectsPanel"),
                                QStringLiteral("chordPopup"), QStringLiteral("textPopup"),
                                QStringLiteral("lyricsPopup")}) {
        if (auto* popup = window_->findChild<QObject*>(name);
            popup != nullptr && popup->property("visible").toBool()) {
            QMetaObject::invokeMethod(popup, "close");
            again_ = true;
            return;
        }
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
        const auto problem = [this](const QString& what) {
            if (editProblem_.isEmpty()) {
                editProblem_ = QStringLiteral("PDF export: %1").arg(what);
            }
        };
        const QTemporaryDir folder;
        const QString file = folder.filePath(QStringLiteral("score")); // ".pdf" is added
        if (!score_->exportPdf(QUrl::fromLocalFile(file))) {
            problem(QStringLiteral("it failed"));
            return false;
        }
        QFile written(file + QStringLiteral(".pdf"));
        if (!written.open(QIODevice::ReadOnly)) {
            problem(QStringLiteral("no file was written"));
            return false;
        }
        // Export as PDF… suggests the song's file with ".pdf" for its extension.
        if (score_->source().isLocalFile()) {
            const QFileInfo source(score_->source().toLocalFile());
            const QFileInfo suggested(score_->pdfFile().toLocalFile());
            if (suggested.dir() != source.dir() ||
                suggested.fileName() != source.completeBaseName() + QStringLiteral(".pdf")) {
                problem(QStringLiteral("%1 suggested for %2")
                            .arg(suggested.filePath(), source.filePath()));
            }
        } else if (!score_->pdfFile().toLocalFile().endsWith(QStringLiteral(".pdf"))) {
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
    // Another song plays from its start.
    steps_.emplace_back([this] {
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
    // `--effects` opened the note effects panel at the caret.
    if (ok && options_.smokeTest && options_.effects && !effectsOpened_) {
        ok = false;
        outcome = QStringLiteral("the note effects panel did not open");
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
