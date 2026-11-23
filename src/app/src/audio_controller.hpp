// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "score_document.hpp"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace elara::audio {
class Player;
class Sequence;
class SystemAudio;
} // namespace elara::audio

/// QML bridge to the system audio device: the transport that plays the open
/// score (ADR-0019) and a test tone. The device is opened lazily, on the first
/// play or test tone, so the UI (and the offscreen smoke test) start without
/// touching audio hardware.
class AudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        elara::app::ScoreDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(bool playingSong READ playingSong NOTIFY playingSongChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
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

    /// The score the transport plays.
    [[nodiscard]] elara::app::ScoreDocument* document() const { return document_; }
    void setDocument(elara::app::ScoreDocument* document);

    /// True while the score plays.
    [[nodiscard]] bool playingSong() const { return playingSong_; }
    /// Seconds into the score.
    [[nodiscard]] double position() const { return position_; }

    /// True while the test tone sounds.
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] QString deviceDescription() const { return deviceDescription_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }

    /// Plays the score, or pauses it.
    Q_INVOKABLE void togglePlayback();
    /// Stops and goes back to the start.
    Q_INVOKABLE void stopPlayback();
    /// Moves the transport to `seconds` into the score.
    Q_INVOKABLE void seek(double seconds);

    Q_INVOKABLE void playTestTone(double frequencyHz = 440.0, double seconds = 1.5);

signals:
    void documentChanged();
    void playingSongChanged();
    void positionChanged();
    void playingChanged();
    void deviceDescriptionChanged();
    void errorMessageChanged();

private:
    bool openDevice();
    void loadSong();
    void songEdited();
    void previewMix();
    void setPlaying(bool playing);
    void setErrorMessage(const QString& message);
    void pollPlayback();
    void pollTransport();

    QPointer<elara::app::ScoreDocument> document_;
    std::unique_ptr<elara::audio::SystemAudio> audio_;
    std::unique_ptr<elara::audio::Player> player_;
    std::shared_ptr<const elara::audio::Sequence> loaded_; // what player_ plays
    QTimer pollTimer_;
    QTimer transportTimer_;
    bool playing_ = false;
    bool playingSong_ = false;
    double position_ = 0.0;
    QString deviceDescription_;
    QString errorMessage_;
};
