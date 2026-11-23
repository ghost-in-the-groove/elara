// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace elara::audio {
class MicrophoneInput;
class Tuner;
} // namespace elara::audio

/// QML bridge to the tuner (ADR-0029): listens to the default audio input
/// while `active` and shows what elara::audio::Tuner reads. The input is
/// opened only when the tuner is shown, so the app and its smoke test start
/// without touching audio hardware.
class TunerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(QString noteName READ noteName NOTIFY readingChanged)
    Q_PROPERTY(int octave READ octave NOTIFY readingChanged)
    Q_PROPERTY(double cents READ cents NOTIFY readingChanged)
    Q_PROPERTY(double frequency READ frequency NOTIFY readingChanged)
    Q_PROPERTY(int stringIndex READ stringIndex NOTIFY readingChanged)
    Q_PROPERTY(double stringCents READ stringCents NOTIFY readingChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(QStringList tuningLabels READ tuningLabels CONSTANT)
    Q_PROPERTY(int tuning READ tuning WRITE setTuning NOTIFY tuningChanged)
    Q_PROPERTY(QStringList stringNames READ stringNames NOTIFY tuningChanged)
    Q_PROPERTY(int a4 READ a4 WRITE setA4 NOTIFY a4Changed)
    Q_PROPERTY(QString deviceDescription READ deviceDescription NOTIFY listeningChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    /// The cents within which a note counts as in tune.
    static constexpr double kInTuneCents = 5.0;

    explicit TunerController(QObject* parent = nullptr);
    ~TunerController() override;
    TunerController(const TunerController&) = delete;
    TunerController& operator=(const TunerController&) = delete;
    TunerController(TunerController&&) = delete;
    TunerController& operator=(TunerController&&) = delete;

    [[nodiscard]] bool active() const { return active_; }
    void setActive(bool active);
    [[nodiscard]] bool listening() const;

    [[nodiscard]] bool hasReading() const { return hasReading_; }
    /// The note's letter and accidental ("A", "C♯"), and its octave.
    [[nodiscard]] QString noteName() const { return noteName_; }
    [[nodiscard]] int octave() const { return octave_; }
    [[nodiscard]] double cents() const { return cents_; }
    [[nodiscard]] double frequency() const { return frequency_; }
    /// The nearest open string as an index into stringNames(); -1 for none.
    [[nodiscard]] int stringIndex() const { return stringIndex_; }
    [[nodiscard]] double stringCents() const { return stringCents_; }
    /// The input level, 0 (-60 dBFS or less) to 1 (full scale), falling slowly.
    [[nodiscard]] double level() const { return level_; }

    /// "Chromatic" first, then the tuning presets.
    [[nodiscard]] QStringList tuningLabels() const;
    [[nodiscard]] int tuning() const { return tuning_; }
    void setTuning(int index);
    /// The open strings of the tuning, lowest first; empty for chromatic.
    [[nodiscard]] QStringList stringNames() const { return stringNames_; }

    [[nodiscard]] int a4() const { return a4_; }
    void setA4(int hz);

    [[nodiscard]] QString deviceDescription() const;
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }

signals:
    void activeChanged();
    void listeningChanged();
    void readingChanged();
    void levelChanged();
    void tuningChanged();
    void a4Changed();
    void errorMessageChanged();

private:
    void start();
    void stop();
    void poll();
    void applyOptions();
    void showReading();
    void setErrorMessage(const QString& message);

    std::unique_ptr<elara::audio::MicrophoneInput> input_;
    std::unique_ptr<elara::audio::Tuner> tuner_;
    std::vector<float> block_;
    QTimer pollTimer_;
    bool active_ = false;
    bool hasReading_ = false;
    QString noteName_;
    int octave_ = 0;
    double cents_ = 0.0;
    double frequency_ = 0.0;
    int stringIndex_ = -1;
    double stringCents_ = 0.0;
    double level_ = 0.0;
    int tuning_ = 1; // standard guitar
    QStringList stringNames_;
    int a4_ = 440;
    QString errorMessage_;
};
