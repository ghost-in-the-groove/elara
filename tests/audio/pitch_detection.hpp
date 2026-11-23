// SPDX-License-Identifier: AGPL-3.0-or-later
// Pitch detection for audio tests.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace elara::audio::test {

/// The fundamental frequency of `x` at `rate` frames per second, by the YIN
/// difference function: the first lag whose cumulative-mean-normalised
/// difference dips below 0.15, refined by a parabola through its neighbours.
/// 0 when there is none (between 30 Hz and 2 kHz).
inline double pitchOf(std::span<const float> x, double rate) {
    const auto maxLag = static_cast<std::size_t>(rate / 30.0);
    const std::size_t window = x.size() - maxLag;
    std::vector<double> d(maxLag + 1, 0.0);
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < window; ++i) {
            const double diff = static_cast<double>(x[i]) - static_cast<double>(x[i + lag]);
            sum += diff * diff;
        }
        d[lag] = sum;
    }
    double running = 0.0;
    std::vector<double> normalised(maxLag + 1, 1.0);
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        running += d[lag];
        normalised[lag] = running > 0.0 ? d[lag] * static_cast<double>(lag) / running : 1.0;
    }
    for (auto lag = static_cast<std::size_t>(rate / 2000.0); lag < maxLag; ++lag) {
        if (normalised[lag] < 0.15) {
            while (lag + 1 < maxLag && normalised[lag + 1] < normalised[lag]) {
                ++lag;
            }
            const double a = normalised[lag - 1];
            const double b = normalised[lag];
            const double c = normalised[lag + 1];
            const double denominator = a - 2.0 * b + c;
            const double shift = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
            return rate / (static_cast<double>(lag) + shift);
        }
    }
    return 0.0;
}

/// The frequency of MIDI key `key` plus `cents`.
inline double keyFrequency(int key, double cents = 0.0) {
    return 440.0 * std::pow(2.0, ((key - 69) + (cents / 100.0)) / 12.0);
}

} // namespace elara::audio::test
