// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio_controller.hpp"

#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/audio/sound_font.hpp>
#include <elara/audio/system_audio.hpp>

#include "included_sound.hpp"
#include "when_done.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <optional>

AudioController::AudioController(QObject* parent)
    : QObject(parent),
      includedSoundFont_(elara::app::includedSoundFont(QCoreApplication::applicationDirPath())),
      includedGuitarSfz_(elara::app::includedGuitarSfz(QCoreApplication::applicationDirPath())) {
    using namespace std::chrono_literals;
    pollTimer_.setInterval(50ms);
    connect(&pollTimer_, &QTimer::timeout, this, &AudioController::pollPlayback);
    // About one screen refresh: the cursor follows the position.
    transportTimer_.setInterval(16ms);
    transportTimer_.setTimerType(Qt::PreciseTimer);
    connect(&transportTimer_, &QTimer::timeout, this, &AudioController::pollTransport);
}

AudioController::~AudioController() {
    deviceJob_.waitForFinished(); // a device job uses audio_ until it ends
    if (audio_) {
        audio_->setSource(nullptr); // the audio thread lets go of the player first
    }
}

void AudioController::setDocument(elara::app::ScoreDocument* document) {
    if (document_ == document) {
        return;
    }
    if (document_) {
        disconnect(document_, nullptr, this, nullptr);
    }
    document_ = document;
    if (document_) {
        connect(document_, &elara::app::ScoreDocument::songReplaced, this,
                &AudioController::loadSong);
        connect(document_, &elara::app::ScoreDocument::songChanged, this,
                &AudioController::songEdited);
        connect(document_, &elara::app::ScoreDocument::sequenceChanged, this,
                &AudioController::songEdited);
        connect(document_, &elara::app::ScoreDocument::mixPreviewed, this,
                &AudioController::previewMix);
    }
    loadSong();
    emit documentChanged();
}

// The device is open and no device job holds audio_: this thread may use it.
bool AudioController::deviceReady() const {
    return audio_ && deviceOpen_ && !deviceBusy_;
}

// Opening a device can take a moment, or seconds when it wakes up, so it
// happens on a worker; what waits for it starts once it has (ADR-0122).
void AudioController::openDevice() {
    if (opening_ && openingDevice_ == outputDevice_) {
        return; // already on its way
    }
    openWanted_ = true;
    nextDeviceJob();
}

void AudioController::listOutputDevices() {
    if (listWanted_ || listing_) {
        return;
    }
    listWanted_ = true;
    emit outputDevicesChanged();
    nextDeviceJob();
}

// One device job at a time, opening before listing. While one runs, only the
// worker uses audio_.
void AudioController::nextDeviceJob() {
    if (deviceBusy_ || (!openWanted_ && !listWanted_)) {
        return;
    }
    if (!audio_) {
        // Made on this thread, which the audio runtime then belongs to.
        audio_ = std::make_unique<elara::audio::SystemAudio>();
    }
    elara::audio::SystemAudio* audio = audio_.get(); // outlives the job: see the destructor
    if (openWanted_) {
        openWanted_ = false;
        opening_ = true;
        openingDevice_ = outputDevice_;
        deviceOpen_ = false;
        emit openingDeviceChanged();
        deviceJob_ = QtConcurrent::run([audio, device = openingDevice_] {
            DeviceJobResult result;
            if (auto opened = audio->open(device); !opened) {
                result.error = QString::fromStdString(opened.error());
            } else {
                result.description = QString::fromStdString(audio->deviceDescription());
            }
            return result;
        });
    } else {
        listWanted_ = false;
        listing_ = true;
        deviceJob_ = QtConcurrent::run([audio] {
            DeviceJobResult result;
            for (const auto& device : audio->outputDevices()) {
                result.outputs.append(
                    QVariantMap{{QStringLiteral("type"), QString::fromStdString(device.type)},
                                {QStringLiteral("name"), QString::fromStdString(device.name)},
                                {QStringLiteral("isDefault"), device.isDefault}});
            }
            return result;
        });
    }
    deviceBusy_ = true;
    elara::app::whenDone(deviceJob_, this, [this](const DeviceJobResult& result) {
        deviceBusy_ = false;
        if (opening_) {
            opening_ = false;
            if (deviceDescription_ != result.description) {
                deviceDescription_ = result.description;
                emit deviceDescriptionChanged();
            }
            deviceOpened(result.error);
            emit openingDeviceChanged();
        } else {
            listing_ = false;
            outputDevices_ = result.outputs;
            emit outputDevicesChanged();
        }
        nextDeviceJob();
    });
}

