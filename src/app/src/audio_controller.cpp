// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio_controller.hpp"

#include <elara/audio/system_audio.hpp>

#include <chrono>

AudioController::AudioController(QObject* parent) : QObject(parent) {
    using namespace std::chrono_literals;
    pollTimer_.setInterval(50ms);
    connect(&pollTimer_, &QTimer::timeout, this, &AudioController::pollPlayback);
}

AudioController::~AudioController() = default;

void AudioController::playTestTone(double frequencyHz, double seconds) {
    if (!audio_) {
        auto audio = std::make_unique<elara::audio::SystemAudio>();
        if (auto opened = audio->open(); !opened) {
            setErrorMessage(
                tr("Audio device unavailable: %1").arg(QString::fromStdString(opened.error())));
            return;
        }
        audio_ = std::move(audio);
        deviceDescription_ = QString::fromStdString(audio_->deviceDescription());
        emit deviceDescriptionChanged();
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
