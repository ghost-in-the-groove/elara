// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio_controller.hpp"

#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sound_font.hpp>
#include <elara/audio/system_audio.hpp>

#include "when_done.hpp"
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>

AudioController::AudioController(QObject* parent) : QObject(parent) {
    using namespace std::chrono_literals;
    pollTimer_.setInterval(50ms);
    connect(&pollTimer_, &QTimer::timeout, this, &AudioController::pollPlayback);
    // About one screen refresh: the cursor follows the position.
    transportTimer_.setInterval(16ms);
    transportTimer_.setTimerType(Qt::PreciseTimer);
    connect(&transportTimer_, &QTimer::timeout, this, &AudioController::pollTransport);
}

AudioController::~AudioController() {
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

bool AudioController::openDevice() {
    if (audio_) {
        return true;
    }
    auto audio = std::make_unique<elara::audio::SystemAudio>();
    if (auto opened = audio->open(); !opened) {
        setErrorMessage(
            tr("Audio device unavailable: %1").arg(QString::fromStdString(opened.error())));
        return false;
    }
    audio_ = std::move(audio);
    deviceDescription_ = QString::fromStdString(audio_->deviceDescription());
    emit deviceDescriptionChanged();
    return true;
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

// The document's sequence with the song it was built from, which an edit
// whose sequence is still being built has left behind; then that edit's mix.
void AudioController::loadSequence() {
    const auto& sequence = document_->sequence();
    // Another speed or a tempo edit moves the time of every note: playback
    // goes on from the same place in the music.
    const double before = player_->position();
    const double after =
        loaded_ ? elara::audio::carriedPosition(*loaded_, before, *sequence) : before;
    if (placed_) {
        carryLoop(*placed_, *sequence); // so does the loop
    }
    player_->load(*document_->sequenceSong(), *sequence);
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
    if (!player_) {
        if (!openDevice()) {
            return;
        }
        player_ = std::make_unique<elara::audio::Player>(audio_->sampleRate());
        player_->setSoundBank(bank_);
        loadSequence();
        player_->seek(position_);
        audio_->setSource(player_.get());
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

// A local path from a path or a file URL, as file dialogs give them.
QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

} // namespace

QString AudioController::soundFontName() const {
    return soundFont_.isEmpty() ? QString() : QFileInfo(soundFont_).completeBaseName();
}

void AudioController::setSoundFont(const QString& file) {
    const QString path = localPath(file);
    const int request = ++soundRequest_;
    if (loading_++ == 0) {
        emit loadingSoundFontChanged();
    }
    // Reading the presets of a large font takes a moment: away from the
    // interface thread.
    elara::app::whenDone(
        QtConcurrent::run(
            [path] { return elara::audio::SoundFont::open(QFileInfo(path).filesystemFilePath()); }),
        this, [this, path, request](const auto& opened) {
            if (--loading_ == 0) {
                emit loadingSoundFontChanged();
            }
            if (request != soundRequest_) {
                return; // another sound was asked for since
            }
            if (!opened) {
                setErrorMessage(tr("The SoundFont did not open: %1")
                                    .arg(QString::fromStdString(opened.error())));
                return;
            }
            setErrorMessage({});
            useSoundBank(*opened, path);
        });
}

void AudioController::useBuiltinSound() {
    ++soundRequest_; // a SoundFont still opening is no longer wanted
    useSoundBank(nullptr, {});
}

void AudioController::restoreSoundFont() {
    if (!rememberSound_) {
        return;
    }
    const QString file = QSettings().value(kSoundFontKey).toString();
    if (!file.isEmpty() && QFileInfo::exists(file)) {
        setSoundFont(file);
    }
}

// The tracks play through `bank` from now on, playing or not, at the same
// place: the player makes their sounds again as it loads the sequence.
void AudioController::useSoundBank(std::shared_ptr<const elara::audio::SoundBank> bank,
                                   const QString& file) {
    bank_ = std::move(bank);
    if (soundFont_ != file) {
        soundFont_ = file;
        emit soundFontChanged();
    }
    if (rememberSound_) {
        QSettings().setValue(kSoundFontKey, file);
    }
    if (player_) {
        player_->setSoundBank(bank_);
        if (document_ && document_->song() && document_->sequence()) {
            loadSequence();
        }
    }
}

void AudioController::playTestTone(double frequencyHz, double seconds) {
    if (!openDevice()) {
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
