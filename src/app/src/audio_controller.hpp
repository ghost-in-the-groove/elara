// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/audio/system_audio.hpp>

#include "score_document.hpp"
#include <QFuture>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace elara::audio {
class Player;
class PreparedSong;
class SoundBank;
class Sequence;
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
    // The output device (ADR-0122): chosen among outputDevices, empty for the
    // system's default; it opens on a worker.
    Q_PROPERTY(QString outputDeviceType READ outputDeviceType NOTIFY outputDeviceChanged)
    Q_PROPERTY(QString outputDeviceName READ outputDeviceName NOTIFY outputDeviceChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(bool listingOutputDevices READ listingOutputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(bool openingDevice READ openingDevice NOTIFY openingDeviceChanged)
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

    /// The output device chosen: its driver type and name, empty for the
    /// system's default.
    [[nodiscard]] QString outputDeviceType() const;
    [[nodiscard]] QString outputDeviceName() const;
    /// The output devices found by the last listOutputDevices(), each a map of
    /// "type", "name" and "isDefault" (the system's default of its type).
    [[nodiscard]] QVariantList outputDevices() const { return outputDevices_; }
    /// True while they are being looked for.
    [[nodiscard]] bool listingOutputDevices() const { return listWanted_ || listing_; }
    /// True while the device opens, which happens on a worker: playback and the
    /// test tone asked for meanwhile start once it has.
    [[nodiscard]] bool openingDevice() const { return opening_; }
    /// Looks for the output devices on a worker; outputDevices changes once
    /// they are found.
    Q_INVOKABLE void listOutputDevices();
    /// Plays through the output device `name` of the driver type `type`, or
    /// through the system's default when `name` is empty, from now on: at once
    /// when the device is already open, playing or paused as before. Kept for
    /// the next start when rememberSound is on.
    Q_INVOKABLE void setOutputDevice(const QString& type, const QString& name);

    /// Plays the score, or pauses it. The first time, the device opens first,
    /// on a worker, and playback starts once it has.
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
    /// chosen or the file chosen is gone. The output device chosen last time
    /// is chosen again; it opens on the first play.
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
    void outputDeviceChanged();
    void outputDevicesChanged();
    void openingDeviceChanged();
    void errorMessageChanged();
    void countInChanged();
    void countingInChanged();
    void loopChanged();
    void soundFontChanged();
    void sfzChanged();
    void loadingSoundFontChanged();
    void rememberSoundChanged();

private:
    [[nodiscard]] bool deviceReady() const;
    void openDevice();
    void nextDeviceJob();
    void deviceOpened(const QString& error);
    void makePlayer();
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
    void setSoundPart(SoundPart& part, std::shared_ptr<const elara::audio::SoundBank> bank,
                      const QString& file);
    void useSoundBank();

    QPointer<elara::app::ScoreDocument> document_;
    /// Made here, opened and listed on a worker (ADR-0122). While a device job
    /// runs, only the worker uses it.
    std::unique_ptr<elara::audio::SystemAudio> audio_;
    // What a device job finds: an error or the device opened, or the outputs.
    struct DeviceJobResult {
        QString error;
        QString description;
        QVariantList outputs;
    };
    QFuture<DeviceJobResult> deviceJob_; // the job running, waited for on destruction
    bool deviceBusy_ = false;            // a device job runs
    bool deviceOpen_ = false;            // audio_ has a device open
    bool opening_ = false;               // the job running opens a device
    bool listing_ = false;               // the job running lists the outputs
    bool openWanted_ = false;            // open outputDevice_ once the job running ends
    bool listWanted_ = false;            // list the outputs once the job running ends
    bool playWhenOpen_ = false;          // start playback once the device opens
    std::optional<std::pair<double, double>> toneWhenOpen_; // Hz, seconds
    elara::audio::OutputDeviceChoice outputDevice_;         // empty: the default
    elara::audio::OutputDeviceChoice openingDevice_;        // what the job opens
    QVariantList outputDevices_;
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