// The device opened, or did not and `error` says why: playback and the test
// tone asked for meanwhile start, unless another device was chosen since.
void AudioController::deviceOpened(const QString& error) {
    deviceOpen_ = error.isEmpty();
    if (openWanted_) {
        return; // what waits for a device waits for that one
    }
    if (!deviceOpen_) {
        setErrorMessage(tr("Audio device unavailable: %1").arg(error));
        playWhenOpen_ = false;
        toneWhenOpen_.reset();
        if (player_) {
            player_->pause();
            pollTransport();
        }
        return;
    }
    if (player_) {
        followDeviceRate(); // the new device may run at another rate
    }
    if (playWhenOpen_) {
        playWhenOpen_ = false;
        togglePlayback();
    }
    if (toneWhenOpen_) {
        const auto [frequencyHz, seconds] = *toneWhenOpen_;
        toneWhenOpen_.reset();
        playTestTone(frequencyHz, seconds);
    }
}

QString AudioController::outputDeviceType() const {
    return QString::fromStdString(outputDevice_.type);
}

QString AudioController::outputDeviceName() const {
    return QString::fromStdString(outputDevice_.name);
}

// The player renders at the device's rate and plays the document's song.
void AudioController::makePlayer() {
    player_ = std::make_unique<elara::audio::Player>(audio_->sampleRate());
    player_->setSoundBank(bank_);
    preparing_.drop();
    loadSequenceNow();
    player_->seek(position_);
    audio_->setSource(player_.get());
}

// The song was edited. When it plays as before (the document kept its
// sequence: the mix or the song's info changed), playback carries on with the
// new mix (ADR-0039); otherwise it plays the new sequence from where it is,
// playing or paused as it was. Only another song goes back to the start
// (loadSong()).
void AudioController::songEdited() {
    if (!document_ || !document_->song() || !document_->sequence()) {
        return;
    }
    if (!player_) {
        // The position waits for the device, at the same place in the music
        // when only the timing changed, and inside the song.
        const auto& sequence = document_->sequence();
        const double position = placed_
                                    ? elara::audio::carriedPosition(*placed_, position_, *sequence)
                                    : std::clamp(position_, 0.0, document_->duration());
        if (placed_) {
            carryLoop(*placed_, *sequence);
        }
        placed_ = sequence;
        if (position != position_) {
            position_ = position;
            emit positionChanged();
        }
        return;
    }
    if (document_->sequence() == loaded_) {
        player_->updateMix(*document_->song());
        return;
    }
    loadSequence();
}

// A fader moves: playback hears it at once.
void AudioController::previewMix() {
    if (player_ && document_) {
        player_->updateMix(document_->previewMixes());
    }
}

// The document's sequence with the song it was built from, then the edited
// song's mix. The player is prepared on a worker (ADR-0112): its events and
// any new sounds, which can mean reading samples. Playback goes on with what
// it has until the newest prepared song is published.
void AudioController::loadSequence() {
    preparing_.request(
        [prepare = player_->preparer(), song = document_->sequenceSong(),
         sequence = document_->sequence()] {
            return std::make_shared<elara::audio::PreparedSong>(prepare(*song, *sequence));
        },
        [this, sequence = document_->sequence()](
            const std::shared_ptr<elara::audio::PreparedSong>& prepared) {
            if (player_ && document_ && document_->song()) {
                publishSequence(sequence, std::move(*prepared));
            }
        });
}

// load() at once, for a player that has nothing to play yet.
void AudioController::loadSequenceNow() {
    publishSequence(document_->sequence(),
                    player_->preparer()(*document_->sequenceSong(), *document_->sequence()));
}

void AudioController::publishSequence(const std::shared_ptr<const elara::audio::Sequence>& sequence,
                                      elara::audio::PreparedSong prepared) {
    // Another speed or a tempo edit moves the time of every note: playback
    // goes on from the same place in the music.
    const double before = player_->position();
    const double after =
        loaded_ ? elara::audio::carriedPosition(*loaded_, before, *sequence) : before;
    if (!player_->publish(std::move(prepared))) {
        return; // prepared for the player before a device rate change
    }
    if (placed_) {
        carryLoop(*placed_, *sequence); // so does the loop
    }
    if (after != before) {
        player_->seek(after);
    }
    loaded_ = sequence;
    placed_ = sequence;
    player_->updateMix(*document_->song());
    applyLoop();
}

