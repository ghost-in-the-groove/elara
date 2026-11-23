// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: runs independent jobs (parts, tracks) on several threads.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace elara::io::detail {

/// Threads for `jobs` independent jobs: `requested` when it is not 0; otherwise
/// one while `work` is below `parallelFrom` (starting threads would cost more
/// than they save) and every core above. Never more than the jobs or 64, never 0.
[[nodiscard]] inline unsigned threadsFor(unsigned requested, std::size_t work,
                                         std::size_t parallelFrom, std::size_t jobs) {
    unsigned threads = requested;
    if (threads == 0) {
        threads = work < parallelFrom ? 1U : std::max(1U, std::thread::hardware_concurrency());
    }
    return static_cast<unsigned>(
        std::max<std::size_t>(1, std::min<std::size_t>({threads, jobs, std::size_t{64}})));
}

/// Calls `job(i)` once for every i in [first, last), on the calling thread when
/// `threads` is 1 and on `threads` worker threads otherwise, each taking the next
/// index until none is left. After every thread stops, the first exception (in
/// thread order) is rethrown. Jobs must not touch each other's data.
template <typename Job>
void forEachIndex(std::size_t first, std::size_t last, unsigned threads, const Job& job) {
    if (threads <= 1) {
        for (std::size_t i = first; i < last; ++i) {
            job(i);
        }
        return;
    }
    std::atomic<std::size_t> next{first};
    std::vector<std::exception_ptr> errors(threads);
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            try {
                for (std::size_t i = next++; i < last; i = next++) {
                    job(i);
                }
            } catch (...) {
                errors[t] = std::current_exception();
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

} // namespace elara::io::detail
