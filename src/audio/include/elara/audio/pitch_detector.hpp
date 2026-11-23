// SPDX-License-Identifier: AGPL-3.0-or-later
// Pitch detection for the tuner (ADR-0028).
#pragma once

#include <elara/core/pitch.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace elara::audio {

/// A detected fundamental: its frequency in Hz and how clearly the signal
/// repeats at that period, from 0 (noise) to 1 (a pure periodic signal).
struct PitchEstimate {
    double frequency = 0.0;
    double clarity = 0.0;
};

/// Finds the fundamental frequency of a mono signal with the YIN method (de
/// Cheveigné and Kawahara, 2002): the period is the first lag where the signal
/// differs least from itself, refined between samples.
///
/// Real-time safe after construction: detect() never allocates, locks or
/// throws, so it can run on an audio input callback.
class PitchDetector {
public:
    static constexpr double kMinFrequency = 30.0;   ///< below a 5-string bass's low B
    static constexpr double kMaxFrequency = 1600.0; ///< above a guitar's 24th fret
    static constexpr double kThreshold = 0.15;      ///< YIN's absolute threshold

    /// Throws std::invalid_argument for a sample rate that is not positive or a
    /// range that is empty or reaches half the sample rate.
    explicit PitchDetector(double sampleRate, double minFrequency = kMinFrequency,
                           double maxFrequency = kMaxFrequency);

    /// Samples detect() looks at: two periods of the lowest frequency.
    [[nodiscard]] std::size_t frameSize() const noexcept { return frameSize_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// The fundamental of the first frameSize() samples (fewer: none), or none
    /// when they are silent or do not repeat clearly enough.
    [[nodiscard]] std::optional<PitchEstimate> detect(std::span<const float> samples) noexcept;

private:
    double sampleRate_;
    std::size_t minLag_;
    std::size_t maxLag_;
    std::size_t window_;
    std::size_t frameSize_;
    std::vector<double> raw_;        // the signal's squared difference with itself, by lag
    std::vector<double> difference_; // the same over its running mean
};

/// What a tuner shows for a frequency: the nearest equal-tempered note and how
/// far from it, and on a fretted instrument the nearest open string.
struct TunerReading {
    double frequency = 0.0;
    core::Pitch note{};
    double cents = 0.0; ///< above `note` when positive, -50 to 50
    int string = 0;     ///< nearest open string, 0 without a tuning
    double stringCents = 0.0;
};

/// The reading for `frequency` with A4 at `a4` Hz, spelled with sharps.
/// std::nullopt for a frequency outside the MIDI range or not positive.
[[nodiscard]] std::optional<TunerReading>
tunerReading(double frequency, double a4 = 440.0,
             const std::optional<core::Tuning>& tuning = std::nullopt);

} // namespace elara::audio
