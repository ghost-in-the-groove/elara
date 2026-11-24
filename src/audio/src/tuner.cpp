// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/tuner.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace elara::audio {

namespace {

double centsBetween(double frequency, double reference) {
    return 1200.0 * std::log2(frequency / reference);
}

} // namespace

Tuner::Tuner(double sampleRate, TunerOptions options)
    : detector_(sampleRate), options_(std::move(options)), window_(detector_.frameSize(), 0.0f) {}

void Tuner::setOptions(TunerOptions options) {
    options_ = std::move(options);
    if (reading_) {
        update();
    }
}

bool Tuner::push(std::span<const float> samples) {
    const std::size_t size = window_.size();
    if (samples.size() >= size) {
        std::copy(samples.end() - static_cast<std::ptrdiff_t>(size), samples.end(),
                  window_.begin());
    } else {
        const auto keep = static_cast<std::ptrdiff_t>(size - samples.size());
        std::copy(window_.end() - keep, window_.end(), window_.begin());
        std::ranges::copy(samples, window_.begin() + keep);
    }
    filled_ = std::min(size, filled_ + samples.size());
    unexamined_ += samples.size();
    if (filled_ < size || unexamined_ < size / 2) {
        return false;
    }
    const std::size_t elapsed = std::exchange(unexamined_, 0);
    const auto estimate = detector_.detect(window_);
    return observe(estimate ? std::optional(estimate->frequency) : std::nullopt, elapsed);
}

bool Tuner::observe(std::optional<double> frequency, std::size_t elapsed) {
    if (!frequency) {
        silent_ += elapsed;
        if (reading_ && static_cast<double>(silent_) > options_.holdSeconds * sampleRate()) {
            reading_.reset();
            recentCount_ = 0;
            recentNext_ = 0;
            candidate_.reset();
            return true;
        }
        return false;
    }
    silent_ = 0;
    if (recentCount_ > 0 && std::abs(centsBetween(*frequency, median())) > kSameNoteCents) {
        // Another note: believe it when it is heard twice in a row.
        if (!candidate_ || std::abs(centsBetween(*frequency, *candidate_)) > kSameNoteCents) {
            candidate_ = frequency;
            return false;
        }
        recent_[0] = *candidate_;
        recentCount_ = 1;
        recentNext_ = 1;
    }
    candidate_.reset();
    recent_[recentNext_] = *frequency;
    recentNext_ = (recentNext_ + 1) % kSmoothing;
    recentCount_ = std::min(recentCount_ + 1, kSmoothing);
    update();
    return true;
}

void Tuner::reset() noexcept {
    std::ranges::fill(window_, 0.0f);
    filled_ = 0;
    unexamined_ = 0;
    silent_ = 0;
    recentCount_ = 0;
    recentNext_ = 0;
    candidate_.reset();
    reading_.reset();
}

double Tuner::median() const {
    // Insertion sort of at most kSmoothing values: on so short an array GCC's
    // optimiser reports std::sort's unrolled steps as out of bounds.
    std::array<double, kSmoothing> sorted = recent_;
    const std::size_t count = std::min(recentCount_, sorted.size());
    for (std::size_t i = 1; i < count; ++i) {
        const double value = sorted[i];
        std::size_t j = i;
        for (; j > 0 && sorted[j - 1] > value; --j) {
            sorted[j] = sorted[j - 1];
        }
        sorted[j] = value;
    }
    const std::size_t middle = recentCount_ / 2;
    return recentCount_ % 2 == 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2.0;
}

void Tuner::update() {
    reading_ = tunerReading(median(), options_.a4, options_.tuning);
}

} // namespace elara::audio
