// SPDX-License-Identifier: AGPL-3.0-or-later
// A lock-free queue of samples between two threads (ADR-0029).
#pragma once

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace elara::audio {

/// Carries samples from one producer thread (an audio input callback) to one
/// consumer thread (the tuner). write() and read() never allocate, lock or
/// block; when the consumer falls behind, the samples that do not fit are
/// dropped and counted.
class SampleRing {
public:
    /// Room for at least `capacity` samples (rounded up to a power of two).
    explicit SampleRing(std::size_t capacity);

    [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size(); }

    /// Producer: appends what fits of `samples` and returns how many.
    std::size_t write(std::span<const float> samples) noexcept;

    /// Consumer: takes up to `out.size()` samples, oldest first, and returns
    /// how many.
    std::size_t read(std::span<float> out) noexcept;

    /// Consumer: discards every sample waiting to be read.
    void clear() noexcept {
        read_.store(written_.load(std::memory_order_acquire), std::memory_order_release);
    }

    /// Consumer: samples waiting to be read.
    [[nodiscard]] std::size_t available() const noexcept;

    /// Samples dropped so far because the ring was full.
    [[nodiscard]] std::size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> buffer_;
    std::size_t mask_;
    std::atomic<std::size_t> written_{0}; // samples ever written; only the producer stores
    std::atomic<std::size_t> read_{0};    // samples ever read; only the consumer stores
    std::atomic<std::size_t> dropped_{0};
};

} // namespace elara::audio
