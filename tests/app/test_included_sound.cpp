// SPDX-License-Identifier: AGPL-3.0-or-later
// The SoundFont that comes with Elara's packages and the one the app starts
// with (ADR-0069).
#include "included_sound.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace {

using elara::app::includedSoundFont;
using elara::app::startingSoundFont;

// An empty file at `path`, with its folders.
QString touch(const QString& path) {
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

TEST_CASE("the included SoundFont sits in share/elara/soundfonts next to bin", "[app][soundfont]") {
    const QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString bin = root.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));
    CHECK(includedSoundFont(bin).isEmpty());

    touch(root.filePath(QStringLiteral("share/elara/soundfonts/README.md")));
    CHECK(includedSoundFont(bin).isEmpty()); // only SF2 and SF3 files count

    const QString font = touch(root.filePath(QStringLiteral("share/elara/soundfonts/General.sf3")));
    touch(root.filePath(QStringLiteral("share/elara/soundfonts/Other.sf2")));
    CHECK(includedSoundFont(bin) == font); // the first by name
}

TEST_CASE("the included SoundFont sits in Resources/soundfonts in the macOS bundle",
          "[app][soundfont]") {
    const QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString macos = root.filePath(QStringLiteral("elara.app/Contents/MacOS"));
    REQUIRE(QDir().mkpath(macos));
    const QString font =
        touch(root.filePath(QStringLiteral("elara.app/Contents/Resources/soundfonts/General.SF3")));
    CHECK(includedSoundFont(macos) == font);
}

TEST_CASE("the app starts with the saved SoundFont, or else the included one", "[app][soundfont]") {
    const QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString included = touch(root.filePath(QStringLiteral("included.sf3")));
    const QString chosen = touch(root.filePath(QStringLiteral("chosen.sf2")));

    CHECK(startingSoundFont(std::nullopt, included) == included); // never chosen
    CHECK(startingSoundFont(std::nullopt, QString()).isEmpty());  // a build without one
    CHECK(startingSoundFont(chosen, included) == chosen);
    CHECK(startingSoundFont(QString(), included).isEmpty()); // the built-in synthesizer
    // A chosen file since moved or removed: the included one.
    CHECK(startingSoundFont(root.filePath(QStringLiteral("gone.sf2")), included) == included);
}
