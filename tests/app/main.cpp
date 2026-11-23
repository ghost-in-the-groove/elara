// SPDX-License-Identifier: AGPL-3.0-or-later
// Catch2 runner for the app tests: fonts and painting need a QGuiApplication,
// which runs without a screen on the offscreen platform.
#include <QByteArray>
#include <QGuiApplication>
#include <QtGlobal>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
