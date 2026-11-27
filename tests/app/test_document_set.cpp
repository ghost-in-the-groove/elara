// SPDX-License-Identifier: AGPL-3.0-or-later
// The songs open in the window (ADR-0160): new ones and files go into tabs of
// their own, files opened together load in parallel, a file that does not open
// closes again, and measures copied in one song are pasted in another.
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include "document_set.hpp"
#include "recovery_store.hpp"
#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>

namespace {

using elara::app::DocumentSet;
using elara::app::RecoveryStore;
using elara::app::ScoreDocument;
using Move = ScoreDocument::CaretMove;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 10000) {
        QCoreApplication::processEvents();
    }
    return done();
}

QUrl corpus(const char* file) {
    return QUrl::fromLocalFile(QStringLiteral(ELARA_CORPUS_DIR "/gp/") + QString::fromLatin1(file));
}

/// Whether every song of `songs` is laid out.
bool allReady(const DocumentSet& songs) {
    for (int i = 0; i < songs.count(); ++i) {
        const ScoreDocument* song = songs.at(i);
        if (song->status() != ScoreDocument::Status::Ready || song->busy() ||
            song->pageCount() == 0) {
            return false;
        }
    }
    return true;
}

/// A song of one guitar and one measure, titled `title`.
elara::core::Song songTitled(const char* title) {
    elara::core::Song song;
    song.info().title = title;
    song.addTrack(
        elara::core::Track::fromPreset(*elara::core::findInstrumentPreset("electric-guitar")));
    song.appendMeasures(1);
    return song;
}

QString titleOf(const DocumentSet& songs, int index) {
    return songs.data(songs.index(index), DocumentSet::TitleRole).toString();
}

} // namespace

TEST_CASE("songs: one empty song at first, which the first new song goes into") {
    DocumentSet songs;
    REQUIRE(songs.count() == 1);
    ScoreDocument* first = songs.current();
    CHECK(first->song() == nullptr);
    CHECK_FALSE(songs.unsaved());

    songs.newSong(QStringLiteral("electric-guitar"));
    CHECK(songs.count() == 1);
    CHECK(songs.current() == first);
    REQUIRE(first->song() != nullptr);

    // The next one goes beside it, and is shown.
    int shown = 0;
    QObject::connect(&songs, &DocumentSet::currentChanged, [&] { ++shown; });
    songs.newSong(QStringLiteral("piano"));
    CHECK(songs.count() == 2);
    CHECK(songs.currentIndex() == 1);
    CHECK(songs.current() != first);
    CHECK(shown == 1);
    CHECK(songs.at(0) == first);
    CHECK(songs.at(2) == nullptr);
    CHECK(songs.indexOf(first) == 0);
    CHECK(songs.indexOf(&songs) == -1);

    songs.showNext();
    CHECK(songs.currentIndex() == 0);
    songs.showPrevious();
    CHECK(songs.currentIndex() == 1);
}

TEST_CASE("songs: files opened together load at once, each in a song of its own") {
    DocumentSet songs;
    songs.open({corpus("band.gp"), corpus("quartet.gp4"), corpus("piano.gp5")});
    REQUIRE(songs.count() == 3);
    // Each is on its way before any is done: they load side by side.
    for (int i = 0; i < 3; ++i) {
        CHECK(songs.at(i)->status() == ScoreDocument::Status::Loading);
        CHECK(songs.data(songs.index(i), DocumentSet::BusyRole).toBool());
    }
    CHECK(songs.currentIndex() == 0); // the first is shown while it loads
    CHECK(titleOf(songs, 1) == QStringLiteral("quartet"));

    REQUIRE(settle([&] { return allReady(songs); }));
    CHECK(songs.at(0)->source() == corpus("band.gp"));
    CHECK(songs.at(2)->source() == corpus("piano.gp5"));
    CHECK_FALSE(songs.data(songs.index(2), DocumentSet::BusyRole).toBool());

    // A file already open is shown, not opened again.
    songs.open({corpus("piano.gp5")});
    CHECK(songs.count() == 3);
    CHECK(songs.currentIndex() == 2);
    CHECK(songs.indexOfFile(corpus("quartet.gp4")) == 1);
    CHECK(songs.indexOfFile(corpus("melody.gp3")) == -1);
}

TEST_CASE("songs: a file that does not open closes again and says why") {
    DocumentSet songs;
    QStringList failures;
    QObject::connect(&songs, &DocumentSet::failed,
                     [&](const QString& message) { failures.append(message); });
    songs.newSong(QStringLiteral("electric-guitar"));
    ScoreDocument* song = songs.current();

    songs.open({corpus("missing.gp")});
    CHECK(songs.count() == 2);
    REQUIRE(settle([&] { return songs.count() == 1; }));
    CHECK(songs.current() == song);
    REQUIRE(failures.size() == 1);
    CHECK(failures.front().startsWith(QStringLiteral("missing.gp: ")));

    // The only song open: an empty one stays in its place.
    DocumentSet alone;
    alone.open({corpus("missing.gp")});
    REQUIRE(settle([&] { return alone.current()->status() != ScoreDocument::Status::Loading; }));
    CHECK(alone.count() == 1);
    CHECK(alone.current()->song() == nullptr);
}

