// SPDX-License-Identifier: AGPL-3.0-or-later
// YIN pitch detection, after A. de Cheveigné and H. Kawahara, "YIN, a
// fundamental frequency estimator for speech and music", JASA 111(4), 2002:
// the difference function, its cumulative mean normalisation, an absolute
// threshold and parabolic interpolation of the chosen lag.
#include <elara/audio/pitch_detector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace elara::audio {

namespace {

constexpr double kSilence = 1e-8; // mean square below which nothing is played (-80 dBFS)

// The squared difference of `window` samples with the same samples `lag`
// later, in four independent sums so that the additions can overlap.
double squaredDifference(const float* samples, std::size_t window, std::size_t lag) noexcept {
    const float* later = samples + lag;
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    std::size_t j = 0;
    for (; j + 4 <= window; j += 4) {
        const double d0 = static_cast<double>(samples[j]) - later[j];
        const double d1 = static_cast<double>(samples[j + 1]) - later[j + 1];
        const double d2 = static_cast<double>(samples[j + 2]) - later[j + 2];
        const double d3 = static_cast<double>(samples[j + 3]) - later[j + 3];
        s0 += d0 * d0;
        s1 += d1 * d1;
        s2 += d2 * d2;
        s3 += d3 * d3;
    }
    for (; j < window; ++j) {
        const double d = static_cast<double>(samples[j]) - later[j];
        s0 += d * d;
    }
    return (s0 + s1) + (s2 + s3);
}

} // namespace

PitchDetector::PitchDetector(double sampleRate, double minFrequency, double maxFrequency)
    : sampleRate_(sampleRate) {
    if (!(sampleRate > 0.0) || !(minFrequency > 0.0) || !(maxFrequency > minFrequency) ||
        maxFrequency >= sampleRate / 2.0) {
        throw std::invalid_argument("PitchDetector: needs 0 < min < max < half the sample rate");
    }
    minLag_ =
        std::max<std::size_t>(2, static_cast<std::size_t>(std::floor(sampleRate / maxFrequency)));
    maxLag_ = static_cast<std::size_t>(std::ceil(sampleRate / minFrequency));
    window_ = maxLag_;
    frameSize_ = window_ + maxLag_ + 1;
    difference_.resize(maxLag_ + 2);
    raw_.resize(maxLag_ + 2);
}

std::optional<PitchEstimate> PitchDetector::detect(std::span<const float> samples) noexcept {
    if (samples.size() < frameSize_) {
        return std::nullopt;
    }
    double energy = 0.0;
    for (std::size_t j = 0; j < window_; ++j) {
        energy += static_cast<double>(samples[j]) * samples[j];
    }
    if (energy / static_cast<double>(window_) < kSilence) {
        return std::nullopt;
    }

    // d(tau): how much the signal differs from itself `tau` samples later;
    // d'(tau): d divided by its mean over the lags up to tau, 1 at tau = 0.
    // The period is the first dip of d' under the threshold, followed down to
    // its bottom: the period, not one of its multiples. Lags past it are never
    // looked at, so they are not computed.
    difference_[0] = 1.0;
    raw_[0] = 0.0;
    double sum = 0.0;
    std::size_t lag = 0;
    for (std::size_t tau = 1; tau <= maxLag_ + 1; ++tau) {
        const double d = squaredDifference(samples.data(), window_, tau);
        raw_[tau] = d;
        sum += d;
        difference_[tau] = sum > 0.0 ? d * static_cast<double>(tau) / sum : 1.0;
        const std::size_t previous = tau - 1;
        if (previous >= minLag_ && difference_[previous] < kThreshold &&
            (difference_[tau] >= difference_[previous] || previous == maxLag_)) {
            lag = previous;
            break;
        }
    }
    if (lag == 0) {
        return std::nullopt;
    }

    // Between samples: the vertex of the parabola through the three raw
    // differences around the lag.
    const double before = raw_[lag - 1];
    const double at = raw_[lag];
    const double after = raw_[lag + 1];
    const double curvature = before - (2.0 * at) + after;
    const double shift =
        curvature > 0.0 ? std::clamp(0.5 * (before - after) / curvature, -0.5, 0.5) : 0.0;
    return PitchEstimate{.frequency = sampleRate_ / (static_cast<double>(lag) + shift),
                         .clarity = std::clamp(1.0 - difference_[lag], 0.0, 1.0)};
}

std::optional<TunerReading> tunerReading(double frequency, double a4,
                                         const std::optional<core::Tuning>& tuning) {
    if (!(frequency > 0.0) || !(a4 > 0.0) || !std::isfinite(frequency)) {
        return std::nullopt;
    }
    const double semitones = 69.0 + (12.0 * std::log2(frequency / a4)); // as a MIDI number
    const long key = std::lround(semitones);
    if (key < 0 || key > 127) {
        return std::nullopt;
    }
    TunerReading reading{.frequency = frequency,
                         .note = core::Pitch::fromMidi(static_cast<int>(key)),
                         .cents = 100.0 * (semitones - static_cast<double>(key))};
    if (tuning) {
        for (int string = 1; string <= tuning->stringCount(); ++string) {
            const double cents =
                100.0 * (semitones - static_cast<double>(tuning->openString(string).midiNumber()));
            if (reading.string == 0 || std::abs(cents) < std::abs(reading.stringCents)) {
                reading.string = string;
                reading.stringCents = cents;
            }
        }
    }
    return reading;
}

} // namespace elara::audio
