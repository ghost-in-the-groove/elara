// SPDX-License-Identifier: AGPL-3.0-or-later
// What values of the model hold on the heap, for approximateBytes() (ADR-0121).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace elara::core::detail {

template <class T>
[[nodiscard]] std::size_t heapBytes(const std::vector<T>& values) noexcept {
    return values.capacity() * sizeof(T);
}

// A string short enough to live in the object holds nothing more: an empty
// string's capacity is that room on every standard library.
[[nodiscard]] inline std::size_t heapBytes(const std::string& text) noexcept {
    return text.capacity() > std::string().capacity() ? text.capacity() + 1 : 0;
}

// The control block of a shared value: two counts and two pointers.
inline constexpr std::size_t kControlBlockBytes = (2 * sizeof(long)) + (2 * sizeof(void*));

// A shared value's object and its control block.
template <class T>
[[nodiscard]] constexpr std::size_t sharedBytes() noexcept {
    return sizeof(T) + kControlBlockBytes;
}

} // namespace elara::core::detail
