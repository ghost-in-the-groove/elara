// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/parallel.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

using elara::core::forEachIndex;

TEST_CASE("forEachIndex runs every index once, whatever the thread count", "[parallel]") {
    for (const unsigned threads : {1U, 2U, 3U, 8U, 64U}) {
        for (const std::size_t count : {0U, 1U, 2U, 7U, 1000U}) {
            const std::size_t first = 5;
            std::vector<std::atomic<int>> runs(first + count);
            forEachIndex(first, first + count, threads, [&](std::size_t i) { ++runs[i]; });
            for (std::size_t i = 0; i < runs.size(); ++i) {
                INFO("threads " << threads << ", count " << count << ", index " << i);
                CHECK(runs[i].load() == (i < first ? 0 : 1));
            }
        }
    }
}

TEST_CASE("forEachIndex rethrows a job's exception and stays usable", "[parallel]") {
    for (const unsigned threads : {1U, 4U}) {
        INFO("threads " << threads);
        CHECK_THROWS_AS(forEachIndex(0, 1000, threads,
                                     [](std::size_t i) {
                                         if (i == 37) {
                                             throw std::runtime_error("job 37");
                                         }
                                     }),
                        std::runtime_error);
        std::atomic<std::size_t> sum{0};
        forEachIndex(0, 100, threads, [&](std::size_t i) { sum += i; });
        CHECK(sum == 4950);
    }
}

TEST_CASE("forEachIndex runs jobs on several threads at once", "[parallel]") {
    // Both jobs wait for each other: one thread alone would wait out the timeout.
    std::atomic<int> arrived{0};
    std::atomic<int> metOther{0};
    forEachIndex(0, 2, 2, [&](std::size_t) {
        ++arrived;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (arrived.load() < 2 && std::chrono::steady_clock::now() < until) {
            std::this_thread::yield();
        }
        if (arrived.load() == 2) {
            ++metOther;
        }
    });
    CHECK(metOther == 2);
}

TEST_CASE("forEachIndex runs nested calls to the end", "[parallel]") {
    std::atomic<std::size_t> sum{0};
    forEachIndex(0, 16, 8, [&](std::size_t outer) {
        forEachIndex(0, 100, 8, [&](std::size_t inner) { sum += outer * 100 + inner; });
    });
    CHECK(sum == 1600 * 1599 / 2);
}

TEST_CASE("forEachIndex serves several callers at once", "[parallel]") {
    constexpr std::size_t kCallers = 4;
    constexpr std::size_t kCalls = 200;
    std::vector<std::size_t> sums(kCallers);
    std::vector<std::thread> callers;
    for (std::size_t c = 0; c < kCallers; ++c) {
        callers.emplace_back([&sums, c] {
            for (std::size_t call = 0; call < kCalls; ++call) {
                std::atomic<std::size_t> sum{0};
                forEachIndex(0, 50, 4, [&](std::size_t i) { sum += i; });
                sums[c] += sum;
            }
        });
    }
    for (std::thread& caller : callers) {
        caller.join();
    }
    for (const std::size_t sum : sums) {
        CHECK(sum == kCalls * 1225);
    }
}
