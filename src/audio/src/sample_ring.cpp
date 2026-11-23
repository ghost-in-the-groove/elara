// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/sample_ring.hpp>

#include <algorithm>
#include <bit>

namespace elara::audio {

SampleRing::SampleRing(std::size_t capacity)
    : buffer_(std::bit_ceil(std::max<std::size_t>(capacity, 2))), mask_(buffer_.size() - 1) {}

std::size_t SampleRing::write(std::span<const float> samples) noexcept {
    const std::size_t written = written_.load(std::memory_order_relaxed);
    const std::size_t free = buffer_.size() - (written - read_.load(std::memory_order_acquire));
    const std::size_t count = std::min(free, samples.size());
    const std::size_t start = written & mask_;
    const std::size_t first = std::min(count, buffer_.size() - start);
    std::copy_n(samples.begin(), first, buffer_.begin() + static_cast<std::ptrdiff_t>(start));
    std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(first), count - first,
                buffer_.begin());
    written_.store(written + count, std::memory_order_release);
    if (count < samples.size()) {
        dropped_.fetch_add(samples.size() - count, std::memory_order_relaxed);
    }
    return count;
}

std::size_t SampleRing::read(std::span<float> out) noexcept {
    const std::size_t read = read_.load(std::memory_order_relaxed);
    const std::size_t count = std::min(written_.load(std::memory_order_acquire) - read, out.size());
    const std::size_t start = read & mask_;
    const std::size_t first = std::min(count, buffer_.size() - start);
    std::copy_n(buffer_.begin() + static_cast<std::ptrdiff_t>(start), first, out.begin());
    std::copy_n(buffer_.begin(), count - first, out.begin() + static_cast<std::ptrdiff_t>(first));
    read_.store(read + count, std::memory_order_release);
    return count;
}

std::size_t SampleRing::available() const noexcept {
    return written_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed);
}

} // namespace elara::audio