// A new song (or none): the transport goes back to its start.
void AudioController::loadSong() {
    clearLoop();
    loaded_.reset();
    placed_ = document_ ? document_->sequence() : nullptr;
    if (player_) {
        player_->pause();
        player_->seek(0.0);
        if (document_ && document_->song() && document_->sequence()) {
            loadSequence();
        }
    }
    pollTransport();
    if (position_ != 0.0) {
        position_ = 0.0;
        emit positionChanged();
    }
}

void AudioController::togglePlayback() {
    if (!document_ || !document_->song() || !document_->sequence()) {
        return;
    }
    // A player whose device is changing plays or pauses at once; without a
    // device ready, playback starts once it is, and a second press while it
    // opens takes that back.
    if (!deviceReady() && !(player_ && deviceBusy_)) {
        playWhenOpen_ = !playWhenOpen_;
        if (playWhenOpen_) {
            openDevice();
        }
        return;
    }
    if (!player_) {
        makePlayer();
    } else {
        followDeviceRate(); // the device may have restarted while paused
    }
    setErrorMessage({});
    if (player_->isPlaying()) {
        player_->pause();
    } else if (countIn_ && loaded_) {
        player_->play(loaded_->countInAt(player_->position()));
    } else {
        player_->play();
    }
    transportTimer_.start();
    pollTransport();
}

void AudioController::stopPlayback() {
    playWhenOpen_ = false;
    if (player_) {
        player_->pause();
        player_->seek(0.0);
    }
    pollTransport();
    if (position_ != 0.0) {
        position_ = 0.0;
        emit positionChanged();
    }
}

void AudioController::seek(double seconds) {
    if (player_) {
        player_->seek(seconds);
        transportTimer_.start();
        pollTransport();
    } else if (document_) {
        // Remembered until the device opens.
        const double clamped = std::clamp(seconds, 0.0, document_->duration());
        if (clamped != position_) {
            position_ = clamped;
            emit positionChanged();
        }
    }
}

void AudioController::pollTransport() {
    if (!player_) {
        return;
    }
    followDeviceRate();
    player_->tidy();
    const bool playing = player_->isPlaying();
    const double position = player_->position();
    if (position != position_) {
        position_ = position;
        emit positionChanged();
    }
    if (playing != playingSong_) {
        playingSong_ = playing;
        emit playingSongChanged();
    }
    if (const bool counting = playing && player_->isCountingIn(); counting != countingIn_) {
        countingIn_ = counting;
        emit countingInChanged();
    }
    if (!playing) {
        transportTimer_.stop();
    }
}

// The player renders at the rate the device had when it was made. A device
// that restarts at another one (headphones plugged in, Bluetooth switching
// profiles, the system's format changed) would play the music at the wrong
// pitch and speed: a player made at the new rate takes over, from the same
// place in the music.
void AudioController::followDeviceRate() {
    const double rate = deviceReady() ? audio_->sampleRate() : 0.0;
    if (rate <= 0.0 || rate == player_->sampleRate() || !document_ || !loaded_ ||
        loaded_ != document_->sequence() || !document_->sequenceSong()) {
        return;
    }
    auto next = std::make_unique<elara::audio::Player>(rate);
    next->setSoundBank(bank_);
    next->load(*document_->sequenceSong(), *loaded_);
    next->updateMix(*document_->song());
    next->takeOver(*player_);
    // Once the device plays the new player, the old one is no longer called.
    audio_->setSource(next.get());
    player_ = std::move(next);
    preparing_.drop();
}

void AudioController::setCountIn(bool countIn) {
    if (countIn_ != countIn) {
        countIn_ = countIn;
        emit countInChanged();
    }
}

void AudioController::markLoop() {
    switch (loop_) {
    case Loop::None:
        loop_ = Loop::Marked;
        loopFrom_ = position_;
        break;
    case Loop::Marked:
        if (position_ > loopFrom_) {
            loop_ = Loop::Looping;
            loopTo_ = position_;
        } else {
            loopFrom_ = position_; // B must come after A: this is A
        }
        break;
    case Loop::Looping:
        clearLoop();
        return;
    }
    applyLoop();
    emit loopChanged();
}

void AudioController::clearLoop() {
    if (loop_ == Loop::None) {
        return;
    }
    loop_ = Loop::None;
    loopFrom_ = 0.0;
    loopTo_ = 0.0;
    applyLoop();
    emit loopChanged();
}

// A and B keep their places in the music, as the position does.
void AudioController::carryLoop(const elara::audio::Sequence& from,
                                const elara::audio::Sequence& to) {
    if (loop_ == Loop::None) {
        return;
    }
    const double a = elara::audio::carriedPosition(from, loopFrom_, to);
    const double b =
        loop_ == Loop::Looping ? elara::audio::carriedPosition(from, loopTo_, to) : 0.0;
    if (a != loopFrom_ || b != loopTo_) {
        loopFrom_ = a;
        loopTo_ = b;
        emit loopChanged();
    }
}

