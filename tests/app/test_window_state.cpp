// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>
#include <catch2/catch_test_macros.hpp>
#include <window_state.hpp>

#include <array>
#include <functional>

using elara::app::WindowState;

namespace {

// Runs the event loop until `done` holds, for at most two seconds.
bool waitFor(const std::function<bool()>& done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

} // namespace

TEST_CASE("Window state: nothing is read or written until it remembers", "[app][window]") {
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("settings.ini"));
    WindowState state;
    state.setSettingsFile(file);
    // The window as it first opens.
    CHECK(state.effects());
    CHECK_FALSE(state.songPane());
    CHECK(state.zoomFit() == QStringLiteral("page"));
    CHECK(state.darkPage());
    CHECK_FALSE(state.continuous());
    CHECK_FALSE(state.geometry().isValid());

    state.setProperty("songPane", true);
    state.save();
    CHECK_FALSE(QFile::exists(file));

    // A first start finds nothing kept, and changes nothing.
    state.setRemember(true);
    CHECK_FALSE(state.restored());
    CHECK(state.songPane());
    CHECK(state.effects());
    CHECK_FALSE(state.geometry().isValid());
}

TEST_CASE("Window state: the panels, the zoom, the page and the view come back", "[app][window]") {
    const QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("settings.ini"));
    {
        WindowState state;
        state.setSettingsFile(file);
        state.setRemember(true);
        state.setProperty("effects", false);
        state.setProperty("songPane", true);
        state.setZoomFit(QString());
        state.setProperty("zoom", 1.75);
        state.setProperty("darkPage", false);
        state.setProperty("continuous", true);
        state.save();
    }
    WindowState next;
    next.setSettingsFile(file);
    next.setRemember(true);
    CHECK(next.restored());
    CHECK_FALSE(next.effects());
    CHECK(next.songPane());
    CHECK(next.zoomFit().isEmpty());
    CHECK(next.zoom() == 1.75);
    CHECK_FALSE(next.darkPage());
    CHECK(next.continuous());
}

TEST_CASE("Window state: the zoom follows only what it knows", "[app][window]") {
    const QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("settings.ini"));
    WindowState state;
    state.setZoomFit(QStringLiteral("width"));
    state.setZoomFit(QStringLiteral("sideways"));
    CHECK(state.zoomFit() == QStringLiteral("width"));

    // A value it does not know, read back, is the default.
    {
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue(QStringLiteral("window/zoomFit"), QStringLiteral("sideways"));
    }
    WindowState read;
    read.setSettingsFile(file);
    read.setRemember(true);
    CHECK(read.zoomFit() == QStringLiteral("page"));
}

TEST_CASE("Window state: the window opens where it was, at its size", "[app][window]") {
    const QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("settings.ini"));
    const QRect kept(30, 40, 400, 300);
    {
        QWindow window;
        WindowState state;
        state.setSettingsFile(file);
        state.setAppWindow(&window);
        state.setRemember(true);
        window.setGeometry(kept);
        window.show();
        // Kept once it has settled.
        REQUIRE(waitFor([&] { return state.geometry() == kept; }));
        CHECK_FALSE(state.maximized());
        state.save();
    }
    QWindow window;
    WindowState state;
    state.setSettingsFile(file);
    state.setAppWindow(&window);
    state.setRemember(true);
    CHECK(window.geometry() == kept);
}

TEST_CASE("Window state: a maximized window keeps the place it had before", "[app][window]") {
    const QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("settings.ini"));
    const QRect kept(20, 30, 360, 240);
    {
        QWindow window;
        WindowState state;
        state.setSettingsFile(file);
        state.setAppWindow(&window);
        state.setRemember(true);
        window.setGeometry(kept);
        window.show();
        REQUIRE(waitFor([&] { return state.geometry() == kept; }));
        window.showMaximized();
        REQUIRE(waitFor([&] { return state.maximized(); }));
        // The maximized size never replaces the window's own.
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 2 * WindowState::kSettleMilliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        CHECK(state.geometry() == kept);
        state.save();
    }
    WindowState next;
    next.setSettingsFile(file);
    next.setRemember(true);
    CHECK(next.maximized());
    CHECK(next.geometry() == kept);
}

TEST_CASE("Window state: a window goes back onto the screens there are", "[app][window]") {
    // Two screens side by side, the primary one first, each without its task bar.
    const std::array screens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1280, 984)};

    // Nothing kept: the system places it.
    CHECK_FALSE(WindowState::placed(QRect(), screens).isValid());
    CHECK_FALSE(WindowState::placed(QRect(0, 0, 800, 600), {}).isValid());

    // On a screen, or across both, it stays where it was.
    CHECK(WindowState::placed(QRect(100, 80, 1200, 800), screens) == QRect(100, 80, 1200, 800));
    CHECK(WindowState::placed(QRect(1500, 100, 900, 700), screens) == QRect(1500, 100, 900, 700));

    // On a screen that is gone, it opens on the primary one, at its size.
    QRect place = WindowState::placed(QRect(4000, 200, 1000, 700), screens);
    CHECK(place.size() == QSize(1000, 700));
    CHECK(place.center() == screens[0].center());

    // Hanging off the screen it is mostly on, it opens in the middle of it.
    place = WindowState::placed(QRect(2800, 500, 1000, 700), screens);
    CHECK(place.size() == QSize(1000, 700));
    CHECK(place.center() == screens[1].center());

    // Larger than its screen now is, it shrinks to leave room for its frame.
    place = WindowState::placed(QRect(1920, 0, 2560, 1400), screens);
    CHECK(place.size() == screens[1].size() - WindowState::kFrameRoom);
    CHECK(screens[1].contains(place));
}
