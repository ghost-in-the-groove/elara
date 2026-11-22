// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace elara::audio {

/// Sine oscillator. Real-time safe: no allocation, no locks, noexcept.
class ToneGenerator {
public:
    ToneGenerator() = default;
    ToneGenerator(double frequencyHz, double sampleRate, float amplitude) noexcept;

    /// Returns the next sample in [-amplitude, amplitude].
    [[nodiscard]] float next() noexcept;

    /// Fills `out` with consecutive mono samples.
    void render(std::span<float> out) noexcept;

private:
    double phase_ = 0.0;
    double phaseIncrement_ = 0.0;
    float amplitude_ = 0.0f;
};

/// Linear fade-in/fade-out gain for frame `position` of a sound `totalFrames` long.
/// Returns 0 outside [0, totalFrames). Real-time safe.
[[nodiscard]] float fadeGain(std::int64_t position, std::int64_t totalFrames,
                             std::int64_t fadeFrames) noexcept;

/// Renders a mono sine tone with 5 ms fades at both ends (offline helper).
[[nodiscard]] std::vector<float> renderTone(double frequencyHz, double sampleRate, double seconds,
                                            float amplitude);

/// Largest absolute sample value, 0 for an empty buffer.
[[nodiscard]] float peakLevel(std::span<const float> samples) noexcept;

} // namespace elara::audio
