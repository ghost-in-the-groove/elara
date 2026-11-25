// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>

#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>
#include <recovery_store.hpp>

#include <filesystem>
#include <string>
#include <vector>

using elara::app::RecoveryStore;

namespace {

elara::core::Song songTitled(const std::string& title) {
    elara::core::Song song;
    song.info().title = title;
    return song;
}

// The title of the song a copy holds, as the copy reads back.
std::string titleIn(const RecoveryStore::Copy& copy) {
    const auto song = elara::io::loadNative(std::filesystem::path(copy.file.toStdU16String()));
    REQUIRE(song.has_value());
    return song->info().title;
}

} // namespace

TEST_CASE("Recovery copies: offered at another start once their writer is gone",
          "[app][recovery]") {
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QUrl source = QUrl::fromLocalFile(dir.filePath(QStringLiteral("riff.gp")));
    RecoveryStore next(dir.path());
    {
        RecoveryStore running(dir.path());
        REQUIRE(
            running.write(songTitled("Riff"), source, QStringLiteral("Riff"), running.removals()));
        CHECK(running.hasCopy());
        // Its writer still runs, so its copy is nobody's to recover...
        CHECK(next.left().empty());
        // ...and a store never offers its own.
        CHECK(running.left().empty());
    } // gone without removing its copy, as after a crash
    const std::vector<RecoveryStore::Copy> left = next.left();
    REQUIRE(left.size() == 1);
    CHECK(left[0].source == source);
    CHECK(left[0].title == QStringLiteral("Riff"));
    CHECK(left[0].written.secsTo(QDateTime::currentDateTime()) < 60);
    CHECK(titleIn(left[0]) == "Riff");
    // Asked again, the store offers the same copy.
    CHECK(next.left().size() == 1);
}

TEST_CASE("Recovery copies: a later write replaces the copy", "[app][recovery]") {
    const QTemporaryDir dir;
    {
        RecoveryStore crashed(dir.path());
        REQUIRE(crashed.write(songTitled("First"), QUrl(), QStringLiteral("First"),
                              crashed.removals()));
        REQUIRE(crashed.write(songTitled("Second"), QUrl(), QStringLiteral("Second"),
                              crashed.removals()));
    }
    const auto left = RecoveryStore(dir.path()).left();
    REQUIRE(left.size() == 1);
    CHECK(left[0].title == QStringLiteral("Second"));
    CHECK(titleIn(left[0]) == "Second");
    CHECK(left[0].source.isEmpty());
}

TEST_CASE("Recovery copies: a removed copy is not offered", "[app][recovery]") {
    const QTemporaryDir dir;
    {
        RecoveryStore saved(dir.path());
        REQUIRE(
            saved.write(songTitled("Saved"), QUrl(), QStringLiteral("Saved"), saved.removals()));
        saved.remove(); // the song saved, or its changes discarded
        CHECK_FALSE(saved.hasCopy());
    }
    CHECK(RecoveryStore(dir.path()).left().empty());
}

TEST_CASE("Recovery copies: a write prepared before a removal is dropped", "[app][recovery]") {
    // The song is copied on a worker; saving it meanwhile removes the copy,
    // which the worker must not then write back.
    const QTemporaryDir dir;
    RecoveryStore store(dir.path());
    const auto seen = store.removals();
    store.remove();
    CHECK_FALSE(store.write(songTitled("Stale"), QUrl(), QStringLiteral("Stale"), seen));
    CHECK_FALSE(store.hasCopy());
    CHECK(store.write(songTitled("Fresh"), QUrl(), QStringLiteral("Fresh"), store.removals()));
    CHECK(store.hasCopy());
}

TEST_CASE("Recovery copies: newest first; a discarded one is gone", "[app][recovery]") {
    const QTemporaryDir dir;
    for (const char* title : {"Older", "Newer"}) {
        RecoveryStore crashed(dir.path());
        REQUIRE(crashed.write(songTitled(title), QUrl(), QString::fromLatin1(title),
                              crashed.removals()));
        QThread::msleep(20); // apart in time
    }
    RecoveryStore next(dir.path());
    auto left = next.left();
    REQUIRE(left.size() == 2);
    CHECK(left[0].title == QStringLiteral("Newer"));
    CHECK(left[1].title == QStringLiteral("Older"));

    CHECK(next.discard(left[0].file));
    left = next.left();
    REQUIRE(left.size() == 1);
    CHECK(left[0].title == QStringLiteral("Older"));
    // A file that is no copy on offer is left alone.
    CHECK_FALSE(next.discard(dir.filePath(QStringLiteral("riff.elara"))));

    CHECK(next.discard(left[0].file));
    CHECK(next.left().empty());
    // Nothing remains of either copy.
    CHECK(QDir(dir.path()).isEmpty(QDir::Files | QDir::NoDotAndDotDot));
}

TEST_CASE("Recovery copies: a store in a folder that does not exist yet makes it",
          "[app][recovery]") {
    const QTemporaryDir dir;
    const QString folder = dir.filePath(QStringLiteral("recovery/copies"));
    RecoveryStore store(folder);
    CHECK(store.left().empty());
    CHECK(store.write(songTitled("Deep"), QUrl(), QStringLiteral("Deep"), store.removals()));
    CHECK(QDir(folder).exists());
}
