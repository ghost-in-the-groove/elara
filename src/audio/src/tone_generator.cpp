// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/tone_generator.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace elara::audio {

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kFadeSeconds = 0.005;
} // namespace

ToneGenerator::ToneGenerator(double frequencyHz, double sampleRate, float amplitude) noexcept
    : phaseIncrement_(sampleRate > 0.0 ? kTwoPi * frequencyHz / sampleRate : 0.0),
      amplitude_(amplitude) {}

float ToneGenerator::next() noexcept {
    const auto sample = amplitude_ * static_cast<float>(std::sin(phase_));
    phase_ += phaseIncrement_;
    if (phase_ >= kTwoPi) {
        phase_ -= kTwoPi;
    }
    return sample;
}

void ToneGenerator::render(std::span<float> out) noexcept {
    for (auto& sample : out) {
        sample = next();
    }
}

float fadeGain(std::int64_t position, std::int64_t totalFrames, std::int64_t fadeFrames) noexcept {
    if (position < 0 || position >= totalFrames) {
        return 0.0f;
    }
    if (fadeFrames <= 0) {
        return 1.0f;
    }
    const auto fromStart = static_cast<double>(position) / static_cast<double>(fadeFrames);
    const auto toEnd =
        static_cast<double>(totalFrames - 1 - position) / static_cast<double>(fadeFrames);
    return static_cast<float>(std::clamp(std::min(fromStart, toEnd), 0.0, 1.0));
}

std::vector<float> renderTone(double frequencyHz, double sampleRate, double seconds,
                              float amplitude) {
    const auto frames = static_cast<std::int64_t>(std::llround(seconds * sampleRate));
    const auto fadeFrames = static_cast<std::int64_t>(std::llround(kFadeSeconds * sampleRate));
    std::vector<float> samples(static_cast<std::size_t>(std::max<std::int64_t>(frames, 0)));
    ToneGenerator generator(frequencyHz, sampleRate, amplitude);
    for (std::int64_t i = 0; i < frames; ++i) {
        samples[static_cast<std::size_t>(i)] = generator.next() * fadeGain(i, frames, fadeFrames);
    }
    return samples;
}

float peakLevel(std::span<const float> samples) noexcept {
    float peak = 0.0f;
    for (const auto sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

} // namespace elara::audio
