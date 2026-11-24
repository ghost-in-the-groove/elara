// SPDX-License-Identifier: AGPL-3.0-or-later
// A value that copies share until one of them changes it (ADR-0051).
#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace elara::core {

/// Holds a T that its copies share: copying one costs a reference count, and
/// the first change through a shared copy gives that copy a T of its own.
/// Threads may read and copy one SharedValue while others change their own
/// copies of it. A moved-from SharedValue may only be assigned or destroyed.
template <typename T>
class SharedValue {
public:
    SharedValue() : value_(std::make_shared<T>()) {}
    explicit SharedValue(T value) : value_(std::make_shared<T>(std::move(value))) {}

    [[nodiscard]] const T& get() const noexcept { return *value_; }

    /// The T to change, copied first when another SharedValue shares it.
    [[nodiscard]] T& edit() {
        if (value_.use_count() != 1) {
            value_ = std::make_shared<T>(*value_);
        } else {
            // The copy that shared it may have gone on another thread: its
            // reads happen before the writes that follow.
            std::atomic_thread_fence(std::memory_order_acquire);
        }
        return *value_;
    }

    /// Whether `other` holds the same T, not only an equal one.
    [[nodiscard]] bool shares(const SharedValue& other) const noexcept {
        return value_ == other.value_;
    }

    friend bool operator==(const SharedValue& a, const SharedValue& b) {
        return a.value_ == b.value_ || *a.value_ == *b.value_;
    }

private:
    std::shared_ptr<T> value_;
};

} // namespace elara::core
