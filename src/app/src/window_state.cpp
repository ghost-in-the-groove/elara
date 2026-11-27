// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window_state.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QList>
#include <QPoint>
#include <QRegion>
#include <QScreen>
#include <QSettings>

#include <algorithm>
#include <memory>

namespace elara::app {

namespace {

const QString kGroup = QStringLiteral("window");
const QString kGeometry = QStringLiteral("geometry");
const QString kMaximized = QStringLiteral("maximized");
const QString kEffects = QStringLiteral("effects");
const QString kSongPane = QStringLiteral("songPane");
const QString kZoomFit = QStringLiteral("zoomFit");
const QString kZoom = QStringLiteral("zoom");
const QString kDarkPage = QStringLiteral("darkPage");
const QString kContinuous = QStringLiteral("continuous");

// What the zoom may follow; anything else read back is the default.
bool knownFit(const QString& fit) {
    return fit.isEmpty() || fit == QStringLiteral("page") || fit == QStringLiteral("width");
}

std::unique_ptr<QSettings> settingsAt(const QString& file) {
    return file.isEmpty() ? std::make_unique<QSettings>()
                          : std::make_unique<QSettings>(file, QSettings::IniFormat);
}

qint64 area(const QRect& rect) {
    return rect.isValid() ? qint64{rect.width()} * rect.height() : 0;
}

} // namespace

WindowState::WindowState(QObject* parent) : QObject(parent) {
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMilliseconds);
    connect(&settle_, &QTimer::timeout, this, &WindowState::settled);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &WindowState::save);
}

void WindowState::setSettingsFile(const QString& file) {
    if (file != settingsFile_) {
        settingsFile_ = file;
        emit settingsFileChanged();
    }
}

void WindowState::setRemember(bool remember) {
    if (remember == remember_) {
        return;
    }
    remember_ = remember;
    if (remember_) {
        read();
        putBack();
    }
    emit rememberChanged();
}

void WindowState::setAppWindow(QWindow* window) {
    if (window == window_) {
        return;
    }
    if (window_ != nullptr) {
        disconnect(window_, nullptr, this, nullptr);
    }
    window_ = window;
    if (window_ != nullptr) {
        for (const auto signal : {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged,
                                  &QWindow::heightChanged}) {
            connect(window_, signal, this, [this] { settle_.start(); });
        }
        connect(window_, &QWindow::visibilityChanged, this, &WindowState::follow);
        putBack();
    }
    emit appWindowChanged();
}

void WindowState::setZoomFit(const QString& fit) {
    if (fit != zoomFit_ && knownFit(fit)) {
        zoomFit_ = fit;
        emit stateChanged();
    }
}

void WindowState::read() {
    const auto settings = settingsAt(settingsFile_);
    settings->beginGroup(kGroup);
    restored_ = !settings->childKeys().isEmpty();
    geometry_ = settings->value(kGeometry).toRect();
    maximized_ = settings->value(kMaximized, false).toBool();
    effects_ = settings->value(kEffects, effects_).toBool();
    songPane_ = settings->value(kSongPane, songPane_).toBool();
    const QString fit = settings->value(kZoomFit, zoomFit_).toString();
    zoomFit_ = knownFit(fit) ? fit : QStringLiteral("page");
    zoom_ = settings->value(kZoom, zoom_).toDouble();
    darkPage_ = settings->value(kDarkPage, darkPage_).toBool();
    continuous_ = settings->value(kContinuous, continuous_).toBool();
    emit geometryChanged();
    emit stateChanged();
}

void WindowState::save() const {
    if (!remember_) {
        return;
    }
    const auto settings = settingsAt(settingsFile_);
    settings->beginGroup(kGroup);
    if (geometry_.isValid()) {
        settings->setValue(kGeometry, geometry_);
    } else {
        settings->remove(kGeometry);
    }
    settings->setValue(kMaximized, maximized_);
    settings->setValue(kEffects, effects_);
    settings->setValue(kSongPane, songPane_);
    settings->setValue(kZoomFit, zoomFit_);
    settings->setValue(kZoom, zoom_);
    settings->setValue(kDarkPage, darkPage_);
    settings->setValue(kContinuous, continuous_);
}

// Once the state is read and there is a window, the window goes back to
// where it was, once.
void WindowState::putBack() {
    if (!remember_ || window_ == nullptr || wentBack_) {
        return;
    }
    wentBack_ = true;
    QList<QRect> screens;
    if (const QScreen* primary = QGuiApplication::primaryScreen(); primary != nullptr) {
        screens.append(primary->availableGeometry());
    }
    for (const QScreen* screen : QGuiApplication::screens()) {
        if (screen != QGuiApplication::primaryScreen()) {
            screens.append(screen->availableGeometry());
        }
    }
    const QRect place = placed(geometry_, screens);
    if (place.isValid()) {
        window_->setGeometry(place);
        // On a screen of another scale than the one the window was made on,
        // the system scales its size and frame again as it moves there, a
        // few pixels off each time: placed once more when it shows there.
        const auto again = [this, place] {
            QTimer::singleShot(0, this, [this, place] {
                if (window_ != nullptr && window_->visibility() == QWindow::Windowed &&
                    window_->geometry() != place) {
                    window_->setGeometry(place);
                }
            });
        };
        if (window_->isVisible()) {
            again();
        } else {
            connect(window_, &QWindow::visibleChanged, this, again, Qt::SingleShotConnection);
        }
    }
    if (maximized_) {
        window_->showMaximized();
    }
}

void WindowState::follow() {
    switch (window_ != nullptr ? window_->visibility() : QWindow::Hidden) {
    case QWindow::Maximized:
        // The size it was maximized to is not its own.
        settle_.stop();
        if (!maximized_) {
            maximized_ = true;
            emit geometryChanged();
        }
        break;
    case QWindow::FullScreen:
        settle_.stop();
        break;
    case QWindow::Windowed:
        if (maximized_) {
            maximized_ = false;
            emit geometryChanged();
        }
        settle_.start();
        break;
    default: // hidden or minimized: as it was before
        break;
    }
}

void WindowState::settled() {
    if (window_ == nullptr || window_->visibility() != QWindow::Windowed) {
        return;
    }
    const QRect geometry = window_->geometry();
    if (geometry != geometry_) {
        geometry_ = geometry;
        emit geometryChanged();
    }
}

QRect WindowState::placed(const QRect& saved, std::span<const QRect> screens) {
    if (!saved.isValid() || screens.empty()) {
        return {};
    }
    QRegion free;
    for (const QRect& screen : screens) {
        free += screen;
    }
    if (QRegion(saved).subtracted(free).isEmpty()) {
        return saved;
    }
    // The screen it was mostly on, or the primary one.
    const auto most = std::ranges::max_element(
        screens, {}, [&saved](const QRect& screen) { return area(saved.intersected(screen)); });
    const QRect screen = area(saved.intersected(*most)) > 0 ? *most : screens.front();
    const QSize room = screen.size() - kFrameRoom;
    QRect place(QPoint(), saved.size().boundedTo(room));
    place.moveCenter(screen.center());
    return place;
}

} // namespace elara::app
