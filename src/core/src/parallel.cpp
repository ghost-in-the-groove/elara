// SPDX-License-Identifier: AGPL-3.0-or-later
#include "elara/core/parallel.hpp"

#include <utility>

namespace elara::core {

WorkerPool::WorkerPool(unsigned threads) {
    threads_.reserve(std::max(1U, threads));
    for (unsigned t = 0; t < std::max(1U, threads); ++t) {
        threads_.emplace_back([this] { work(); });
    }
}

WorkerPool::~WorkerPool() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& thread : threads_) {
        thread.join();
    }
}

WorkerPool& WorkerPool::shared() {
    static WorkerPool pool(std::max(1U, std::thread::hardware_concurrency()) - 1);
    return pool;
}

void WorkerPool::post(std::shared_ptr<detail::PoolTask> task, unsigned copies) {
    if (copies == 0) {
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        batches_.push_back({.task = std::move(task), .left = copies});
    }
    if (copies >= threads_.size()) {
        wake_.notify_all();
    } else {
        for (unsigned c = 0; c < copies; ++c) {
            wake_.notify_one();
        }
    }
}

void WorkerPool::work() {
    std::unique_lock lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stopping_ || !batches_.empty(); });
        if (batches_.empty()) {
            return;
        }
        Batch& batch = batches_.front();
        std::shared_ptr<detail::PoolTask> task = batch.task;
        if (--batch.left == 0) {
            batches_.pop_front();
        }
        lock.unlock();
        task->run();
        task.reset();
        lock.lock();
    }
}

} // namespace elara::core