void AudioController::applyLoop() {
    if (!player_) {
        return;
    }
    if (loop_ == Loop::Looping) {
        player_->setLoop(loopFrom_, loopTo_);
    } else {
        player_->setLoop(0.0, 0.0);
    }
}

namespace {

const QString kSoundFontKey = QStringLiteral("audio/soundFont");
const QString kGuitarSfzKey = QStringLiteral("audio/guitarSfz");
const QString kBassSfzKey = QStringLiteral("audio/bassSfz");
const QString kOutputTypeKey = QStringLiteral("audio/outputDeviceType");
const QString kOutputNameKey = QStringLiteral("audio/outputDeviceName");

// A local path from a path or a file URL, as file dialogs give them.
QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

QString baseName(const QString& file) {
    return file.isEmpty() ? QString() : QFileInfo(file).completeBaseName();
}

} // namespace

QString AudioController::soundFontName() const {
    return baseName(soundFont_.file);
}

QString AudioController::guitarSfzName() const {
    return baseName(guitars_.file);
}

QString AudioController::bassSfzName() const {
    return baseName(basses_.file);
}

void AudioController::setLoading(int change) {
    const bool before = loading_ > 0;
    loading_ += change;
    if ((loading_ > 0) != before) {
        emit loadingSoundFontChanged();
    }
}

void AudioController::setSoundFont(const QString& file) {
    const QString path = localPath(file);
    const int request = ++soundFont_.request;
    setLoading(1);
    // Reading the presets of a large font takes a moment: away from the
    // interface thread.
    elara::app::whenDone(
        QtConcurrent::run(
            [path] { return elara::audio::SoundFont::open(QFileInfo(path).filesystemFilePath()); }),
        this, [this, path, request](const auto& opened) {
            setLoading(-1);
            if (request != soundFont_.request) {
                return; // another sound was asked for since
            }
            if (!opened) {
                setErrorMessage(tr("The SoundFont did not open: %1")
                                    .arg(QString::fromStdString(opened.error())));
                return;
            }
            setErrorMessage({});
            useSoundPart(soundFont_, *opened, path);
        });
}

AudioController::SoundPart* AudioController::sfzPart(const QString& tracks) {
    if (tracks == QStringLiteral("guitars")) {
        return &guitars_;
    }
    if (tracks == QStringLiteral("basses")) {
        return &basses_;
    }
    return nullptr;
}

void AudioController::setSfz(const QString& tracks, const QString& file) {
    SoundPart* part = sfzPart(tracks);
    if (part == nullptr) {
        return;
    }
    const QString path = localPath(file);
    const int request = ++part->request;
    setLoading(1);
    // The samples of an instrument are read into memory, on every core.
    elara::app::whenDone(
        QtConcurrent::run(
            [path] { return elara::audio::SfzBank::open(QFileInfo(path).filesystemFilePath()); }),
        this, [this, part, path, request](const auto& opened) {
            setLoading(-1);
            if (request != part->request) {
                return; // another instrument was asked for since
            }
            if (!opened) {
                setErrorMessage(tr("The SFZ instrument did not open: %1")
                                    .arg(QString::fromStdString(opened.error())));
                return;
            }
            const auto missing = static_cast<int>((*opened)->missingSamples().size());
            setErrorMessage(
                missing == 0
                    ? QString()
                    : tr("%n sample(s) of the SFZ instrument could not be read", "", missing));
            useSoundPart(*part, *opened, path);
        });
}

void AudioController::clearSfz(const QString& tracks) {
    if (SoundPart* part = sfzPart(tracks); part != nullptr) {
        ++part->request; // an instrument still opening is no longer wanted
        useSoundPart(*part, nullptr, {});
    }
}

void AudioController::useBuiltinSound() {
    for (SoundPart* part : {&soundFont_, &guitars_, &basses_}) {
        ++part->request; // a sound still opening is no longer wanted
        setSoundPart(*part, nullptr, {});
    }
    useSoundBank(); // once, for the three parts
}

