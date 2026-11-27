// SPDX-License-Identifier: AGPL-3.0-or-later
// The window as it was when the app last quit: its place and size, its
// panels, its zoom, its page and its view (ADR-0137).
#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWindow>

#include <span>

namespace elara::app {

/// What the window looked like when the app last quit, kept in the settings
/// for the next start while `remember` is on (off for checks and screenshots,
/// which leave the person's window alone). The state is read when `remember`
/// turns on, which also puts `appWindow` back where it was, and written as
/// the app quits (`save()`).
///
/// The place and size are followed here, from `appWindow`: they are kept
/// once they have settled and only while the window is neither maximized nor
/// full screen, so that a maximized window comes back maximized over the
/// place it had before. The rest is set by the window, which binds it to its
/// panels, its zoom, its page and its view once they are restored.
class WindowState : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY rememberChanged)
    /// Keeps the state in this INI file instead of the app's settings
    /// (tests and `--window-state`). Set before `remember` turns on.
    Q_PROPERTY(
        QString settingsFile READ settingsFile WRITE setSettingsFile NOTIFY settingsFileChanged)
    /// Whether the settings held a state when `remember` turned on.
    Q_PROPERTY(bool restored READ restored NOTIFY rememberChanged)
    /// The window it puts back and follows.
    Q_PROPERTY(QWindow* appWindow READ appWindow WRITE setAppWindow NOTIFY appWindowChanged)
    /// The window's place and size the last time it was neither maximized
    /// nor full screen; invalid until it is known.
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
    /// Whether the window was maximized.
    Q_PROPERTY(bool maximized READ maximized NOTIFY geometryChanged)
    /// The note effects palette, at the left of the score.
    Q_PROPERTY(bool effects MEMBER effects_ NOTIFY stateChanged)
    /// The song panel, at the right of the score.
    Q_PROPERTY(bool songPane MEMBER songPane_ NOTIFY stateChanged)
    /// What the zoom follows: "page" (the page across the view up to the
    /// reading zoom, ADR-0133), "width", or "" for `zoom`, chosen by hand.
    Q_PROPERTY(QString zoomFit READ zoomFit WRITE setZoomFit NOTIFY stateChanged)
    Q_PROPERTY(double zoom MEMBER zoom_ NOTIFY stateChanged)
    /// The dark page in the dark theme (ADR-0114).
    Q_PROPERTY(bool darkPage MEMBER darkPage_ NOTIFY stateChanged)
    /// The continuous view (ADR-0057).
    Q_PROPERTY(bool continuous MEMBER continuous_ NOTIFY stateChanged)

public:
    /// How long the place and size must stay still to be kept: maximizing
    /// moves and resizes the window before it says it is maximized.
    static constexpr int kSettleMilliseconds = 250;
    /// Room left around a window placed again for its frame and title bar.
    static constexpr QSize kFrameRoom{16, 48};

    explicit WindowState(QObject* parent = nullptr);

    [[nodiscard]] bool remember() const noexcept { return remember_; }
    void setRemember(bool remember);

    [[nodiscard]] QString settingsFile() const { return settingsFile_; }
    void setSettingsFile(const QString& file);

    [[nodiscard]] bool restored() const noexcept { return restored_; }

    [[nodiscard]] QWindow* appWindow() const { return window_; }
    void setAppWindow(QWindow* window);

    [[nodiscard]] QRect geometry() const { return geometry_; }
    [[nodiscard]] bool maximized() const noexcept { return maximized_; }

    [[nodiscard]] bool effects() const noexcept { return effects_; }
    [[nodiscard]] bool songPane() const noexcept { return songPane_; }
    [[nodiscard]] QString zoomFit() const { return zoomFit_; }
    void setZoomFit(const QString& fit);
    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    [[nodiscard]] bool darkPage() const noexcept { return darkPage_; }
    [[nodiscard]] bool continuous() const noexcept { return continuous_; }

    /// Writes the state to the settings, while `remember` is on. Called as
    /// the app quits.
    Q_INVOKABLE void save() const;

    /// Where a window kept at `saved` opens on screens whose free areas
    /// (without task bars) are `screens`, the primary one first: where it
    /// was while all of it is still on them; otherwise centred on the screen
    /// that holds most of it (the primary one when none does), no larger than
    /// that screen leaves room for with `kFrameRoom`. Invalid when `saved` is
    /// or there is no screen.
    [[nodiscard]] static QRect placed(const QRect& saved, std::span<const QRect> screens);

signals:
    void rememberChanged();
    void settingsFileChanged();
    void appWindowChanged();
    void geometryChanged();
    void stateChanged();

private:
    void read();
    void putBack();
    void follow();
    void settled();

    bool remember_ = false;
    bool restored_ = false;
    bool wentBack_ = false; ///< whether the window was put back where it was
    QString settingsFile_;
    QPointer<QWindow> window_;
    QTimer settle_;
    QRect geometry_;
    bool maximized_ = false;
    bool effects_ = true;
    bool songPane_ = false;
    QString zoomFit_ = QStringLiteral("page");
    double zoom_ = 1.0;
    bool darkPage_ = true;
    bool continuous_ = false;
};

} // namespace elara::app
