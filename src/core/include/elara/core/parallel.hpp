// SPDX-License-Identifier: AGPL-3.0-or-later
// Runs independent jobs (parts, tracks, systems) on several threads.
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace elara::core {

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

namespace detail {

/// Work queued on a `WorkerPool`.
class PoolTask {
public:
    PoolTask() = default;
    PoolTask(const PoolTask&) = delete;
    PoolTask& operator=(const PoolTask&) = delete;
    virtual ~PoolTask() = default;

    /// Runs on a pool thread; must not throw.
    virtual void run() noexcept = 0;
};

} // namespace detail

/// Threads kept waiting for work, so that running jobs in parallel does not
/// start and join threads every time (about 0.5 ms with every core). One pool
/// serves the whole program: `WorkerPool::shared()`.
class WorkerPool {
public:
    /// Starts `threads` threads, at least one.
    explicit WorkerPool(unsigned threads);
    /// Lets the queued tasks finish, then stops the threads.
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /// The program's pool, started on first use: one thread fewer than the
    /// cores, since the thread that asks for work runs jobs too, and at least one.
    [[nodiscard]] static WorkerPool& shared();

    [[nodiscard]] unsigned size() const noexcept { return static_cast<unsigned>(threads_.size()); }

    /// Queues `task` to run `copies` times, each on whichever thread is free.
    void post(std::shared_ptr<detail::PoolTask> task, unsigned copies);

private:
    struct Batch {
        std::shared_ptr<detail::PoolTask> task;
        unsigned left = 0;
    };

    void work();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Batch> batches_;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

namespace detail {

/// One `forEachIndex` call: the indices not yet taken, and the pool threads
/// that joined in.
template <typename Job>
class IndexRun final : public PoolTask {
public:
    IndexRun(std::size_t first, std::size_t last, const Job& job)
        : next_(first), last_(last), job_(&job) {}

    /// A pool thread joins in, unless the caller has already finished: the job
    /// may live on the caller's stack.
    void run() noexcept override {
        {
            const std::scoped_lock lock(mutex_);
            if (closed_) {
                return;
            }
            ++running_;
        }
        takeJobs();
        const std::scoped_lock lock(mutex_);
        if (--running_ == 0) {
            idle_.notify_all();
        }
    }

    /// The caller takes jobs too, then waits for the pool threads that joined in
    /// and keeps the others out. Rethrows the first exception a job threw.
    void finish() {
        takeJobs();
        std::unique_lock lock(mutex_);
        closed_ = true;
        idle_.wait(lock, [this] { return running_ == 0; });
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

private:
    void takeJobs() noexcept {
        try {
            for (std::size_t i = next_++; i < last_; i = next_++) {
                (*job_)(i);
            }
        } catch (...) {
            next_ = last_; // the call fails anyway: hand out no more jobs
            const std::scoped_lock lock(mutex_);
            if (!error_) {
                error_ = std::current_exception();
            }
        }
    }

    std::atomic<std::size_t> next_;
    const std::size_t last_;
    const Job* job_;
    std::mutex mutex_;
    std::condition_variable idle_;
    unsigned running_ = 0;
    bool closed_ = false;
    std::exception_ptr error_;
};

} // namespace detail

/// Calls `job(i)` once for every i in [first, last), on the calling thread when
/// `threads` is 1 and otherwise on the calling thread and up to `threads - 1`
/// threads of `WorkerPool::shared()`, each taking the next index until none is
/// left. Once a job throws, no further jobs start, and after every thread stops
/// the first exception is rethrown. Jobs must not touch each other's data; they
/// may call `forEachIndex` themselves (the caller always works too, so a busy
/// pool only means fewer helpers).
template <typename Job>
void forEachIndex(std::size_t first, std::size_t last, unsigned threads, const Job& job) {
    if (threads <= 1 || last <= first + 1) {
        for (std::size_t i = first; i < last; ++i) {
            job(i);
        }
        return;
    }
    auto run = std::make_shared<detail::IndexRun<Job>>(first, last, job);
    const std::size_t helpers = std::min<std::size_t>(threads - 1, last - first - 1);
    WorkerPool::shared().post(run, static_cast<unsigned>(helpers));
    run->finish();
}

} // namespace elara::core
