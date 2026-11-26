// SPDX-License-Identifier: AGPL-3.0-or-later
// A flow of worker jobs whose results matter only while they are the newest
// asked for (ADR-0110).
#pragma once

#include "when_done.hpp"
#include <QObject>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace elara::app {

/// What a lane did since it was made.
struct LaneWork {
    std::uint64_t asked = 0;   ///< requests made
    std::uint64_t run = 0;     ///< jobs started
    std::uint64_t skipped = 0; ///< requests a newer one, or drop(), replaced before they ran
    std::uint64_t stale = 0;   ///< jobs run whose result was not delivered

    friend bool operator==(const LaneWork&, const LaneWork&) = default;
};

/// A flow of jobs run on worker threads, such as laying the score out, whose
/// results matter only while they are the newest asked for.
///
/// At most one job runs. A request made meanwhile waits, replacing the one
/// already waiting, and its job starts once the running one ends. A job's
/// result reaches its `done` on `context`'s thread, from its event loop, only
/// if no request came after it and drop() was not called since. So a burst of
/// requests runs two jobs, the first and the last, however long the burst,
/// and a result older than one already delivered is never delivered.
///
/// A job cannot be stopped once it runs: drop() only stops waiting for it.
/// The lane must not outlive `context`.
template <typename Result>
class JobLane {
public:
    using Job = std::function<Result()>;
    using Done = std::function<void(const Result&)>;

    /// `busyChanged`, when given, is called each time busy() changes.
    explicit JobLane(QObject* context, std::function<void()> busyChanged = {},
                     QThreadPool* pool = QThreadPool::globalInstance())
        : context_(context), busyChanged_(std::move(busyChanged)), pool_(pool) {}

    /// Asks for `job`, run at once when no job runs, else once the running one
    /// ends; `done` gets its result unless a newer request comes first.
    void request(Job job, Done done) {
        ++work_.asked;
        if (running_) {
            if (waiting_) {
                ++work_.skipped;
            }
            waiting_.emplace(Request{.job = std::move(job), .done = std::move(done)});
            return;
        }
        start(Request{.job = std::move(job), .done = std::move(done)});
        notify();
    }

    /// Delivers nothing of what runs or waits now: the song they were for is
    /// gone.
    void drop() {
        ++generation_;
        if (waiting_) {
            ++work_.skipped;
            waiting_.reset();
        }
    }

    /// Whether a job runs.
    [[nodiscard]] bool busy() const noexcept { return running_; }
    [[nodiscard]] const LaneWork& work() const noexcept { return work_; }

private:
    struct Request {
        Job job;
        Done done;
    };

    void start(Request request) {
        running_ = true;
        ++work_.run;
        whenDone(
            QtConcurrent::run(pool_, std::move(request.job)), context_,
            [this, generation = generation_, done = std::move(request.done)](const Result& result) {
                running_ = false;
                if (waiting_) {
                    ++work_.stale; // a newer request waits: run it instead
                    Request next = std::move(*waiting_);
                    waiting_.reset();
                    start(std::move(next));
                    return;
                }
                notify();
                if (generation != generation_) {
                    ++work_.stale;
                    return;
                }
                done(result); // which may ask again
            });
    }

    void notify() const {
        if (busyChanged_) {
            busyChanged_();
        }
    }

    QObject* context_;
    std::function<void()> busyChanged_;
    QThreadPool* pool_;
    std::optional<Request> waiting_;
    bool running_ = false;
    std::uint64_t generation_ = 0; ///< bumped by drop()
    LaneWork work_;
};

} // namespace elara::app
