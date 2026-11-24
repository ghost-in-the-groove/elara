// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio_controller.hpp"

#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/system_audio.hpp>

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
        // The position waits for the device; it stays inside the song.
        if (const double end = document_->duration(); position_ > end) {
            position_ = end;
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
    player_->load(*document_->sequenceSong(), *document_->sequence());
    loaded_ = document_->sequence();
    player_->updateMix(*document_->song());
}

// A new song (or none): the transport goes back to its start.
void AudioController::loadSong() {
    loaded_.reset();
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
        loadSequence();
        player_->seek(position_);
        audio_->setSource(player_.get());
    }
    setErrorMessage({});
    if (player_->isPlaying()) {
        player_->pause();
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
    if (!playing) {
        transportTimer_.stop();
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
