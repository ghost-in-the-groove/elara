// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "score_document.hpp"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
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
    // Practice (ADR-0024): a measure of clicks before playing, and the A-B loop.
    Q_PROPERTY(bool countIn READ countIn WRITE setCountIn NOTIFY countInChanged)
    Q_PROPERTY(bool countingIn READ countingIn NOTIFY countingInChanged)
    Q_PROPERTY(bool loopMarked READ loopMarked NOTIFY loopChanged)
    Q_PROPERTY(bool looping READ looping NOTIFY loopChanged)
    Q_PROPERTY(double loopFrom READ loopFrom NOTIFY loopChanged)
    Q_PROPERTY(double loopTo READ loopTo NOTIFY loopChanged)

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

    /// True when playing starts with a measure of clicks.
    [[nodiscard]] bool countIn() const { return countIn_; }
    void setCountIn(bool countIn);
    /// True while those clicks sound.
    [[nodiscard]] bool countingIn() const { return countingIn_; }

    /// True once A is marked and B is not yet.
    [[nodiscard]] bool loopMarked() const { return loop_ == Loop::Marked; }
    /// True while playback loops from loopFrom() to loopTo().
    [[nodiscard]] bool looping() const { return loop_ == Loop::Looping; }
    /// Seconds of A, when it is marked.
    [[nodiscard]] double loopFrom() const { return loopFrom_; }
    /// Seconds of B, while looping.
    [[nodiscard]] double loopTo() const { return loopTo_; }
    /// Marks A at the playback position, then B (A again when the position is
    /// not after A), and a third time ends the loop. The loop keeps its place
    /// in the music when the speed or a tempo changes; another song ends it.
    Q_INVOKABLE void markLoop();
    Q_INVOKABLE void clearLoop();

    Q_INVOKABLE void playTestTone(double frequencyHz = 440.0, double seconds = 1.5);

signals:
    void documentChanged();
    void playingSongChanged();
    void positionChanged();
    void playingChanged();
    void deviceDescriptionChanged();
    void errorMessageChanged();
    void countInChanged();
    void countingInChanged();
    void loopChanged();

private:
    bool openDevice();
    void loadSong();
    void loadSequence();
    void songEdited();
    void previewMix();
    void setPlaying(bool playing);
    void setErrorMessage(const QString& message);
    void pollPlayback();
    void pollTransport();
    void carryLoop(const elara::audio::Sequence& from, const elara::audio::Sequence& to);
    void applyLoop();

    enum class Loop : std::uint8_t { None, Marked, Looping };

    QPointer<elara::app::ScoreDocument> document_;
    std::unique_ptr<elara::audio::SystemAudio> audio_;
    std::unique_ptr<elara::audio::Player> player_;
    std::shared_ptr<const elara::audio::Sequence> loaded_; // what player_ plays
    std::shared_ptr<const elara::audio::Sequence> placed_; // what position_ counts in
    QTimer pollTimer_;
    QTimer transportTimer_;
    bool playing_ = false;
    bool playingSong_ = false;
    double position_ = 0.0;
    bool countIn_ = false;
    bool countingIn_ = false;
    Loop loop_ = Loop::None;
    double loopFrom_ = 0.0;
    double loopTo_ = 0.0;
    QString deviceDescription_;
    QString errorMessage_;
};
