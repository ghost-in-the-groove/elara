// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QThreadPool>
#include <catch2/catch_test_macros.hpp>
#include <job_lane.hpp>

#include <future>
#include <memory>
#include <vector>

using elara::app::JobLane;
using elara::app::LaneWork;

namespace {

// Processes events until `done` holds or five seconds have gone by.
template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// Holds the jobs that wait on it until open() is called.
class Gate {
public:
    void wait() const { opened_.wait(); }
    void open() { promise_.set_value(); }

private:
    std::promise<void> promise_;
    std::shared_future<void> opened_ = promise_.get_future().share();
};

} // namespace

TEST_CASE("A burst of requests runs the first and the last, and delivers the last", "[app][jobs]") {
    QObject context;
    QThreadPool pool;
    JobLane<int> lane(&context, {}, &pool);
    auto gate = std::make_shared<Gate>();
    std::vector<int> delivered;
    for (int n = 1; n <= 10; ++n) {
        lane.request(
            [gate, n] {
                gate->wait();
                return n;
            },
            [&delivered](int value) { delivered.push_back(value); });
    }
    CHECK(lane.busy());
    gate->open();
    CHECK(settle([&lane] { return !lane.busy(); }));
    CHECK(delivered == std::vector<int>{10});
    CHECK(lane.work() == LaneWork{.asked = 10, .run = 2, .skipped = 8, .stale = 1});
}

TEST_CASE("Requests apart in time are each delivered, in order", "[app][jobs]") {
    QObject context;
    JobLane<int> lane(&context);
    std::vector<int> delivered;
    for (int n = 1; n <= 3; ++n) {
        lane.request([n] { return n; }, [&delivered](int value) { delivered.push_back(value); });
        REQUIRE(
            settle([&delivered, n] { return delivered.size() == static_cast<std::size_t>(n); }));
    }
    CHECK(delivered == std::vector<int>{1, 2, 3});
    CHECK(lane.work() == LaneWork{.asked = 3, .run = 3});
}

TEST_CASE("Nothing that runs or waits when a lane drops its work is delivered", "[app][jobs]") {
    QObject context;
    QThreadPool pool;
    int busyChanges = 0;
    JobLane<int> lane(&context, [&busyChanges] { ++busyChanges; }, &pool);
    auto gate = std::make_shared<Gate>();
    std::vector<int> delivered;
    const auto record = [&delivered](int value) {
        delivered.push_back(value);
    };
    lane.request(
        [gate] {
            gate->wait();
            return 1;
        },
        record);
    lane.request([] { return 2; }, record);
    lane.drop();
    gate->open();
    CHECK(settle([&lane] { return !lane.busy(); }));
    CHECK(delivered.empty());
    CHECK(busyChanges == 2); // busy, then idle
    // What is asked after the drop is delivered.
    lane.request([] { return 3; }, record);
    CHECK(settle([&delivered] { return !delivered.empty(); }));
    CHECK(delivered == std::vector<int>{3});
    CHECK(lane.work() == LaneWork{.asked = 3, .run = 2, .skipped = 1, .stale = 1});
}

TEST_CASE("A lane is idle when its result is delivered, so the result may ask again",
          "[app][jobs]") {
    QObject context;
    JobLane<int> lane(&context);
    std::vector<int> delivered;
    std::function<void(const int&)> again = [&](int value) {
        CHECK_FALSE(lane.busy());
        delivered.push_back(value);
        if (value < 3) {
            lane.request([value] { return value + 1; }, again);
        }
    };
    lane.request([] { return 1; }, again);
    CHECK(settle([&delivered] { return delivered.size() == 3; }));
    CHECK(delivered == std::vector<int>{1, 2, 3});
}
