// SPDX-License-Identifier: AGPL-3.0-or-later
// Saving on a worker (ADR-0107) and the recovery copy (ADR-0106), through the
// document: a file holds the song as it was when its save started, an edit
// made meanwhile stays unsaved, and a copy holds the unsaved changes until
// they are undone.
#include <elara/io/native_format.hpp>

#include "recovery_store.hpp"
#include "score_document.hpp"
#include "songs_to_edit.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace {

using elara::app::ScoreDocument;
using elara::app::testing::SongToEdit;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 10000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// The title a song file holds, empty if it does not read.
std::string titleIn(const QString& file) {
    const auto song = elara::io::loadNative(std::filesystem::path(file.toStdU16String()));
    return song ? song->info().title : std::string();
}

// What the document last said of a save: whether it was written.
struct Saves {
    std::optional<bool> saved;

    explicit Saves(const ScoreDocument& score) {
        QObject::connect(&score, &ScoreDocument::saveFinished,
                         [this](const QUrl& /*file*/, bool ok) { saved = ok; });
    }
    [[nodiscard]] bool finished() {
        return settle([this] { return saved.has_value(); });
    }
};

} // namespace

TEST_CASE(
    "saving: the file holds the song as the save found it, and an edit meanwhile is unsaved") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song("new");
    ScoreDocument& score = song.score;
    Saves saves(score);

    score.setSongTitle(QStringLiteral("Saved"));
    REQUIRE(song.laidOut());
    score.save(QUrl::fromLocalFile(folder.filePath(QStringLiteral("saved"))));
    // Before the save can be done: its result comes through the event loop.
    score.setSongTitle(QStringLiteral("Edited while saving"));
    REQUIRE(saves.finished());
    const QString file = folder.filePath(QStringLiteral("saved.elara"));
    CHECK(*saves.saved);
    CHECK_FALSE(score.saving());
    CHECK(score.source() == QUrl::fromLocalFile(file));
    CHECK(score.modified());
    CHECK(titleIn(file) == "Saved");

    // Undoing the edit made while saving returns to the state saved.
    score.undo();
    REQUIRE(song.laidOut());
    CHECK(score.songTitle() == QStringLiteral("Saved"));
    CHECK_FALSE(score.modified());
}

TEST_CASE("saving: a save that ends after another song replaced the one it wrote marks nothing") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song("new");
    ScoreDocument& score = song.score;
    Saves saves(score);

    score.setSongTitle(QStringLiteral("Replaced"));
    score.save(QUrl::fromLocalFile(folder.filePath(QStringLiteral("replaced"))));
    score.newSong(QStringLiteral("electric-guitar"));
    REQUIRE(saves.finished());
    CHECK(*saves.saved);
    CHECK(titleIn(folder.filePath(QStringLiteral("replaced.elara"))) == "Replaced");
    CHECK(score.source().isEmpty());
    CHECK_FALSE(score.modified());
}

TEST_CASE("saving: the recovery copy holds the song's unsaved changes, and goes once undone") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song("new");
    ScoreDocument& score = song.score;
    score.setRecoveryFolder(folder.path());
    const elara::app::RecoveryStore* store = score.recovery();
    REQUIRE(store != nullptr);
    bool written = false;
    QObject::connect(&score, &ScoreDocument::recoveryWritten, [&written] { written = true; });

    score.setSongTitle(QStringLiteral("Recovery copy"));
    score.writeRecovery();
    REQUIRE(settle([&] { return written && store->hasCopy(); }));
    CHECK(titleIn(store->copyFile()) == "Recovery copy");

    score.undo();
    REQUIRE(song.laidOut());
    CHECK_FALSE(score.modified());
    CHECK_FALSE(store->hasCopy());
}
