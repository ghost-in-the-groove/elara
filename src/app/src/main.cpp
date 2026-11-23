// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>

#include "score_document.hpp"
#include "startup.hpp"
#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace {

using elara::app::ScoreDocument;

bool hasArgument(int argc, char* argv[], const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    // Checks and screenshots render on the GUI thread, so that animations
    // advance with every frame whether or not the window is on a screen.
    if (hasArgument(argc, argv, "--smoke-test") || hasArgument(argc, argv, "--screenshot")) {
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Elara"));
    QGuiApplication::setOrganizationName(QStringLiteral("Elara"));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(
        elara::core::version().data(), static_cast<qsizetype>(elara::core::version().size())));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Tablature, score and music studio"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QCoreApplication::translate("main", "A score to open."),
                                 QStringLiteral("[file]"));
    const QCommandLineOption trackOption(
        QStringLiteral("track"),
        QCoreApplication::translate("main", "Show this track (from 1), or \"all\"."),
        QStringLiteral("track"));
    const QCommandLineOption viewOption(
        QStringLiteral("view"), QCoreApplication::translate("main", "Show notation, tab or both."),
        QStringLiteral("view"));
    const QCommandLineOption smokeTest(
        QStringLiteral("smoke-test"),
        QCoreApplication::translate("main", "Check the window and the file in every view, fail "
                                            "on any QML warning, and quit."));
    const QCommandLineOption screenshot(
        QStringLiteral("screenshot"),
        QCoreApplication::translate("main", "Save the window as an image once shown, and quit."),
        QStringLiteral("png"));
    const QCommandLineOption seekOption(
        QStringLiteral("seek"),
        QCoreApplication::translate("main", "Move the playback cursor to this many seconds."),
        QStringLiteral("seconds"));
    const QCommandLineOption tunerOption(
        QStringLiteral("tuner"), QCoreApplication::translate("main", "Open the tuner at start."));
    parser.addOptions({trackOption, viewOption, seekOption, smokeTest, screenshot, tunerOption});
    parser.process(app);

    elara::app::StartupOptions options;
    if (!parser.positionalArguments().isEmpty()) {
        options.file = QUrl::fromLocalFile(
            QDir::current().absoluteFilePath(parser.positionalArguments().front()));
    }
    if (parser.isSet(trackOption)) {
        const QString track = parser.value(trackOption);
        bool number = false;
        const int index = track.toInt(&number);
        if (track == QStringLiteral("all")) {
            options.track = ScoreDocument::kAllTracks;
        } else if (number && index >= 1) {
            options.track = index - 1;
        } else {
            std::fprintf(stderr, "elara: --track takes a track number from 1, or \"all\"\n");
            return EXIT_FAILURE;
        }
    }
    if (parser.isSet(viewOption)) {
        const QString view = parser.value(viewOption);
        if (view == QStringLiteral("notation")) {
            options.view = ScoreDocument::View::Standard;
        } else if (view == QStringLiteral("tab")) {
            options.view = ScoreDocument::View::Tablature;
        } else if (view == QStringLiteral("both")) {
            options.view = ScoreDocument::View::Both;
        } else {
            std::fprintf(stderr, "elara: --view takes notation, tab or both\n");
            return EXIT_FAILURE;
        }
    }
    if (parser.isSet(seekOption)) {
        bool number = false;
        const double seconds = parser.value(seekOption).toDouble(&number);
        if (!number || seconds < 0.0) {
            std::fprintf(stderr, "elara: --seek takes a number of seconds\n");
            return EXIT_FAILURE;
        }
        options.seek = seconds;
    }
    options.smokeTest = parser.isSet(smokeTest);
    options.screenshot = parser.value(screenshot);
    const bool checking = options.smokeTest || !options.screenshot.isEmpty();

    // The music font for the logo and the UI's music symbols.
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Bravura.otf"));

    QQmlApplicationEngine engine;
    bool qmlWarnings = false;
    QObject::connect(
        &engine, &QQmlEngine::warnings, &app, [&qmlWarnings](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) {
                std::fprintf(stderr, "qml: %s\n", warning.toString().toLocal8Bit().constData());
            }
            qmlWarnings = qmlWarnings || !warnings.isEmpty();
        });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

    engine.setInitialProperties({{QStringLiteral("initialFile"), options.file},
                                 {QStringLiteral("showTuner"), parser.isSet(tunerOption)}});
    engine.loadFromModule("Elara.App", "Main");

    auto* window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    if (window == nullptr || (checking && qmlWarnings)) {
        if (checking) {
            // Headless check used by CTest (app.smoke): the window must load cleanly.
            elara::app::reportSmokeTest(false, QStringLiteral("the window did not load"));
        }
        return EXIT_FAILURE;
    }
    if (checking || options.track || options.view || options.seek) {
        auto* startup = new elara::app::Startup(window, options, qmlWarnings);
        startup->start();
    }
    return QGuiApplication::exec();
}
