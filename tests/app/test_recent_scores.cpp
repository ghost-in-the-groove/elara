// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <recent_scores.hpp>

using elara::app::RecentScores;

namespace {

// A score file of its own in `dir`, made empty.
QString touch(const QTemporaryDir& dir, const QString& name) {
    const QString path = dir.filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    return path;
}

QStringList namesOf(const RecentScores& recent) {
    QStringList names;
    for (const QVariant& score : recent.scores()) {
        names.append(score.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

} // namespace

TEST_CASE("Recent scores: the latest file first, each once", "[app][recent]") {
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    RecentScores recent;
    recent.add(QUrl::fromLocalFile(touch(dir, "a.gp")));
    recent.add(QUrl::fromLocalFile(touch(dir, "b.musicxml")));
    recent.add(QUrl::fromLocalFile(touch(dir, "c.elara")));
    CHECK(namesOf(recent) == QStringList{"c", "b", "a"});
    // Opened again, a file moves to the top instead of showing twice.
    recent.add(QUrl::fromLocalFile(dir.filePath("a.gp")));
    CHECK(namesOf(recent) == QStringList{"a", "c", "b"});

    const QVariantMap first = recent.scores().front().toMap();
    CHECK(first.value("url").toUrl() == QUrl::fromLocalFile(dir.filePath("a.gp")));
    CHECK(first.value("format").toString() == "GP");
    CHECK(first.value("folder").toString() == QDir::toNativeSeparators(dir.path()));
    CHECK(recent.scores()[1].toMap().value("format").toString() == "ELARA");
    CHECK(recent.scores()[2].toMap().value("format").toString() == "XML");
}

TEST_CASE("Recent scores: at most eight, the oldest dropped", "[app][recent]") {
    const QTemporaryDir dir;
    RecentScores recent;
    for (int i = 0; i < 10; ++i) {
        recent.add(QUrl::fromLocalFile(touch(dir, QStringLiteral("song%1.gp").arg(i))));
    }
    const QStringList names = namesOf(recent);
    REQUIRE(names.size() == RecentScores::kLimit);
    CHECK(names.front() == "song9");
    CHECK(names.back() == "song2");
}

TEST_CASE("Recent scores: files that are gone are not shown", "[app][recent]") {
    const QTemporaryDir dir;
    RecentScores recent;
    const QString kept = touch(dir, "kept.gp");
    const QString gone = touch(dir, "gone.gp");
    recent.add(QUrl::fromLocalFile(kept));
    recent.add(QUrl::fromLocalFile(gone));
    REQUIRE(QFile::remove(gone));
    CHECK(namesOf(recent) == QStringList{"kept"});
    // Songs without a file and other URLs are not recent files.
    recent.add(QUrl());
    recent.add(QUrl(QStringLiteral("https://example.com/song.gp")));
    CHECK(namesOf(recent) == QStringList{"kept"});
}

TEST_CASE("Recent scores: one removed, or all", "[app][recent]") {
    const QTemporaryDir dir;
    RecentScores recent;
    int changed = 0;
    QObject::connect(&recent, &RecentScores::scoresChanged, [&changed] { ++changed; });
    recent.add(QUrl::fromLocalFile(touch(dir, "a.gp")));
    recent.add(QUrl::fromLocalFile(touch(dir, "b.gp")));
    recent.remove(QUrl::fromLocalFile(dir.filePath("a.gp")));
    CHECK(namesOf(recent) == QStringList{"b"});
    recent.clear();
    CHECK(recent.scores().isEmpty());
    CHECK(changed == 4);
}

TEST_CASE("Recent scores: remembered for the next start only when asked", "[app][recent]") {
    const QTemporaryDir dir;
    const QString settings = dir.filePath("settings.ini");
    const QString song = touch(dir, "song.gp");
    {
        // Checks and screenshots leave the person's list alone.
        RecentScores check;
        check.setSettingsFile(settings);
        check.add(QUrl::fromLocalFile(song));
    }
    {
        RecentScores first;
        first.setSettingsFile(settings);
        first.setRemember(true);
        CHECK(first.scores().isEmpty());
        first.add(QUrl::fromLocalFile(song));
    }
    RecentScores next;
    next.setSettingsFile(settings);
    CHECK(next.scores().isEmpty()); // read only once it remembers
    next.setRemember(true);
    CHECK(namesOf(next) == QStringList{"song"});
}
