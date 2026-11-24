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
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QUrl>
#include <QVariantMap>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

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

// The value of an option given as "--name value" or "--name=value". The
// language is read before the parser, whose help text it translates.
std::optional<QString> argumentValue(int argc, char* argv[], std::string_view name) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == name && i + 1 < argc) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
        if (argument.size() > name.size() && argument.starts_with(name) &&
            argument[name.size()] == '=') {
            return QString::fromLocal8Bit(argv[i] + name.size() + 1);
        }
    }
    return std::nullopt;
}

// Installs the translation of the UI for the language asked for, or else for
// the system's languages, falling back to English. English has a catalog of
// its own for plural forms ("1 measure"). Qt's own strings, such as the
// parser's help, come from Qt's catalogs when they are installed.
bool installTranslations(const std::optional<QString>& language, QTranslator& elara,
                         QTranslator& qt) {
    const QString directory = QStringLiteral(":/i18n");
    QLocale locale;
    if (language) {
        if (!elara.load(QStringLiteral("elara_") + *language, directory)) {
            return false;
        }
        locale = QLocale(*language);
    } else if (!elara.load(locale, QStringLiteral("elara"), QStringLiteral("_"), directory)) {
        static_cast<void>(elara.load(QStringLiteral("elara_en"), directory));
    }
    QCoreApplication::installTranslator(&elara);
    if (qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qt);
    }
    return true;
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
    // The app icon, drawn from its SVG by the build (ADR-0064), and the desktop
    // entry Linux desktops find it through.
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/elara-%1.png").arg(size), QSize(size, size));
    }
    QGuiApplication::setWindowIcon(icon);
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.ghost_in_the_groove.Elara"));

    const std::optional<QString> language = argumentValue(argc, argv, "--language");
    QTranslator elaraTranslator;
    QTranslator qtTranslator;
    if (!installTranslations(language, elaraTranslator, qtTranslator)) {
        std::fprintf(stderr, "elara: no translation for language \"%s\"; try en or es\n",
                     language->toLocal8Bit().constData());
        return EXIT_FAILURE;
    }

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
    const QCommandLineOption songPaneOption(
        QStringLiteral("song-pane"),
        QCoreApplication::translate("main", "Open the song and tracks panel at start."));
    const QCommandLineOption measureOption(
        QStringLiteral("measure"),
        QCoreApplication::translate("main", "Open the caret's measure at start."));
    const QCommandLineOption tuningOption(
        QStringLiteral("tuning"),
        QCoreApplication::translate("main",
                                    "Open the first track's tuning in the song panel at start."));
    const QCommandLineOption continuousOption(
        QStringLiteral("continuous"),
        QCoreApplication::translate("main", "Show the score as one continuous scroll."));
    const QCommandLineOption languageOption(
        QStringLiteral("language"),
        QCoreApplication::translate("main", "Show the interface in this language (en, es)."),
        QStringLiteral("language"));
    const QCommandLineOption soundFontOption(
        QStringLiteral("soundfont"),
        QCoreApplication::translate("main", "Play through this SoundFont (SF2 or SF3)."),
        QStringLiteral("file"));
    const QCommandLineOption guitarSfzOption(
        QStringLiteral("guitar-sfz"),
        QCoreApplication::translate("main", "Play the guitars through this SFZ instrument."),
        QStringLiteral("file"));
    const QCommandLineOption bassSfzOption(
        QStringLiteral("bass-sfz"),
        QCoreApplication::translate("main", "Play the basses through this SFZ instrument."),
        QStringLiteral("file"));
    parser.addOptions({trackOption, viewOption, seekOption, smokeTest, screenshot, tunerOption,
                       songPaneOption, measureOption, tuningOption, continuousOption,
                       languageOption, soundFontOption, guitarSfzOption, bassSfzOption});
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
    if (parser.isSet(soundFontOption)) {
        options.soundFont = QDir::current().absoluteFilePath(parser.value(soundFontOption));
    }
    if (parser.isSet(guitarSfzOption)) {
        options.guitarSfz = QDir::current().absoluteFilePath(parser.value(guitarSfzOption));
    }
    if (parser.isSet(bassSfzOption)) {
        options.bassSfz = QDir::current().absoluteFilePath(parser.value(bassSfzOption));
    }
    options.continuous = parser.isSet(continuousOption);
    options.smokeTest = parser.isSet(smokeTest);
    options.screenshot = parser.value(screenshot);
    const bool checking = options.smokeTest || !options.screenshot.isEmpty();
    if (checking) {
        // The catalog the UI is shown with, which app.smoke_spanish checks.
        std::printf("ui language: %s\n", qPrintable(elaraTranslator.language()));
    }

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
                                 {QStringLiteral("showTuner"), parser.isSet(tunerOption)},
                                 {QStringLiteral("showSongPane"), parser.isSet(songPaneOption)},
                                 {QStringLiteral("showMeasure"), parser.isSet(measureOption)},
                                 {QStringLiteral("showTuning"), parser.isSet(tuningOption)},
                                 {QStringLiteral("initialSoundFont"), options.soundFont},
                                 {QStringLiteral("initialGuitarSfz"), options.guitarSfz},
                                 {QStringLiteral("initialBassSfz"), options.bassSfz},
                                 // Checks leave the settings of the person using the app alone.
                                 {QStringLiteral("rememberSettings"), !checking}});
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
    if (checking || options.track || options.view || options.seek || options.continuous) {
        auto* startup = new elara::app::Startup(window, options, qmlWarnings);
        startup->start();
    }
    return QGuiApplication::exec();
}
