// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tuner_controller.hpp"

#include <elara/audio/microphone_input.hpp>
#include <elara/audio/pitch_detector.hpp>
#include <elara/audio/tuner.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/tuning.hpp>

#include <QCoreApplication>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace {

using elara::core::Tuning;

struct TuningLabel {
    std::string_view preset;
    const char* label;
};

constexpr std::array kTuningLabels{
    TuningLabel{"guitar", QT_TRANSLATE_NOOP("TunerController", "Guitar, standard")},
    TuningLabel{"guitar-drop-d", QT_TRANSLATE_NOOP("TunerController", "Guitar, drop D")},
    TuningLabel{"guitar-half-step-down",
                QT_TRANSLATE_NOOP("TunerController", "Guitar, half step down")},
    TuningLabel{"guitar-dadgad", QT_TRANSLATE_NOOP("TunerController", "Guitar, DADGAD")},
    TuningLabel{"guitar-open-g", QT_TRANSLATE_NOOP("TunerController", "Guitar, open G")},
    TuningLabel{"guitar-open-d", QT_TRANSLATE_NOOP("TunerController", "Guitar, open D")},
    TuningLabel{"guitar-7", QT_TRANSLATE_NOOP("TunerController", "7-string guitar")},
    TuningLabel{"guitar-8", QT_TRANSLATE_NOOP("TunerController", "8-string guitar")},
    TuningLabel{"bass", QT_TRANSLATE_NOOP("TunerController", "Bass")},
    TuningLabel{"bass-drop-d", QT_TRANSLATE_NOOP("TunerController", "Bass, drop D")},
    TuningLabel{"bass-5", QT_TRANSLATE_NOOP("TunerController", "5-string bass")},
    TuningLabel{"bass-6", QT_TRANSLATE_NOOP("TunerController", "6-string bass")},
    TuningLabel{"ukulele", QT_TRANSLATE_NOOP("TunerController", "Ukulele")},
    TuningLabel{"mandolin", QT_TRANSLATE_NOOP("TunerController", "Mandolin")},
    TuningLabel{"banjo", QT_TRANSLATE_NOOP("TunerController", "Banjo")},
};

QString label(std::string_view preset) {
    const auto found = std::ranges::find(kTuningLabels, preset, &TuningLabel::preset);
    if (found == kTuningLabels.end()) {
        return QString::fromUtf8(preset.data(), static_cast<qsizetype>(preset.size()));
    }
    return QCoreApplication::translate("TunerController", found->label);
}

// A pitch's letter and accidentals with music signs: "C♯", "E♭".
QString spelled(const elara::core::Pitch& pitch) {
    constexpr std::string_view kLetters = "CDEFGAB";
    QString text(QChar::fromLatin1(kLetters[static_cast<std::size_t>(pitch.step())]));
    for (int i = 0; i < pitch.alter(); ++i) {
        text += QChar(0x266F);
    }
    for (int i = 0; i > pitch.alter(); --i) {
        text += QChar(0x266D);
    }
    return text;
}

std::optional<Tuning> presetAt(int index) {
    const auto names = Tuning::presetNames();
    if (index < 1 || index > static_cast<int>(names.size())) {
        return std::nullopt;
    }
    return Tuning::preset(names[static_cast<std::size_t>(index - 1)]);
}

} // namespace

TunerController::TunerController(QObject* parent) : QObject(parent) {
    using namespace std::chrono_literals;
    pollTimer_.setInterval(30ms);
    connect(&pollTimer_, &QTimer::timeout, this, &TunerController::poll);
    applyOptions();
}

TunerController::~TunerController() = default;

void TunerController::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (active_) {
        start();
    } else {
        stop();
    }
    emit activeChanged();
}

bool TunerController::listening() const {
    return input_ != nullptr;
}

QStringList TunerController::tuningLabels() const {
    QStringList labels{tr("Chromatic")};
    for (const std::string_view preset : Tuning::presetNames()) {
        labels << label(preset);
    }
    return labels;
}

void TunerController::setTuning(int index) {
    if (tuning_ == index || index < 0 || index > static_cast<int>(Tuning::presetNames().size())) {
        return;
    }
    tuning_ = index;
    applyOptions();
    emit tuningChanged();
}

void TunerController::setA4(int hz) {
    hz = std::clamp(hz, 400, 480);
    if (a4_ == hz) {
        return;
    }
    a4_ = hz;
    applyOptions();
    emit a4Changed();
}

QString TunerController::deviceDescription() const {
    return input_ ? QString::fromStdString(input_->deviceDescription()) : QString();
}

void TunerController::start() {
    auto input = std::make_unique<elara::audio::MicrophoneInput>();
    if (auto opened = input->open(); !opened) {
        setErrorMessage(tr("No audio input: %1").arg(QString::fromStdString(opened.error())));
        return;
    }
    if (input->sampleRate() <= 2.0 * elara::audio::PitchDetector::kMaxFrequency) {
        setErrorMessage(tr("The audio input's sample rate is too low for the tuner."));
        return;
    }
    input_ = std::move(input);
    tuner_ = std::make_unique<elara::audio::Tuner>(input_->sampleRate());
    block_.resize(8192);
    applyOptions();
    setErrorMessage({});
    pollTimer_.start();
    emit listeningChanged();
}

void TunerController::stop() {
    pollTimer_.stop();
    const bool wasListening = listening();
    input_.reset();
    tuner_.reset();
    if (hasReading_) {
        hasReading_ = false;
        emit readingChanged();
    }
    if (level_ != 0.0) {
        level_ = 0.0;
        emit levelChanged();
    }
    if (wasListening) {
        emit listeningChanged();
    }
}

void TunerController::poll() {
    bool changed = false;
    for (std::size_t got = input_->read(block_); got > 0; got = input_->read(block_)) {
        changed = tuner_->push(std::span(block_).first(got)) || changed;
    }
    if (changed) {
        showReading();
    }
    // A meter from -60 dBFS to full scale that falls back slowly.
    const double peak = std::max(1e-6, static_cast<double>(input_->takePeak()));
    const double heard = std::clamp((20.0 * std::log10(peak) + 60.0) / 60.0, 0.0, 1.0);
    const double level = std::max(heard, level_ - 0.04);
    if (std::abs(level - level_) > 1e-3) {
        level_ = level;
        emit levelChanged();
    }
}

void TunerController::applyOptions() {
    const std::optional<Tuning> tuning = presetAt(tuning_);
    QStringList names;
    if (tuning) {
        for (int string = tuning->stringCount(); string >= 1; --string) {
            const auto open = tuning->openString(string);
            names << spelled(open) + QString::number(open.octave());
        }
    }
    stringNames_ = names;
    if (tuner_) {
        tuner_->setOptions({.a4 = static_cast<double>(a4_), .tuning = tuning});
        showReading();
    }
}

void TunerController::showReading() {
    const auto& reading = tuner_->reading();
    hasReading_ = reading.has_value();
    if (reading) {
        noteName_ = spelled(reading->note);
        octave_ = reading->note.octave();
        cents_ = reading->cents;
        frequency_ = reading->frequency;
        const auto count = static_cast<int>(stringNames_.size());
        stringIndex_ = reading->string > 0 ? count - reading->string : -1;
        stringCents_ = reading->stringCents;
    }
    emit readingChanged();
}

void TunerController::setErrorMessage(const QString& message) {
    if (errorMessage_ != message) {
        errorMessage_ = message;
        emit errorMessageChanged();
    }
}
