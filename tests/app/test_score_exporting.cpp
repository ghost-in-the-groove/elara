// SPDX-License-Identifier: AGPL-3.0-or-later
// The song exported through the document: as a PDF of the pages the score
// shows, laid out and written on a worker (ADR-0074, ADR-0107), and as
// MusicXML and MIDI files that read back, each export telling what its format
// left out. Each suggests the song's file with its own extension, and none
// changes the song.
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>

#include "score_document.hpp"
#include "songs_to_edit.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace {

using elara::app::ScoreDocument;
using elara::app::testing::kSongsToEdit;
using elara::app::testing::SongToEdit;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 30000) {
        QCoreApplication::processEvents();
    }
    return done();
}

std::filesystem::path pathOf(const QString& file) {
    return {file.toStdU16String()};
}

// The PDF the document exports of its song into `folder`, empty if the export
// failed or wrote nothing.
QByteArray exportedPdf(ScoreDocument& score, const QTemporaryDir& folder) {
    std::optional<bool> written;
    const auto connection =
        QObject::connect(&score, &ScoreDocument::exportFinished,
                         [&written](const QUrl& /*file*/, bool ok) { written = ok; });
    // ".pdf" is added.
    score.exportPdf(QUrl::fromLocalFile(folder.filePath(QStringLiteral("score"))));
    const bool finished = settle([&written] { return written.has_value(); });
    QObject::disconnect(connection);
    if (!finished || !*written) {
        return {};
    }
    QFile file(folder.filePath(QStringLiteral("score.pdf")));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// What the document reports of its exports: how many, and the details of the
// last, what its format left out.
struct Reports {
    int count = 0;
    QStringList last;

    explicit Reports(const ScoreDocument& score) {
        QObject::connect(&score, &ScoreDocument::report,
                         [this](const QString& /*message*/, const QStringList& details) {
                             ++count;
                             last = details;
                         });
    }
};

} // namespace

TEST_CASE("exporting: a PDF is a whole document with the pages the score shows") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song(source);

    const QByteArray pdf = exportedPdf(song.score, folder);
    REQUIRE_FALSE(pdf.isEmpty());
    CHECK(pdf.startsWith("%PDF-1.7\n"));
    CHECK(pdf.endsWith("%%EOF\n"));
    CHECK(pdf.count("/Type /Page ") == song.score.pageCount());
    CHECK(song.unchanged());
}

TEST_CASE("exporting: a PDF of a song several pages long has each of them") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song("new");
    song.score.setMeasureCount(128);
    REQUIRE(song.laidOut());
    REQUIRE(song.score.pageCount() > 1);

    const QByteArray pdf = exportedPdf(song.score, folder);
    REQUIRE_FALSE(pdf.isEmpty());
    CHECK(pdf.count("/Type /Page ") == song.score.pageCount());
}

TEST_CASE("exporting: MusicXML and MIDI read back, each telling what its format left out") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    const Reports reports(score);

    // ".musicxml" and ".mid" are added.
    REQUIRE(score.exportMusicXml(QUrl::fromLocalFile(folder.filePath(QStringLiteral("xml")))));
    REQUIRE(reports.count == 1);
    const QStringList xmlLeftOut = reports.last;
    REQUIRE(score.exportMidi(QUrl::fromLocalFile(folder.filePath(QStringLiteral("midi")))));
    REQUIRE(reports.count == 2);
    const QStringList midiLeftOut = reports.last;

    const auto xml =
        elara::io::importMusicXmlFile(pathOf(folder.filePath(QStringLiteral("xml.musicxml"))));
    const auto midi =
        elara::io::importMidiFile(pathOf(folder.filePath(QStringLiteral("midi.mid"))));
    REQUIRE(xml);
    REQUIRE(midi);
    CHECK(xml->song.trackCount() == score.song()->trackCount());
    CHECK(song.unchanged());

    // What each format leaves out of a GP4 file of four tracks.
    if (std::string_view(source) == "quartet.gp4") {
        CHECK(xml->song.trackCount() == 4);
        CHECK(xmlLeftOut.size() == 4);
        CHECK_FALSE(midiLeftOut.isEmpty());
    }
}

TEST_CASE("exporting: each export suggests the song's file with its own extension") {
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    // Beside the song's file, named after it; a song without a file is named
    // after its title, without what no file name may hold, in the documents.
    QDir folder;
    QString name;
    if (score.source().isLocalFile()) {
        const QFileInfo file(score.source().toLocalFile());
        folder = file.dir();
        name = file.completeBaseName();
    } else {
        score.setSongTitle(QStringLiteral("Riff: take 2"));
        folder.setPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        name = QStringLiteral("Riff- take 2");
    }

    for (const QString& extension :
         {QStringLiteral("pdf"), QStringLiteral("musicxml"), QStringLiteral("mid")}) {
        INFO(extension.toStdString());
        const QFileInfo suggested(score.exportFile(extension).toLocalFile());
        CHECK(suggested.dir().path().toStdString() == folder.path().toStdString());
        CHECK(suggested.fileName().toStdString() ==
              (name + QLatin1Char('.') + extension).toStdString());
    }
}