void AudioController::restoreSound() {
    if (!rememberSound_) {
        return;
    }
    const QSettings settings;
    const auto saved = [&settings](const QString& key) {
        const QString file = settings.value(key).toString();
        return !file.isEmpty() && QFileInfo::exists(file) ? file : QString();
    };
    const QString font = elara::app::startingSoundFont(
        settings.contains(kSoundFontKey)
            ? std::optional<QString>(settings.value(kSoundFontKey).toString())
            : std::nullopt,
        includedSoundFont_);
    if (!font.isEmpty()) {
        setSoundFont(font);
    }
    if (const QString file = saved(kGuitarSfzKey); !file.isEmpty()) {
        setSfz(QStringLiteral("guitars"), file);
    }
    if (const QString file = saved(kBassSfzKey); !file.isEmpty()) {
        setSfz(QStringLiteral("basses"), file);
    }
    // Not looked for now: a device that is gone says so on the first play.
    setOutputDevice(settings.value(kOutputTypeKey).toString(),
                    settings.value(kOutputNameKey).toString());
}

void AudioController::setOutputDevice(const QString& type, const QString& name) {
    elara::audio::OutputDeviceChoice device{
        .type = name.isEmpty() ? std::string() : type.toStdString(), .name = name.toStdString()};
    if (device == outputDevice_) {
        return;
    }
    outputDevice_ = std::move(device);
    emit outputDeviceChanged();
    if (rememberSound_) {
        QSettings settings;
        settings.setValue(kOutputTypeKey, outputDeviceType());
        settings.setValue(kOutputNameKey, outputDeviceName());
    }
    if (deviceOpen_ || opening_) {
        openDevice(); // what plays moves to it
    }
}

QString AudioController::trackSound(int track) const {
    const auto song = document_ ? document_->song() : nullptr;
    if (!song || track < 0 || static_cast<std::size_t>(track) >= song->trackCount()) {
        return {};
    }
    const auto bank = bank_ ? bank_ : elara::audio::builtinBank();
    return QString::fromStdString(
        bank->soundName(song->track(static_cast<std::size_t>(track)).instrument()));
}

// `part` plays through `bank`, from `file`, from now on.
void AudioController::useSoundPart(SoundPart& part,
                                   std::shared_ptr<const elara::audio::SoundBank> bank,
                                   const QString& file) {
    setSoundPart(part, std::move(bank), file);
    useSoundBank();
}

// `part` is `bank`, from `file`, which is remembered when rememberSound is
// on; useSoundBank() makes the tracks play through it.
void AudioController::setSoundPart(SoundPart& part,
                                   std::shared_ptr<const elara::audio::SoundBank> bank,
                                   const QString& file) {
    const bool font = &part == &soundFont_;
    part.bank = std::move(bank);
    if (part.file != file) {
        part.file = file;
        if (font) {
            emit soundFontChanged();
        } else {
            emit sfzChanged();
        }
    }
    if (rememberSound_) {
        QSettings().setValue(font                 ? kSoundFontKey
                             : &part == &guitars_ ? kGuitarSfzKey
                                                  : kBassSfzKey,
                             file);
    }
}

// The tracks play through the SoundFont, or the built-in synthesizer, and
// the guitars and basses through their SFZ instruments, from now on, playing
// or not, at the same place: the player makes again, as it loads the
// sequence, the sounds of the tracks whose part changed. The instruments
// share their samples with the banks they had.
void AudioController::useSoundBank() {
    using elara::audio::SfzTracks;
    std::shared_ptr<const elara::audio::SoundBank> bank = soundFont_.bank;
    const std::pair<const SoundPart*, SfzTracks> instruments[] = {{&basses_, SfzTracks::Basses},
                                                                  {&guitars_, SfzTracks::Guitars}};
    for (const auto& [part, tracks] : instruments) {
        if (const auto* sfz = dynamic_cast<const elara::audio::SfzBank*>(part->bank.get())) {
            bank = sfz->with(bank ? bank : elara::audio::builtinBank(), tracks);
        }
    }
    bank_ = std::move(bank);
    if (player_) {
        player_->setSoundBank(bank_);
        if (document_ && document_->song() && document_->sequence()) {
            loadSequence();
        }
    }
}

void AudioController::playTestTone(double frequencyHz, double seconds) {
    if (!deviceReady()) {
        toneWhenOpen_ = {frequencyHz, seconds}; // once the device opens
        openDevice();
        return;
    }
    setErrorMessage({});
    audio_->playTone(frequencyHz, seconds);
    setPlaying(true);
    pollTimer_.start();
}

void AudioController::pollPlayback() {
    if (audio_ && !audio_->isPlaying()) {
        pollTimer_.stop();
        setPlaying(false);
    }
}

void AudioController::setPlaying(bool playing) {
    if (playing_ != playing) {
        playing_ = playing;
        emit playingChanged();
    }
}

void AudioController::setErrorMessage(const QString& message) {
    if (errorMessage_ != message) {
        errorMessage_ = message;
        emit errorMessageChanged();
    }
}
