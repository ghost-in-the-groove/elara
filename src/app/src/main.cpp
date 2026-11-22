// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Elara"));
    QGuiApplication::setOrganizationName(QStringLiteral("Elara"));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(
        elara::core::version().data(), static_cast<qsizetype>(elara::core::version().size())));

    // Fusion follows the system light/dark palette on every platform.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const bool smokeTest = QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"));

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

    engine.loadFromModule("Elara.App", "Main");

    if (smokeTest) {
        // Headless check used by CTest (app.smoke): the window must load cleanly.
        const bool ok = !engine.rootObjects().isEmpty() && !qmlWarnings;
        std::fprintf(ok ? stdout : stderr, "smoke-test: %s\n", ok ? "ok" : "FAILED");
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    return QGuiApplication::exec();
}
