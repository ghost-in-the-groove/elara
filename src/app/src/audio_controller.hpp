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
class PreparedSong;
class SoundBank;
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
    // The sound of the tracks (ADR-0043): a SoundFont, or the built-in
    // synthesizer, and SFZ instruments for the guitars and the basses (ADR-0066).
    Q_PROPERTY(QString soundFont READ soundFont NOTIFY soundFontChanged)
    Q_PROPERTY(QString soundFontName READ soundFontName NOTIFY soundFontChanged)
    Q_PROPERTY(QString guitarSfz READ guitarSfz NOTIFY sfzChanged)
    Q_PROPERTY(QString guitarSfzName READ guitarSfzName NOTIFY sfzChanged)
    Q_PROPERTY(QString bassSfz READ bassSfz NOTIFY sfzChanged)
    Q_PROPERTY(QString bassSfzName READ bassSfzName NOTIFY sfzChanged)
    Q_PROPERTY(bool loadingSoundFont READ loadingSoundFont NOTIFY loadingSoundFontChanged)
    Q_PROPERTY(bool rememberSound MEMBER rememberSound_ NOTIFY rememberSoundChanged)
    // The SoundFont that comes with Elara, empty for none (ADR-0069).
    Q_PROPERTY(QString includedSoundFont READ includedSoundFont CONSTANT)
    // The guitar SFZ instrument that comes with Elara, empty for none
    // (ADR-0072).
    Q_PROPERTY(QString includedGuitarSfz READ includedGuitarSfz CONSTANT)

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
    /// The SoundFont the tracks play through; empty for the built-in synthesizer.
    [[nodiscard]] QString soundFont() const { return soundFont_.file; }
    /// Its file name without the extension.
    [[nodiscard]] QString soundFontName() const;
    /// The SFZ instruments the guitars and the basses play; empty for none.
    [[nodiscard]] QString guitarSfz() const { return guitars_.file; }
    [[nodiscard]] QString bassSfz() const { return basses_.file; }
    /// Their file names without the extension.
    [[nodiscard]] QString guitarSfzName() const;
    [[nodiscard]] QString bassSfzName() const;
    /// The SoundFont that comes with Elara's packages, which the tracks play
    /// through until another sound is chosen; empty when there is none.
    [[nodiscard]] QString includedSoundFont() const { return includedSoundFont_; }
    /// The guitar SFZ instrument that comes with Elara's packages, which the
    /// guitars play through once chosen; empty when there is none.
    [[nodiscard]] QString includedGuitarSfz() const { return includedGuitarSfz_; }
    /// True while a SoundFont or an SFZ instrument opens.
    [[nodiscard]] bool loadingSoundFont() const { return loading_ > 0; }
    /// Opens the SoundFont at `file` (a path or a file URL) on a worker; once
    /// it has, the tracks play through it. A file that does not open leaves
    /// the sound as it was and says why in errorMessage().
    Q_INVOKABLE void setSoundFont(const QString& file);
    /// Opens the SFZ instrument at `file` (a path or a file URL) and its
    /// samples on a worker for the tracks of `tracks`, "guitars" or "basses";
    /// once it has, those tracks play through it and the others as before. A
    /// file that does not open leaves the sound as it was and says why.
    Q_INVOKABLE void setSfz(const QString& tracks, const QString& file);
    /// The guitars or the basses play as the other tracks again.
    Q_INVOKABLE void clearSfz(const QString& tracks);
    /// Plays every track through the built-in synthesizer again.
    Q_INVOKABLE void useBuiltinSound();
    /// Opens the SoundFont and the SFZ instruments chosen last time, when
    /// rememberSound is on: the included SoundFont when no sound was ever
    /// chosen or the file chosen is gone.
    Q_INVOKABLE void restoreSound();
    /// The sound track `track` of the open song plays with: the timbre's
    /// name, the SoundFont preset's or the SFZ instrument's.
    Q_INVOKABLE [[nodiscard]] QString trackSound(int track) const;

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
    void soundFontChanged();
    void sfzChanged();
    void loadingSoundFontChanged();
    void rememberSoundChanged();

private:
    bool openDevice();
    void loadSong();
    void loadSequence();
    void loadSequenceNow();
    void publishSequence(const std::shared_ptr<const elara::audio::Sequence>& sequence,
                         elara::audio::PreparedSong prepared);
    void songEdited();
    void previewMix();
    void setPlaying(bool playing);
    void setErrorMessage(const QString& message);
    void pollPlayback();
    void pollTransport();
    void followDeviceRate();
    void carryLoop(const elara::audio::Sequence& from, const elara::audio::Sequence& to);
    void applyLoop();

    enum class Loop : std::uint8_t { None, Marked, Looping };

    // A sound the tracks can play through: the SoundFont, or an SFZ
    // instrument, with the file it came from.
    struct SoundPart {
        std::shared_ptr<const elara::audio::SoundBank> bank; // null for none
        QString file;
        int request = 0; // the last file asked for wins
    };
    SoundPart* sfzPart(const QString& tracks);
    void setLoading(int change);
    void useSoundPart(SoundPart& part, std::shared_ptr<const elara::audio::SoundBank> bank,
                      const QString& file);
    void useSoundBank();

    QPointer<elara::app::ScoreDocument> document_;
    std::unique_ptr<elara::audio::SystemAudio> audio_;
    std::unique_ptr<elara::audio::Player> player_;
    /// Songs prepared for player_ on a worker: only the newest is published
    /// (ADR-0112).
    elara::app::JobLane<std::shared_ptr<elara::audio::PreparedSong>> preparing_{this};
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
    std::shared_ptr<const elara::audio::SoundBank> bank_; // null for the built-in synthesizer
    SoundPart soundFont_;
    SoundPart guitars_;
    SoundPart basses_;
    QString includedSoundFont_;
    QString includedGuitarSfz_;
    int loading_ = 0; // SoundFonts and SFZ instruments opening
    bool rememberSound_ = false;
    QString deviceDescription_;
    QString errorMessage_;
};