TEST_CASE("songs: closing one shows the next, and the only one leaves an empty song") {
    DocumentSet songs;
    songs.newSong(QStringLiteral("electric-guitar"));
    songs.newSong(QStringLiteral("electric-bass"));
    songs.newSong(QStringLiteral("piano"));
    ScoreDocument* third = songs.at(2);

    songs.setCurrentIndex(1);
    songs.close(1);
    CHECK(songs.count() == 2);
    CHECK(songs.current() == third); // the one after it
    songs.close(1);
    CHECK(songs.currentIndex() == 0); // the one before the last
    // Closing one not shown keeps the one shown.
    songs.newSong(QStringLiteral("flute"));
    ScoreDocument* flute = songs.current();
    songs.close(0);
    CHECK(songs.current() == flute);
    CHECK(songs.currentIndex() == 0);

    songs.close(0);
    REQUIRE(songs.count() == 1);
    CHECK(songs.current() != flute);
    CHECK(songs.current()->song() == nullptr);
    songs.close(5); // out of range: nothing
    CHECK(songs.count() == 1);
}

TEST_CASE("songs: measures copied in one song are pasted in another") {
    DocumentSet songs;
    songs.newSong(QStringLiteral("electric-guitar"));
    ScoreDocument* from = songs.current();
    songs.newSong(QStringLiteral("electric-guitar"));
    ScoreDocument* to = songs.current();
    REQUIRE(settle([&] { return allReady(songs); }));
    CHECK_FALSE(to->canPaste());

    // A fret in the first song's first measure, copied there.
    songs.setCurrentIndex(0);
    from->typeDigit(5);
    int pasteable = 0;
    QObject::connect(to, &ScoreDocument::clipboardChanged, [&] { ++pasteable; });
    from->copy();
    CHECK(pasteable == 1);
    CHECK(to->canPaste());

    // Pasted over the second song's third measure.
    songs.setCurrentIndex(1);
    to->moveCaret(Move::NextMeasure);
    to->moveCaret(Move::NextMeasure);
    to->paste(false);
    const auto& measure = to->song()->track(0).staff(0).measure(2);
    CHECK(measure == from->song()->track(0).staff(0).measure(0));
    CHECK(to->modified());
    CHECK(songs.data(songs.index(1), DocumentSet::ModifiedRole).toBool());
    CHECK(songs.unsaved());
    CHECK(songs.nextUnsaved() == 1);
}

TEST_CASE("songs: unsaved changes are found from the song shown on") {
    DocumentSet songs;
    int unsaved = 0;
    QObject::connect(&songs, &DocumentSet::unsavedChanged, [&] { ++unsaved; });
    songs.newSong(QStringLiteral("electric-guitar"));
    songs.newSong(QStringLiteral("piano"));
    CHECK(songs.nextUnsaved() == -1);
    songs.at(0)->setSongTitle(QStringLiteral("Changed"));
    CHECK(songs.unsaved());
    CHECK(unsaved == 1);
    CHECK(songs.nextUnsaved() == 0);
    CHECK(songs.data(songs.index(0), DocumentSet::TitleRole).toString() ==
          QStringLiteral("Changed"));

    songs.close(0); // its changes discarded
    CHECK_FALSE(songs.unsaved());
    CHECK(unsaved == 2);
}

TEST_CASE("songs: copies left by a run that ended are recovered each in a song of its own") {
    const QTemporaryDir folder;
    {
        // What a run that ended with two unsaved songs left: their copies,
        // which the stores keep as they go.
        RecoveryStore riff(folder.path());
        RecoveryStore ballad(folder.path());
        REQUIRE(riff.write(songTitled("Riff"), {}, QStringLiteral("Riff"), riff.removals()));
        REQUIRE(
            ballad.write(songTitled("Ballad"), {}, QStringLiteral("Ballad"), ballad.removals()));
    }
    DocumentSet songs;
    songs.setRecoveryFolder(folder.path());
    const QVariantList left = songs.recoverable();
    REQUIRE(left.size() == 2);

    // The song shown listed them; each goes into a song of its own.
    songs.recover(left[0].toMap().value(QStringLiteral("file")).toString());
    songs.recover(left[1].toMap().value(QStringLiteral("file")).toString());
    REQUIRE(songs.count() == 2);
    CHECK(songs.currentIndex() == 1);
    REQUIRE(settle([&] { return allReady(songs); }));
    QStringList titles{songs.at(0)->songTitle(), songs.at(1)->songTitle()};
    titles.sort();
    CHECK(titles == QStringList{QStringLiteral("Ballad"), QStringLiteral("Riff")});
    CHECK(songs.at(0)->modified());
    CHECK(songs.at(1)->modified());
}
