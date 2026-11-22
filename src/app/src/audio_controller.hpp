// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace elara::audio {
class SystemAudio;
}

/// QML bridge to the system audio device. The device is opened lazily on the
/// first playTestTone() so the UI (and the offscreen smoke test) start without
/// touching audio hardware.
class AudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString deviceDescription READ deviceDescription NOTIFY deviceDescriptionChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit AudioController(QObject* parent = nullptr);
    ~AudioController() override;
    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;
    AudioController(AudioController&&) = delete;
    AudioController& operator=(AudioController&&) = delete;

    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] QString deviceDescription() const { return deviceDescription_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }

    Q_INVOKABLE void playTestTone(double frequencyHz = 440.0, double seconds = 1.5);

signals:
    void playingChanged();
    void deviceDescriptionChanged();
    void errorMessageChanged();

private:
    void setPlaying(bool playing);
    void setErrorMessage(const QString& message);
    void pollPlayback();

    std::unique_ptr<elara::audio::SystemAudio> audio_;
    QTimer pollTimer_;
    bool playing_ = false;
    QString deviceDescription_;
    QString errorMessage_;
};
